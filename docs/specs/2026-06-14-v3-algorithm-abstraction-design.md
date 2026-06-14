# v3 — Algorithm abstraction

**Status:** Design proposed, 2026-06-14.

**Scope:** Turn the fixed slot chain into a *selectable algorithm*. An algorithm is a routing of DSP blocks; v3 ships a small library of them, all built from the existing `{filter, shaper}` palette, plus the selection mechanism, a per-block-type parameter namespace, and the taxonomy documentation. No new block types (v5), no non-linear routing (v4+), no modulation/smoothing (v6).

## Goal

After v3:

- A `Layer` owns a **palette** — one instance of each available block type — and a **selected algorithm** that names an ordered walk through that palette.
- The user selects the algorithm via a `layer.algorithm` parameter; the choice is audible (e.g. distortion before vs. after the filter).
- Parameters are keyed by block **type** (`layer.filter.*`, `layer.shaper.*`), stable across algorithm selection. v1/v2 presets still load and sound identical.
- The algorithm/block taxonomy is documented and grounded in the actual K2000 model (via the [k2000-kb](https://github.com/increp/k2000) reference KB), including where v3 deliberately simplifies.

## Background: the K2000 model (sourced)

From the K2000 Series Musician's Guide (pp. 47–48, 253), via the reference KB:

- An **algorithm** is a preset "wiring" (signal path) of a sample to the outputs through a series of DSP functions. There are **31 algorithms**; the path of each is fixed — you *select* an algorithm, you don't rewire it.
- Each algorithm has DSP-function **blocks** at fixed positions (`F1`–`F4`). **The first function always controls pitch; the last always controls final amplitude.** The configurable DSP sits between.
- A single algorithm may **split into two wires** (parallel branches) that rejoin or stay split ("double-output" algorithms).
- A single algorithm **can repeat a function category** — *"one or more blocks…can have filter functions assigned to them"* — so two filters in one algorithm is a real case.
- The DSP function categories: FILTERS, EQ, PITCH/AMP/PAN, MIXERS, WAVEFORMS, ADDED WAVEFORMS, NON-LINEAR FUNCTIONS, WAVEFORMS WITH NON-LINEAR INPUTS, MIXERS WITH NON-LINEAR INPUTS, HARD SYNC.

**How v3 maps and where it simplifies:**

| K2000 | v3 |
|---|---|
| osc/pitch first, amp last, DSP between | identical: `osc → [algorithm blocks] → amp` |
| 31 fixed-wiring algorithms | a 4-entry library, linear wiring only |
| `F1`–`F4` positional blocks; a category may repeat | **block-type** addressing; **one instance per type** (no duplicates yet) |
| split/parallel wires | linear (single wire) only — graph routing deferred to v4+ |
| ~10 function categories | 2 block types (SVF filter, waveshaper) |

The positional/per-F-block model (which K2000-faithful duplicate-block algorithms require) is deferred to **v7**; see Deferred work.

## Architecture

```
Layer
 ├─ palette (one instance per block type, owns shared config):
 │     filter : SVFFilter   ← layer.filter.*
 │     shaper : Waveshaper  ← layer.shaper.*
 ├─ algorithm = AlgorithmLibrary[ snapshot.algorithmId ]   ← layer.algorithm
 └─ snapshot (ParamSnapshot, now includes algorithmId)

Voice (per playing note)
 ├─ per-block-type VoiceState  { filter:{ic1,ic2}, shaper:{} }
 └─ render(): osc → walk algorithm's ordered block-type list,
                    processing through palette[type] with this voice's state
                  → amp
```

The Voice walks the *selected algorithm's ordered list of block types*; for each entry it processes the audio through the corresponding palette block, passing its own per-voice state. Switching algorithms changes only which ordered list is walked — **no reallocation, no parameter churn**; the palette and APVTS layout are fixed.

This is the model ADR 0006 ("algorithm as passive data") anticipated. The existing `Algorithm` struct (`slotCount` + `blockTypePerSlot[]`) already expresses an ordered block-type list; v3 keeps it and adds identity/metadata for the library. (ADR 0006 names a `render_order` field that the implemented struct never had — order is implicit in the array index. ADR 0008 reconciles this: order *is* the array order.)

## The v3 algorithm library

A static, **append-only** array of records `{ stable id, display name, ordered block-type list }`:

| id | display | walk | exercises |
|---|---|---|---|
| `filter_then_shaper` | "Filter → Shaper" | `[filter, shaper]` | **default**; = v2 behaviour |
| `shaper_then_filter` | "Shaper → Filter" | `[shaper, filter]` | ordering (audibly distinct) |
| `filter_only` | "Filter only" | `[filter]` | block absence |
| `thru` | "Thru" | `[]` | empty walk (osc → amp) |

Append-only ordering (same convention as the `BlockTypeId` enum) keeps the `layer.algorithm` choice-index preset-stable.

## Parameter model and migration

New namespace, keyed by block type (full words, matching existing style):

| v3 ID | v2 ID |
|---|---|
| `layer.filter.type` | `layer.slot0.type` |
| `layer.filter.cutoff` | `layer.slot0.cutoff` |
| `layer.filter.resonance` | `layer.slot0.resonance` |
| `layer.shaper.drive` | `layer.slot1.drive` |
| `layer.shaper.mix` | `layer.slot1.mix` |
| `layer.algorithm` | *(new — `AudioParameterChoice`)* |
| `master.gain` | *(unchanged)* |

- **Migration** extends the existing shim. The state schema attribute bumps to `v=3`; `setStateInformation` applies rename tables **cumulatively**: if `v<2`, run the v1→v2 table; then if `v<3`, run the v2→v3 table. A v1 preset migrates v1→v2→v3 in a single load.
- **`layer.algorithm` defaults to `filter_then_shaper`**, which is the v2 signal path — so every v1/v2 preset loads sounding byte-identical; the new selector simply rests at its default.
- **`ParamSnapshot`** gains `int algorithmId`. The larger "split ParamSnapshot into sub-structs" refactor (floated in v2 known-concerns) is **not** done here — it's orthogonal churn; re-noted as deferred.

## Selection mechanism and RT behaviour

- `layer.algorithm` is an `AudioParameterChoice` whose options are generated from the library (never hand-duplicated). Host-automatable for the eventual UI.
- `PluginProcessor` reads it into `snapshot.algorithmId`. `Layer::updateParameters` configures the palette blocks from their namespaces **and** sets the active algorithm = `library[algorithmId]`.
- **Switching mid-note** re-routes the signal path on the next block and can click (filter state / level jump). v3 **accepts and documents** this — same family as the v2-known "stealing causes a click." Crossfade/smoothing stays deferred to v6.

## Module changes

| Module | Change |
|---|---|
| `dsp/Algorithm` | Gains a stable id + display name; add `AlgorithmLibrary` (append-only static array) and lookup by id/index. Order is the array order (reconcile ADR 0006). |
| `Layer` | Owns a palette (one block instance per type) instead of positional `slots_`; configures each from its namespace; selects active algorithm from snapshot. |
| `Voice` | Holds per-block-**type** `VoiceState`; walks the active algorithm's block-type list. |
| `params/Parameters` | New `layer.filter.*` / `layer.shaper.*` IDs + `layer.algorithm` choice (options from the library). |
| `params/ParamSnapshot` | Add `algorithmId`. |
| `PluginProcessor` | Snapshot reads `algorithmId`; migration shim extended to `v=3` with the v2→v3 table. |
| `PluginEditor` | One combo bound to `layer.algorithm`. No per-algorithm UI reflow. |

If `Layer` grows past ~250 lines accumulating palette/selection helpers, factor the palette into a small helper type.

## Deliverables

- **ADR 0008 — Algorithm selection: palette + ordered-algorithm model, semantic per-block-type parameter namespace, append-only library.** Records the one-instance-per-type constraint and the v2→v3 migration; reconciles ADR 0006's `render_order`. (Split the migration into ADR 0009 only if 0008 grows unwieldy.)
- **`docs/architecture/algorithm-taxonomy.md`** — the palette/algorithm/library taxonomy, the K2000 mapping above (sourced), linear-now / graph-at-v4+ routing, and the resolved duplicate-block finding pointing at the v7 positional model.
- **Minimal editor combo** for `layer.algorithm`.

## Testing

- **Library well-formedness:** ids unique; every block type in every algorithm exists in the palette; no duplicate block type within an algorithm (enforce the v3 constraint).
- **Routing correctness:** `filter_then_shaper` vs `shaper_then_filter` produce **different** output for identical input (ordering proven); under `filter_only`, `layer.shaper.drive` has no effect on output; under `thru`, `layer.filter.cutoff` has no effect.
- **Selection:** changing `layer.algorithm` changes the active walk a Voice renders.
- **Migration + behaviour preservation:** a v2-format preset (`layer.slot0.*` / `layer.slot1.*`) loads onto `layer.filter.*` / `layer.shaper.*` with `algorithm = filter_then_shaper`, and the default-algorithm render matches v2 output. Extends the existing PresetMigration test (and keeps the v1→v2→v3 cumulative path covered).

## Deferred work (not v3)

- **Positional / per-F-block parameter model** for K2000-faithful duplicate-block algorithms — required by **v7**; budget a v3→v7 param migration. (Resolved open question; see roadmap.)
- **Non-linear routing** (split/parallel wires) — v4+ graph encoding.
- **Parameter smoothing / algorithm-switch crossfade** — v6.
- **ParamSnapshot sub-struct split** — deferred; revisit when oscillator/mod fields land (v5/v6).
- **New block types** (more filters, EQ, mixers, hard sync, etc.) — v5/v7.
