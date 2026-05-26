# ADR 0002 — Polymorphic DSP slots over hardcoded chain

**Date:** 2026-05-25
**Status:** Accepted

## Context

VAST architecture means each voice's signal path is a user-configurable graph of DSP blocks. v1 ships only one fixed graph (Oscillator → SVF → Waveshaper → Amp), but the internal data model of that graph is what every future phase grows from. Three shapes were on the table:

## Options considered

- **A. Hardcoded chain.** `Voice` directly contains `SVFFilter filter; Waveshaper shaper;` as named members. Simplest possible v1, but changing the routing later means rewriting the voice class. No foundation for VAST.
- **B. Polymorphic slots.** `Voice` holds `std::array<std::unique_ptr<DSPBlock>, 2>`. Block type per slot is fixed in v1 (slot 0 = filter, slot 1 = shaper), but the `DSPBlock` interface is real from day one. Easy to add new block types as subclasses; easy to let the user pick block per slot later.
- **C. Full graph abstraction.** Voice holds an `Algorithm` object whose topology is itself data — nodes plus edges. Most future-proof, but designing serialization, traversal, and cache-friendly storage before we have multiple algorithms to test against is speculative.

## Decision

Option B — polymorphic slots.

## Why

B commits to the central architectural idea ("DSP blocks are pluggable, parameterized units") without speculatively designing graph machinery that v1 cannot validate.

- A leaves us with no real abstraction and guarantees a rewrite. Rejected.
- C designs the wrong thing first. Without 4-5 algorithms in hand, we'd guess wrong about graph storage and traversal patterns and spend v1 carrying a half-right abstraction.
- B is the smallest abstraction that's *actually used* by v1: the `DSPBlock` interface is exercised by two concrete blocks, parameter namespacing is real, preset state already records block type per slot. Everything works in v1, and the path to v4 (user-selectable blocks) is "add a registry" rather than "redesign the voice."

## Consequences

- `Voice` holds `std::array<std::unique_ptr<DSPBlock>, 2>`. Slot index is implicit; the block doesn't know which slot it's in.
- Block parameters are namespaced by slot (`slot0.cutoff`, `slot1.drive`) at registration in `AudioProcessorValueTreeState`.
- Preset state saves block type per slot (`{ slot: 0, type: "svf_filter" }`) from v1, so v1 presets stay loadable in v4 and beyond.
- The `DSPBlock` interface gets its own architecture doc — see [`../architecture/dsp-block-interface.md`](../architecture/dsp-block-interface.md).
- The full graph abstraction (Option C) becomes a v4+ concern, designed against the real patterns 4-5 algorithms reveal.
