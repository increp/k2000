# ADR 0004 — Defer photoreal UI to a later phase

**Date:** 2026-05-25
**Status:** Accepted

## Context

The long-term UI vision is photoreal: high-resolution photographs of actual Novation Summit and Kurzweil K2000 hardware composited as a backdrop, with interactive controls (knobs, buttons, combo boxes) overlaid on the photo regions corresponding to the real hardware controls.

The question for v1 is whether to build that UI now or use minimal default JUCE controls.

## Options considered

- **Photoreal UI in v1.** Compose backdrop image, hand-place or JSON-map every control rect, implement transparent JUCE controls with custom `LookAndFeel` to draw on top of the photo, decide whether to use sprite-strip knobs for photographic rotation, work out the trademark/copyright story.
- **Minimal functional JUCE UI in v1, photoreal as a later phase.** Rotary sliders and combo boxes in a simple grid. Bound to parameters via `SliderAttachment`/`ComboBoxAttachment`. Ugly but complete and easy.

## Decision

Minimal functional JUCE UI in v1. Photoreal UI scheduled for a later phase (currently v6 — see [`../roadmap/phases.md`](../roadmap/phases.md)).

## Why

v1 has only ~10 controls. Designing a photoreal panel for that tiny set means either:
- Cropping the hardware photos so tightly that the visual payoff is small, or
- Showing more of the hardware with most controls dead, which is misleading and ages poorly.

Either way, the UI work would compete for time with DSP work — the actual learning goal of v1. A polished UI on a 1-oscillator skeleton is also disproportionate; the project should look about as ambitious as it actually is.

Deferring buys us:
- More time on the DSP and architecture that v1 is really about.
- More controls (after v2/v3 land) to make a photoreal panel worth designing.
- Time to think through the trademark/copyright story before producing a distributable artifact with the hardware photos in it.

## Consequences

- v1 ships with default JUCE controls in a single panel — see the [v1 spec](../specs/2026-05-25-v1-skeleton-design.md#gui-v1).
- The `gui/` source folder exists but is empty in v1; `PluginEditor` does all UI work directly.
- When the photoreal UI phase arrives, the planned approach is: transparent JUCE controls + a JSON layout file mapping `paramId` to pixel rect + a tightly-cropped photo region containing only the controls that exist at that time. Sprite-strip knobs are an open option if transparent overlays don't look good enough.
- The trademark/copyright question (using photos of commercial hardware in a distributable plugin) is parked on the v6 spec. Fine for personal/learning use; needs a real answer before any public distribution.
