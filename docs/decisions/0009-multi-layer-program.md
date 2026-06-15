# ADR 0009 — Multi-Layer Programs: shared pool, range routing, per-layer namespace

**Status:** Accepted, 2026-06-15. Effective from v4.

## Context

v4 makes a Program hold multiple Layers (delivering Summit's dual-engine). Three decisions: how layers combine, how voices are allocated, and how per-layer parameters are named.

The K2000 (Musician's Guide p. 44) gives each layer a keyboard + velocity range and draws polyphony from a shared pool; Layer/Split fall out of ranges. Summit (User Guide pp. 12–13) exposes explicit Layer/Split/Dual modes and partitions voices 8+8 per part.

## Decision

- **Range-based combination, not modes.** Each layer carries `{ enable, keyLo, keyHi, velLo, velHi, channel, level }`. Layer/Split/Dual emerge from ranges. A Summit-style mode selector is a v7 convenience.
- **Shared voice pool (64), not per-part partition.** On note-on, each enabled layer whose ranges/channel match the note gets a pooled voice. More flexible than Summit's 8+8; a quiet layer doesn't reserve voices.
- **Per-layer namespace `layer0.*` / `layer1.*`** (plain digits, not `layer[0]`). Registration loops over `kNumLayers` (=2 in v4); raising the count later is a loop-bound change. A v3→v4 *prefix rewrite* (`layer.* → layer0.*`) keeps v3 presets loading; `layer1` defaults disabled.

## Consequences

- v4 fully parameterizes 2 layers; the structures generalize toward the K2000's 32 (deferred — that's a param-surface decision for later).
- Voices are rebindable across layers at no RT cost (v3 already split per-voice state from Layer config; all layers share the block palette).
- Deferred: voice modes (Poly2/Mono/legato), velocity crossfade, note-aware stealing, the mode selector (v7), per-layer FX (v8).
