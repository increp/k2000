# v2 — Layer abstraction

**Date:** 2026-06-11
**Status:** Implemented (tagged v2.0.0 on 2026-06-14).
**Scope:** Structural refactor that introduces the `Layer` abstraction. v1's audio behavior is preserved end-to-end but now expressed through the new abstraction. Foundation for the v3+ work.

## Goal

Split today's `Voice` into two distinct concerns matching the K2000 mental model:

- **Layer** — *configuration* container. Owns the algorithm topology, the parameter values, and the per-Layer DSP block instances. There is exactly one Layer in v2 (held by a placeholder `Program`).
- **Voice** — *runtime* instance. One per playing note. Holds note state (pitch, velocity, envelope position) and renders by walking its Layer's algorithm topology.

By v2 ship: the plugin sounds identical to v1, but the data model is ready for v3 (algorithm selection within a Layer) and v4 (Programs holding multiple Layers).

## Non-goals (deferred to later phases)

- Multiple algorithms in the library — the slot chain stays fixed at v2. (v3.)
- Multiple Layers per Program — Program has exactly one Layer slot at v2. (v4.)
- Multi-MIDI / Layer / Split modes — single MIDI flow at v2. (v4.)
- New DSP block types — same SVF filter + waveshaper as v1. (v5.)
- Mod matrix, LFOs, additional envelopes. (v6.)
- Any UI restructure beyond the param-ID rename. (v9+.)

See [`../roadmap/phases.md`](../roadmap/phases.md) for what each subsequent phase adds.

## Signal flow

```
MIDI in ──▶ PluginProcessor ──▶ Program ──▶ Layer
                                              │ (config: algorithm,
                                              │  per-Layer block instances,
                                              │  ParamSnapshot)
                                              ▼
                              VoiceManager ──▶ Voice[0..7]
                                                  │ (runtime: note, env state,
                                                  │  walks Layer's algorithm)
                                                  ▼
                                ┌──────────────────────────────┐
                                │  Oscillator                  │
                                │     │                        │
                                │     ▼                        │
                                │  Layer.slot[0] (SVFFilter)   │
                                │     │                        │
                                │     ▼                        │
                                │  Layer.slot[1] (Waveshaper)  │
                                │     │                        │
                                │     ▼                        │
                                │  Amp ◀── ADSR envelope       │
                                └──────────────────┬───────────┘
                                                   ▼
                                            Voice output mix
                                                   │
                                                   ▼
                                            Stereo bus → host
```

The key architectural change: **the DSP block instances live in the `Layer`, not on each `Voice`.** A Voice walks the topology by *reading* the Layer's `slot[i]` block and calling its `process(...)`. This is the v1 model expressed cleanly — currently each Voice owns its own block instances, which works for stateless block configs but doesn't model "one Layer plays through these blocks" correctly. v2 fixes the ownership.

**Per-voice state vs. shared block state.** Stateful blocks like `SVFFilter` *do* hold per-voice integrator state. v2 resolves this by giving each block instance a small per-Voice state struct that the Voice owns; the block itself is configuration + render logic owned by the Layer. The Voice passes its own per-block state into the block's `process(...)` call.

**Polyphony:** still 8 voices in v2.

## Module layout (v2)

| Module | Responsibility | Change from v1 |
|---|---|---|
| `PluginProcessor` | JUCE `AudioProcessor`; owns Program, dispatches MIDI to VoiceManager, manages state save/load | Now owns a Program (was: directly owned VoiceManager) |
| `PluginEditor` | JUCE `AudioProcessorEditor`; same control set as v1 | Param IDs updated for namespacing |
| `Program` | Container for 1..N Layers (1 in v2). Holds Layer-level routing eventually (split point, MIDI channel routing) | **New** |
| `Layer` | Holds algorithm config, DSP block instances, ParamSnapshot, ADSR envelope config | **New** (extracted from Voice) |
| `VoiceManager` | Voice allocation, stealing, MIDI routing, voice-output mix | Now asks "which Layer should this MIDI event go to?" — trivially answered with 1 Layer in v2 |
| `Voice` | One playing note: note state, envelope position, per-block voice-local state. Renders by walking its Layer's algorithm. | Slimmer: no longer owns block instances |
| `Algorithm` | Passive data struct: `{ slot_count, block_type_per_slot, render_order }` | **New** |
| `dsp/Oscillator` | PolyBLEP oscillator | Unchanged |
| `dsp/DSPBlock` | Abstract base for swappable processing units | Add a `VoiceState` nested type so blocks can declare their per-voice state shape |
| `dsp/blocks/SVFFilter` | State-variable filter | Per-voice integrator state moves to nested `VoiceState` |
| `dsp/blocks/Waveshaper` | tanh / soft-clip drive | Trivially stateless; nested `VoiceState` is empty |
| `dsp/Envelope` | ADSR | Per-voice state stays in Voice (unchanged) |
| `params/Parameters` | APVTS layout — all IDs now prefixed `layer.` | Renamed param IDs |
| `params/ParamSnapshot` | Audio-thread-safe snapshot, now living inside Layer | Moved (was: owned by PluginProcessor) |
| `gui/` | Still empty; PluginEditor handles UI directly | Unchanged |

If `Voice` shrinks below ~100 lines (likely after extraction), that's fine. If `Layer` grows above ~250 lines (possible if it accumulates algorithm-walking helpers), consider whether algorithm-walk logic belongs in a free function instead.

## Architectural commitments (new ADRs for v2)

- **ADR 0005 — Voice/Layer split: K2000-faithful runtime/config separation** (to be written alongside spec).
- **ADR 0006 — Algorithm as passive data structure** (no virtual `render()`; Voice walks the topology).
- **ADR 0007 — Parameter namespace migration to `layer.*` with v1-preset migration shim.**

Existing ADRs (0001–0004) are unchanged and still load-bearing.

## Parameter set (v2)

Same controls as v1 — only the IDs change. All params now namespaced under `layer.`:

| Group | v2 ID | v1 ID (for migration) | Range / values |
|---|---|---|---|
| Oscillator | `layer.osc.waveform` | `osc.waveform` | { Saw, Square, Triangle, Sine } |
| Oscillator | `layer.osc.coarse` | `osc.coarse` | -24..+24 semitones |
| Oscillator | `layer.osc.fine` | `osc.fine` | -100..+100 cents |
| Slot 0 — SVF Filter | `layer.slot0.type` | `slot0.type` | { LP, HP, BP, Notch } |
| Slot 0 — SVF Filter | `layer.slot0.cutoff` | `slot0.cutoff` | 20 Hz..20 kHz (log) |
| Slot 0 — SVF Filter | `layer.slot0.resonance` | `slot0.resonance` | 0..1 |
| Slot 1 — Waveshaper | `layer.slot1.drive` | `slot1.drive` | 0..1 |
| Slot 1 — Waveshaper | `layer.slot1.mix` | `slot1.mix` | 0..1 |
| Amp env | `layer.amp.attack` | `amp.attack` | 1 ms..5 s (log) |
| Amp env | `layer.amp.decay` | `amp.decay` | 1 ms..5 s (log) |
| Amp env | `layer.amp.sustain` | `amp.sustain` | 0..1 |
| Amp env | `layer.amp.release` | `amp.release` | 1 ms..5 s (log) |
| Master | `master.gain` | `master.gain` | -60..+6 dB |

`master.gain` stays at the top level because it's not Layer-scoped — when v4 introduces multiple Layers, master is still one knob downstream of the Program mix.

(The exact v1 IDs above are the ones registered in v1's `params/Parameters.cpp`. The migration shim in `setStateInformation` reads them and writes the new IDs.)

## State / preset save and load

JUCE's `AudioProcessorValueTreeState` handles serialization. Two changes:

1. **Param IDs change.** v1 presets won't load by default after the rename.
2. **Migration shim.** `setStateInformation` checks the loaded XML's root element for a `v=2` attribute. If absent (v1 preset), the shim walks the XML before APVTS reads it and rewrites old IDs → new IDs. New presets always save with `v=2`.

The migration shim is ~30 lines, table-driven (one entry per renamed ID), and well-tested. It runs once per preset load and is removed in a future major version (probably v6+).

Per-slot block type IDs continue to be saved alongside parameter values (carried over from v1) — they're not yet user-selectable but the format is in place for v3.

## GUI

Unchanged from v1 — plain JUCE controls, FlexBox layout, attached via `SliderAttachment` / `ComboBoxAttachment`. Attachment IDs updated to match the new param IDs. Looks identical to v1.

## Real-time constraints

The audio thread (`processBlock`) must still never allocate, lock, block, or throw. The v2 refactor changes ownership but preserves these guarantees:

- All Layer/Voice state allocated in `prepareToPlay`.
- ParamSnapshot is now read at block start by the Layer (was: by Voice) — still atomic, still lock-free.
- Voice's walk through `Layer::slot[i]` is a fixed-iteration loop, no allocation.
- DSP block per-voice state is preallocated as part of the Voice (no per-block allocation).

The existing v1 RT-safety guard (jassert + thread-local sentinel) covers v2 unchanged. The v2 test suite re-runs it.

## Testing strategy

Same three layers as v1:

1. **Pure DSP unit tests.** Mostly unchanged from v1 — Oscillator, SVFFilter, Waveshaper, Envelope tests stay green through the refactor since the underlying DSP doesn't change.
2. **Layer-level tests** (**new**). Given a Layer config + parameter values, a Voice rendering through that Layer should produce expected audio characteristics (sine at expected frequency, low-pass attenuates Nyquist, etc.). These confirm the Layer/Voice split works end-to-end.
3. **Plugin-lifecycle tests.** Updated to assert that:
   - v2 presets round-trip with `v=2` attribute.
   - v1 presets load through the migration shim and round-trip identically to a fresh v2 save.
   - Param IDs in the APVTS match the v2 namespace.

Manual smoke test: load in Carla on Linux + Ableton on Windows; confirm audio is bit-identical (or perceptually identical) to a v1.0.0 build for the same MIDI input + parameter settings.

## Project layout (v2)

```
k2000/
├── CMakeLists.txt                    # unchanged
├── README.md
├── .github/workflows/build.yml       # unchanged
├── docs/
├── src/
│   ├── PluginProcessor.{h,cpp}       # now owns Program
│   ├── PluginEditor.{h,cpp}          # attachment IDs updated
│   ├── Program.{h,cpp}               # NEW (placeholder, 1 Layer in v2)
│   ├── Layer.{h,cpp}                 # NEW
│   ├── VoiceManager.{h,cpp}          # MIDI dispatch trivially asks Program for Layer
│   ├── Voice.{h,cpp}                 # slimmer: note state + algorithm walk
│   ├── dsp/
│   │   ├── Oscillator.{h,cpp}        # unchanged
│   │   ├── Envelope.{h,cpp}          # unchanged
│   │   ├── DSPBlock.h                # gains nested VoiceState support
│   │   ├── Algorithm.{h,cpp}         # NEW (passive data struct)
│   │   └── blocks/
│   │       ├── SVFFilter.{h,cpp}     # per-voice state extracted
│   │       └── Waveshaper.{h,cpp}    # per-voice state extracted (empty)
│   ├── params/
│   │   ├── Parameters.{h,cpp}        # IDs renamed to layer.*
│   │   └── ParamSnapshot.h           # now owned by Layer
│   └── gui/                          # still empty in v2
├── tests/
│   ├── CMakeLists.txt
│   ├── (existing DSP unit tests, mostly unchanged)
│   ├── LayerTests.cpp                # NEW
│   ├── PresetMigrationTests.cpp      # NEW
│   └── PluginLifecycleTests.cpp      # updated for v=2
└── third_party/JUCE/                 # unchanged submodule
```

Approximate file-size deltas:
- `Voice.{h,cpp}` shrinks from ~250 lines combined to ~120 lines.
- `Layer.{h,cpp}` is ~200 lines combined.
- `Program.{h,cpp}` is ~60 lines (mostly forwarding; gains real content at v4).
- `Algorithm.{h,cpp}` is ~80 lines (data + a small builder for the one v2 algorithm).
- Net code added: ~210 lines; net code removed: ~130 lines.

## Build / install / test

Unchanged from v1:

```bash
cmake -B build -S .
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build
```

Windows build artifact still produced via GitHub Actions, dropped into Ableton on the user's Windows box for listening confirmation.

## Definition of done for v2

- [ ] All v1 tests pass against the refactored code.
- [ ] New Layer-level tests pass.
- [ ] Preset migration test: a v1 preset (saved by the v1.0.0 build) loads cleanly through the v2 migration shim, and a subsequent save produces the same effective parameter values as a fresh v2-only save.
- [ ] Plugin loads in Carla on Linux and Ableton 12 on Windows; audio output for a held note at default settings is perceptually identical to v1.0.0.
- [ ] No allocation or locking in `processBlock` (existing RT-safety sentinel still green).
- [ ] All four new ADRs written (0005, 0006, 0007) and linked from this spec.
- [ ] Roadmap updated to mark v2 as complete.

## Risks

- **Per-voice state extraction for stateful blocks.** SVFFilter currently keeps integrator state inside the block instance. v2 moves the state into a nested struct owned by Voice. Risk: subtle pointer-aliasing bugs if the Voice doesn't pass the right state to the right block. Mitigation: Layer-level test compares pre/post-refactor render against the same input parameters.
- **Preset migration shim correctness.** A bug in the rename table silently loads the wrong values. Mitigation: parametrized test that walks the full v1 param list and asserts round-trip equivalence.
- **Scope creep.** It will be tempting to start the v3 algorithm-selection work "while we're refactoring." Resist. v2 ships v2.

## Notes on prior recommendations

- The [v2 known concerns doc](../roadmap/v2-known-concerns.md) suggests splitting `ParamSnapshot` into sub-structs (`OscSnapshot[3]`, `FilterSnapshot`, `ShaperSnapshot`, etc.) "by v2." That suggestion was written under the original v2 scope (Peak character — 3 NCOs + analog filter). Under Path B, v2 keeps v1's 13-field parameter set; splitting `ParamSnapshot` is premature here and would add structure without solving any current problem. Revisit at v5 (Summit-character blocks) when the snapshot actually grows.
- `DSPBlock::getParamSpecs()` is still half-implemented (returns `{}` for SVF + Waveshaper, parallel APVTS layout hand-coded). v2 inherits this state unchanged; the call is made at v3 when blocks become user-selectable per slot.
