# ADR 0007 — Param namespace and v1 preset shim

**Status:** Accepted, 2026-06-12. Effective from v2.

## Context

v1 param IDs are flat: `osc.coarse`, `slot0.cutoff`, `amp.attack`, `master.gain`. v2 introduces the Layer abstraction; v4 will introduce multi-Layer Programs. If params stay flat through v2, the v4 rename has to touch every ID and every preset breaks.

## Decision

Move all Layer-scoped params under a `layer.*` namespace at v2. Examples:
- `osc.coarse` → `layer.osc.coarse`
- `slot0.cutoff` → `layer.slot0.cutoff`
- `amp.attack` → `layer.amp.attack`
- `master.gain` *stays* `master.gain` (it lives downstream of the Program mix).

At v4 the prefix becomes `layer[0].*`, `layer[1].*` — additive, no further rename.

A v1 → v2 preset migration shim runs in `setStateInformation`. It checks the loaded XML for a `v=2` attribute; if absent, it walks the XML rewriting old IDs to new before APVTS reads it. The shim is table-driven, ~30 lines.

## Consequences

- v1 presets continue to load.
- v2-saved presets carry `v=2` and skip the shim.
- The shim is removed in a future major version (probably v6+), after which v1 presets stop loading. Document this in the v6+ spec.
