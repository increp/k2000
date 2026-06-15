# v4 — Multi-Layer Programs

**Status:** Design proposed, 2026-06-15.

**Scope:** Make a `Program` hold multiple `Layer`s, routed by per-layer key range, velocity range, and MIDI channel, played from a shared voice pool. v4 fully parameterizes **2 layers** (delivering Summit's dual-engine as a 2-Layer Program) with all structures written generic over the layer count and a **64-voice** shared pool. Layer/Split/Dual behaviors *emerge* from per-layer ranges — no explicit "mode" primitive (a Summit-style mode selector is a v7 convenience).

## Goal

After v4:

- A `Program` owns `kNumLayers` (=2) `LayerSlot`s, each = a v3 `Layer` (palette/algorithm/filter/shaper/osc/amp) plus routing `{ enable, keyLo, keyHi, velLo, velHi, channel, level }`.
- A shared **64-voice** pool: on note-on, each enabled layer whose ranges/channel match the note gets a voice; one MIDI note can light a voice per matching layer.
- Parameters live under `layer0.*` / `layer1.*`; a v3 preset loads with `layer0` = the old single layer and `layer1` disabled, sounding identical.
- Summit's dual-engine is reachable: two layers, each its own algorithm/filter, combined by setting ranges (Layer = both full-range; Split = adjacent key ranges; Dual = different MIDI channels) and per-layer level.

## Background: the reference models (sourced)

Via the k2000-kb:

**K2000** (Musician's Guide p. 44, pp. 52–56): a Program consists of **1–3 layers** (up to 32 for drum programs), each a keymap + algorithm. The **LAYER page** sets each layer's **keyboard range** and attack/release. The layer is the **unit of polyphony** — a 2-layer program covering the full range triggers two voice channels per note, drawn from the 48-voice pool. So Split/Layer are not modes; they fall out of per-layer key (and velocity) ranges.

**Summit** (User Guide pp. 12–13, p. 22): bi-timbral, **16 voices**. A *Single Patch* runs both engines in tandem (16 voices). A *Multi Patch* has **Part A + Part B**, **8 voices each**, with three MULTIMODE settings: **Layer** (A+B mixed over the whole keyboard, adjustable mix), **Split** (A lower / B upper, movable split point, default C3), **Dual** (play A or B alone / separate parts). Summit also has voice *modes* (Poly, Poly2 retrigger, Mono/Mono2/MonoLG legato).

**How v4 maps:** Summit's three MULTIMODE settings are *configurations* of the K2000-style range model — Layer = both layers full key range; Split = adjacent key ranges; Dual = different MIDI channels. So v4 ships the **range-based mechanism** (the general primitive) and the modes become presets/UI over it later (v7). v4 deliberately diverges from Summit's per-part **8+8 partition** in favor of a **single shared 64-voice pool** (more flexible allocation; a quiet layer doesn't waste reserved voices).

## Architecture

```
PluginProcessor
 └─ Program
     ├─ slots[kNumLayers]                    kNumLayers = 2 (code loops over it)
     │    LayerSlot = { Layer layer;         // v3 config: palette, algorithm, snapshot
     │                  LayerRouting routing } // enable, key/vel range, channel, level
     └─ (master.gain applied at the processor)

 └─ VoiceManager
     └─ voices[64]            shared pool; each Voice holds a non-owning Layer*
```

Each audio block, `PluginProcessor` updates each of the `kNumLayers` slots: builds that layer's `ParamSnapshot` from its `layer{i}.*` DSP params and calls `layer.updateParameters(...)`, and reads its `layer{i}.*` routing params into `slot.routing` and the layer's output level. (A disabled layer is still updated — cheap — it just receives no voices.) `VoiceManager` then renders the pool; master gain is applied at the end.

`VoiceManager` binds to the `Program` (not a single Layer). On note-on it walks the slots, and for each **enabled** slot whose routing matches the note it allocates a pooled voice bound to that slot's `Layer`. Because v3 separated per-voice *state* (owned by `Voice`, one `VoiceState` per block type) from Layer-owned *config*, and all layers share the same block palette, a pooled voice can play any layer with no reallocation — `setLayer` on note-on is allocation-free.

## Parameter model and migration

Registered by a loop over `kNumLayers`, so raising the count later is a loop-bound change (and this begins to retire the v2-known hand-coded-layout debt for per-layer params).

Per layer `i ∈ {0,1}`:

| Group | IDs |
|---|---|
| DSP (the v3 set) | `layer{i}.algorithm`, `layer{i}.filter.{type,cutoff,resonance}`, `layer{i}.shaper.{drive,mix}`, `layer{i}.osc.{waveform,coarse,fine}`, `layer{i}.amp.{attack,decay,sustain,release}` |
| Routing | `layer{i}.enable` (bool), `layer{i}.keyLo`/`keyHi` (0–127), `layer{i}.velLo`/`velHi` (1–127), `layer{i}.channel` (choice: Omni, 1–16), `layer{i}.level` (dB, −60…+6) |

`master.gain` unchanged. Defaults: `layer0.enable = true`, `layer1.enable = false`; both full key/vel range, channel Omni, level 0 dB.

**v3→v4 migration** is the first **prefix rewrite**: any `layer.*` ID → `layer0.*`. The cumulative shim gains a v3→v4 step that rewrites the `layer.` prefix to `layer0.` (distinct from the existing 1:1 rename tables). State version bumps to `v=4`. With `layer1` defaulting to disabled, a v3 preset loads as a single full-range layer and sounds identical.

## Voice allocation and stealing

- **Note-on** `(note, velocity, channel)`: for each enabled slot where `keyLo ≤ note ≤ keyHi` **and** `velLo ≤ velocity ≤ velHi` **and** (`channel == Omni` or `channel == slot.channel`) — allocate a voice from the pool, `setLayer(&slot.layer)`, `noteOn`. The voice records `(note, slotIndex)`. One MIDI note may consume up to `kNumLayers` voices.
- **Note-off** `(note, channel)`: release every active voice whose recorded note matches and whose slot's channel matches the note-off channel.
- **Stealing:** when the 64-voice pool has no free voice, steal the oldest active voice (the existing strategy, now across the whole pool). Note-aware / quietest-voice stealing stays deferred.

## Rendering, per-layer level, MIDI channel

- Voices render additively into the mono mix (as today). `Voice::render` multiplies its output by `layer_->level()` (a linear gain the slot sets each block from `layer{i}.level`), alongside the amp envelope and velocity. Layer-mode A:B balance is the two layers' levels.
- `VoiceManager` reads each note message's channel (`MidiMessage::getChannel()`) for the per-layer match. Non-note events (all-notes-off) act globally. Sample-accurate sub-block rendering between events is unchanged.

## Module changes

| Module | Change |
|---|---|
| `Program` | Owns `slots[kNumLayers]` (`LayerSlot = Layer + LayerRouting`); exposes its slots. **Expanded** from the v3 single-Layer pass-through. |
| `LayerRouting` | **New** small struct `{ bool enable; int keyLo,keyHi,velLo,velHi; int channel; float levelGain; }` + a `matches(note,vel,channel)` predicate. |
| `Layer` | Gains an output `level()` (linear gain) set each block. |
| `VoiceManager` | `kNumVoices = 64`; binds to a `Program`; note-on walks active slots and allocates per match; tracks `(note, slotIndex)`; channel-aware note-off. |
| `Voice` | Records its slot index; multiplies render by `layer_->level()`. (Pool/rebinding already supported by v3.) |
| `params/Parameters` | Per-layer param generation in a loop over `kNumLayers`; new routing params. |
| `PluginProcessor` | Per-layer snapshot + routing update loop; v3→v4 prefix-rewrite migration; `v=4`. |
| `PluginEditor` | "Edit layer" combo (0/1) re-points attachments; compact per-layer routing strip. Minimal by design. |

If `VoiceManager` grows unwieldy with allocation logic, factor the slot-matching/allocation into a small helper.

## Testing

- **Routing:** split (note in only one layer's key range fires that layer), overlap (fires both), velocity out of range doesn't fire, channel filter (Omni vs specific) routes correctly.
- **Shared pool:** note → one voice per matching layer; allocation past 64 active steals oldest; note-off releases the correct voice(s) (incl. channel-aware).
- **Per-layer level:** level scales a layer's contribution; very low level ≈ silences it; relative A:B balance follows the two levels.
- **Migration + behavior preservation:** a v3-format preset prefix-rewrites to `layer0.*` with `layer1` disabled and renders identically to v3; single-enabled-`layer0` output equals v3. Extends the cumulative migration test (v1→v2→v3→v4).

## Deferred work (not v4)

- **Voice modes** — Poly2 (same-note retrigger) and Mono/Mono2/legato (Summit findings); v4 is Poly-only.
- **Note-aware / quietest-voice stealing** — keep oldest-active.
- **Velocity crossfade** — hard velocity range only in v4.
- **Summit-style mode selector** (Single/Layer/Split/Dual + split point) — v7, with the flagship Summit preset.
- **Layer count > 2** — architecture is generic over `kNumLayers`; raising it (toward the K2000's 32) is a later bump, with its own param-surface decision.
- **Per-layer FX** — v8. **Photoreal / full multi-layer UI** — v9.
