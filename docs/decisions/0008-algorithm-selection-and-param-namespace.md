# ADR 0008 — Algorithm selection: palette model + semantic param namespace

**Status:** Accepted, 2026-06-14. Effective from v3.

## Context

v3 makes the DSP chain a selectable algorithm. Two questions follow: how is an algorithm represented at runtime, and what does a parameter belong to once algorithms can reorder blocks?

The K2000 model (Musician's Guide pp. 47–48, 253, via the k2000-kb): an algorithm is a fixed "wiring" of DSP functions assigned to positional blocks `F1`–`F4`, pitch always first and amplitude always last, and a single algorithm **can repeat a function category** (e.g. two filters).

## Decision

- **Palette + ordered algorithm.** A `Layer` owns one instance of each block type (the palette). An `Algorithm` is passive data — an ordered list of block *types*. A `Voice` walks that list, processing through the palette block for each type using its own per-voice state. Algorithms differ by data, not code (extends ADR 0006). Order is the array order; ADR 0006's never-implemented `render_order` field is dropped.
- **Semantic parameter namespace.** Parameters are keyed by block type (`layer.filter.*`, `layer.shaper.*`), stable across algorithm selection. This assumes **one instance of each block type per algorithm**.
- **Append-only `AlgorithmLibrary`** so the `layer.algorithm` choice index stays preset-stable.

## Consequences

- Selecting an algorithm is an index change — no reallocation, no parameter add/remove; APVTS stays fixed.
- The one-instance-per-type assumption is **not** K2000-faithful: K2000 allows duplicate function categories per algorithm. v3's semantic namespace is a deliberate simplification (v3's palette has no duplicates). The positional/per-F-block model with union param registration is required by v7; budget a v3→v7 parameter migration then. See the roadmap "Resolved questions".
- Param IDs change `layer.slot0.* / layer.slot1.* → layer.filter.* / layer.shaper.*`; a cumulative v1→v2→v3 migration shim keeps old presets loading.
