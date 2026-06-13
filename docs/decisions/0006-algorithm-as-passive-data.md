# ADR 0006 — Algorithm as passive data

**Status:** Accepted, 2026-06-12. Effective from v2.

## Context

At v3 the plugin will support selecting from a library of algorithms. Each algorithm describes a routing topology (slot order, block type per slot) for the per-voice DSP chain. Two ways to represent this:

A) **Active class.** `Algorithm` is an abstract base with a virtual `render(voice, layer, out, n)` per subclass. Each algorithm in the library is a concrete class.

B) **Passive data.** `Algorithm` is a struct of `{ slot_count, block_type_per_slot, render_order }`. The Voice walks the struct and calls each slot's `process()`. Algorithms differ by data, not by code.

## Decision

Passive data (option B). At v2 there's one algorithm, but the struct shape is in place for v3.

## Consequences

- Adding an algorithm in v3 is "add a record"; no new vtable, no new translation unit.
- Algorithms with radically different routing topologies (parallel branches, feedback) will need extension to the struct (e.g., a small flow-graph encoding) — accepted as v4+ work.
- Voice rendering stays branch-light: walk the array, call each block.
