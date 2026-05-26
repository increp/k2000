# v1 — Skeleton end-to-end

**Date:** 2026-05-25
**Status:** Approved, not yet implemented
**Scope:** The first version of the plugin that ships, makes sound, and proves the architecture.

## Goal

Get a working VST3 synth that:
- Loads in a DAW (Reaper/Bitwig/Carla on Linux for dev; Ableton 12 on Windows for listening tests).
- Responds to MIDI, plays polyphonically, saves/restores state.
- Has its DSP organized around the **polymorphic DSP-block** architecture that VAST will eventually exercise — even though v1 only ships one fixed two-slot chain.

The aim is **not** to sound like a finished Peak+VAST hybrid. It is to build the bones so that every subsequent phase has somewhere to plug into.

## Non-goals (deferred to later phases)

- Multiple oscillators per voice (Peak has 3 NCOs).
- Analog-modeled multimode filter (the signature Peak sound).
- Modulation matrix, LFOs, additional envelopes.
- Effects section (chorus / delay / reverb).
- Multiple VAST algorithms; user-selectable block per slot.
- More DSP block types beyond SVF filter + waveshaper.
- Wavetable / sample sources.
- Photoreal UI using hardware photographs.
- macOS build target.
- Preset browser UI.

See [`../roadmap/phases.md`](../roadmap/phases.md) for when each lands.

## Signal flow

```
MIDI in ──▶ PluginProcessor ──▶ VoiceManager ──▶ Voice[0..7]
                                                    │
                                                    ▼
                              ┌─────────────────────────────────────┐
                              │  Oscillator                         │
                              │     │                               │
                              │     ▼                               │
                              │  DSPBlock slot 0 (SVFFilter)        │
                              │     │                               │
                              │     ▼                               │
                              │  DSPBlock slot 1 (Waveshaper)       │
                              │     │                               │
                              │     ▼                               │
                              │  Amp ◀── ADSR envelope              │
                              └──────────────────┬──────────────────┘
                                                 ▼
                                          Voice output mix
                                                 │
                                                 ▼
                                          Stereo bus → host
```

Voice rendering is mono per voice; stereo widening at the voice-mix stage. Sample rate and block size come from the host; no internal resampling.

**Polyphony:** 8 voices. Enough to exercise voice stealing without overcomplicating v1.

## Module layout

| Module | Responsibility |
|---|---|
| `PluginProcessor` | JUCE `AudioProcessor`; owns parameters, voice manager, MIDI dispatch, state save/load |
| `PluginEditor` | JUCE `AudioProcessorEditor`; minimal control panel bound to parameters |
| `VoiceManager` | Voice allocation, stealing, MIDI routing, voice-output mix |
| `Voice` | One playing note: oscillator + two DSP block slots + ADSR amp envelope |
| `dsp/Oscillator` | PolyBLEP anti-aliased oscillator: saw / square / triangle / sine |
| `dsp/DSPBlock` | Abstract base for swappable processing units. See [`../architecture/dsp-block-interface.md`](../architecture/dsp-block-interface.md). |
| `dsp/blocks/SVFFilter` | State-variable filter (LP/HP/BP/Notch) — first concrete block |
| `dsp/blocks/Waveshaper` | tanh / soft-clip drive — second concrete block |
| `dsp/Envelope` | ADSR amp envelope |
| `params/Parameters` | Single source of truth for parameter IDs, ranges, defaults |
| `params/ParamSnapshot` | Audio-thread-safe snapshot of current parameter values |
| `gui/` | (empty in v1; PluginEditor handles all UI directly) |

One concern per file; if a file grows past ~300 lines, it's probably doing too much.

## Architectural commitments

These are the choices that v1 establishes for every later phase to build on. Each has an ADR explaining the *why*:

- [ADR 0001 — JUCE framework](../decisions/0001-juce-framework.md)
- [ADR 0002 — Polymorphic DSP slots over hardcoded chain](../decisions/0002-polymorphic-dsp-slots.md)
- [ADR 0003 — Windows builds via GitHub Actions, not local cross-compile](../decisions/0003-windows-via-github-actions.md)
- [ADR 0004 — Defer photoreal UI to a later phase](../decisions/0004-defer-photoreal-ui.md)

## Parameter set (v1)

About a dozen controls. All registered in a single `juce::AudioProcessorValueTreeState`.

| Group | Parameter | Range / values |
|---|---|---|
| Oscillator | waveform | { Saw, Square, Triangle, Sine } |
| Oscillator | coarse tune | -24 .. +24 semitones |
| Oscillator | fine tune | -100 .. +100 cents |
| Slot 0 — SVF Filter | type | { LP, HP, BP, Notch } |
| Slot 0 — SVF Filter | cutoff | 20 Hz .. 20 kHz (log) |
| Slot 0 — SVF Filter | resonance | 0 .. 1 |
| Slot 1 — Waveshaper | drive | 0 .. 1 |
| Slot 1 — Waveshaper | mix | 0 .. 1 (dry/wet) |
| Amp env | attack | 1 ms .. 5 s (log) |
| Amp env | decay | 1 ms .. 5 s (log) |
| Amp env | sustain | 0 .. 1 |
| Amp env | release | 1 ms .. 5 s (log) |
| Master | output gain | -60 .. +6 dB |

Block parameters are namespaced by slot in the parameter tree: `slot0.cutoff`, `slot1.drive`, etc. This survives future phases where the user picks block per slot.

## State / preset save and load

JUCE's `AudioProcessorValueTreeState` handles parameter serialization automatically. Preset format from day one includes per-slot block type IDs (`{ slot: 0, type: "svf_filter" }`) even though v1 fixes them — this is what makes v1 presets forward-compatible when v4 lets users swap blocks.

## GUI (v1)

Plain JUCE controls in a single panel:
- Rotary sliders for all continuous parameters
- Combo boxes for the two waveform/type selectors
- Auto-laid-out via `juce::FlexBox` or simple grid
- Bound to parameters via `SliderAttachment` / `ComboBoxAttachment`

Ugly by design. Photoreal UI deferred — see [ADR 0004](../decisions/0004-defer-photoreal-ui.md).

## Real-time constraints

The audio thread (`processBlock`) must never:
- Allocate or free memory.
- Take locks.
- Make system calls (file I/O, logging, mutex operations).
- Throw exceptions.

Mechanisms we use to honor this:
- All voice/block state is allocated in `prepareToPlay`, never in `processBlock`.
- Parameter changes from the GUI are read from the parameter tree's atomic value-pointers at block start, into a `ParamSnapshot` struct. No locks.
- `reset()` on voices (called by `VoiceManager` during voice stealing) is RT-safe — internal state clear only, no allocation.

## Testing strategy

Three layers:

1. **Pure DSP unit tests** (most valuable). Each block / oscillator tested in isolation. Examples:
   - Oscillator: at 440 Hz @ 48 kHz, sine output's FFT peak is at the expected bin.
   - SVF LP at known cutoff: DC passes, Nyquist is attenuated by > X dB.
   - Waveshaper: bounded output, monotonic mapping for the relevant range.
2. **Plugin-lifecycle tests.** Construct the processor, call `prepareToPlay`, send MIDI, call `processBlock`, assert non-silent output after note-on, assert silence after release. Catches plumbing bugs.
3. **Manual DAW testing.** Reaper/Bitwig/Carla on Linux for dev; Ableton 12 on Windows (via CI build artifact) for golden-path listening checks.

Framework: `juce::UnitTest` for layers 1 and 2. No external test dependency.

## Project layout

```
k2000/
├── CMakeLists.txt
├── README.md
├── .github/
│   └── workflows/
│       └── build.yml          # Linux + Windows build matrix; uploads .vst3 artifacts
├── docs/                      # this folder
├── src/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── VoiceManager.{h,cpp}
│   ├── Voice.{h,cpp}
│   ├── dsp/
│   │   ├── Oscillator.{h,cpp}
│   │   ├── Envelope.{h,cpp}
│   │   ├── DSPBlock.h
│   │   └── blocks/
│   │       ├── SVFFilter.{h,cpp}
│   │       └── Waveshaper.{h,cpp}
│   ├── params/
│   │   ├── Parameters.{h,cpp}
│   │   └── ParamSnapshot.h
│   └── gui/                   # empty in v1
├── tests/
│   ├── CMakeLists.txt
│   ├── OscillatorTests.cpp
│   ├── SVFFilterTests.cpp
│   ├── WaveshaperTests.cpp
│   ├── EnvelopeTests.cpp
│   └── PluginLifecycleTests.cpp
└── third_party/
    └── JUCE/                  # git submodule, pinned version
```

## Build / install / test

Local (Linux):

```bash
cmake -B build -S .
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build   # copies build/k2000.vst3 to ~/.vst3/
```

Windows VST3 for Ableton:
1. Push to GitHub.
2. GitHub Actions workflow builds Linux + Windows.
3. Download the `k2000-windows-<sha>.zip` artifact from the Actions tab.
4. Unzip into `%COMMONPROGRAMFILES%\VST3\` (or your user VST3 folder).
5. Rescan plugins in Ableton 12.

## Definition of done for v1

- [ ] CMake project builds clean on Linux locally.
- [ ] GitHub Actions builds Linux + Windows VST3 artifacts on every push.
- [ ] All `juce::UnitTest` tests pass on both platforms in CI.
- [ ] Plugin loads in Carla on Linux and in Ableton 12 on Windows.
- [ ] Playing a MIDI keyboard produces audible polyphonic sound through the full signal chain.
- [ ] Filter cutoff/resonance audibly affect tone in all four filter modes.
- [ ] Waveshaper drive audibly distorts.
- [ ] ADSR envelope behaves correctly (clean attack/decay/sustain/release).
- [ ] Parameter changes from the GUI are reflected in audio without crackles.
- [ ] Preset save/restore round-trips all parameters.
- [ ] No allocation or locking detected in `processBlock` (verified in debug builds via an audio-thread sentinel — a thread-local flag set on entry to `processBlock` and asserted-not-set in custom `operator new` / lock acquisitions in debug builds, or equivalent tooling).
