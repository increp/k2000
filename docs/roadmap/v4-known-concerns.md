# v4 known concerns (carried from the v4 build)

> **Point-in-time record (v4 era).** Not groomed since; several items are since resolved. Surviving concerns belong in the living register `docs/architecture/engine-questions.md` — check there before acting on anything below.

Issues identified during v4 that don't block shipping but should be addressed in a later phase. Captured here so they aren't lost.

## Editor has no automated test coverage

The plugin editor (`PluginEditor`) has zero automated tests — the unit-test target builds with `K2000_TESTING=1`, where `createEditor()` returns `nullptr`, so nothing exercises the GUI. v4 shipped **two real editor bugs that only surfaced in manual Ableton smoke**:

1. **Routing-strip knobs didn't render** — the per-layer routing controls (`Key Lo/Hi`, `Vel Lo/Hi`, `Level`) are rotary `LabeledSlider`s laid out in a 12px-tall box, so the knobs drew nothing.
2. **Switching the Edit-Layer combo corrupted the layer you left** — `bindLayer` re-pointed each control's APVTS attachment with `attach = make_unique<…>()`, which constructs the new attachment (syncing the control to the new layer's value) *while the old attachment is still live*, so the old attachment wrote the new layer's value back into the previous layer's param. Fix was to `reset()` the attachment before re-pointing.

**Sharp edge for the future:** any editor that re-points attachments (per-layer pages, per-slot pages, the v7 mode selector) MUST detach the old attachment before creating the new one. Consider a headless editor test (construct the editor with `K2000_TESTING=0` on a message thread, drive `bindLayer`, and assert that each layer's params are preserved across switches), or at minimum a per-release editor smoke checklist.

## Minor: `masterGainDb` read per layer

`params::snapshot(apvts, i)` reads `master.gain` into `masterGainDb` for every layer, but `processBlock` only uses layer 0's. One redundant atomic load per extra layer per block — negligible, but `master.gain` could move out of the per-layer snapshot if `ParamSnapshot` is ever split.

## Deferred to later phases (per the v4 spec)

- **Voice modes** — Poly2 (same-note retrigger), Mono / Mono2 / legato (Summit findings). v4 is Poly-only.
- **Note-aware / quietest-voice stealing** — v4 keeps oldest-active across the 64-pool.
- **Velocity crossfade** — hard velocity range only.
- **Summit-style mode selector** (Single/Layer/Split/Dual + split point) — v7, with the flagship Summit preset.
- **Layer count > 2** — structures are generic over `kNumLayers`; raising it (toward the K2000's 32) is a later param-surface decision.
