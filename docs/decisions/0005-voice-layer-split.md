# ADR 0005 — Voice/Layer split

**Status:** Accepted, 2026-06-12. Effective from v2.

## Context

v1 conflated two K2000 concepts in a single `Voice` class:
- A *Layer* is a configuration: an algorithm + DSP blocks + envelope + ParamSnapshot. It does not by itself play notes.
- A *Voice* is a runtime instance playing a particular note through a Layer. It holds note state and per-block integrator state.

In v1, `Voice` owned the block instances, which works for one voice but breaks the conceptual model: when v4 multi-Layer Programs arrive, two Voices playing the same Layer should share that Layer's config but each have their own integrators.

## Decision

Split `Voice` into:
- `Layer` — owns the algorithm topology, DSP block instances, ParamSnapshot, and envelope config. Stateless w.r.t. per-note rendering.
- `Voice` — owns note state, ADSR envelope position, and a small per-block `VoiceState` struct for each stateful block in its Layer's algorithm.

A `Voice` holds a non-owning pointer to its `Layer`. With one Layer at v2, all Voices point at the same one.

## Consequences

- v4 multi-Layer work is a clean add-on: Voices already know which Layer they play.
- Block-state-per-voice extraction (Task 3 of v2 plan) is required: SVFFilter's integrators must move out of the block instance.
- v1's preset format is forward-compatible because slot type IDs were already saved; only param IDs change (handled by the v1-preset shim, [ADR 0007](0007-param-namespace-and-v1-preset-shim.md)).
