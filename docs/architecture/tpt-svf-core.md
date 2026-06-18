# The TPT / ZDF State-Variable Core (linear foundation)

> **Version:** 5.01 · **Status:** architecture reference, written 2026-06-17.
> **Purpose:** documents the *linear* state-variable filter core that every spine `FilterModel`
> is built on — what `src/dsp/spine/TptSvfCell.h` implements and why. The nonlinear Huggett
> character is layered on top of this (see [nonlinear-filter-modeling.md](nonlinear-filter-modeling.md));
> aliasing of those nonlinearities is handled per [antialiasing-adaa.md](antialiasing-adaa.md).
> **Sources** (saved in `docs/incoming_research/`): Simper/Cytomic *SvfLinearTrapOptimised2*
> and *SvfInputMixing*; Zavalishin *The Art of VA Filter Design* (rev 2.1.2). Anchors the
> [v5 spec](../specs/2026-06-16-v5-constant-summit-voice-design.md) and register **Q13** /
> [engine-questions.md](engine-questions.md).

## Why TPT, not Chamberlin or a biquad

A topology-preserving transform (TPT), a.k.a. zero-delay-feedback (ZDF), state-variable filter
discretizes the *analog* SVF with trapezoidal integration. We use it because:

- **Modulation-safe.** Cutoff and resonance can be swept at audio rate without the zipper/blow-up
  artifacts of direct-form biquads or the classic Chamberlin SVF (whose tuning degrades near
  Nyquist and at high resonance).
- **Simultaneous LP/BP/HP/Notch outputs** from one cell — the mode switch just selects a tap.
  This is the natural shape for the Huggett's multimode and dual-routing behaviour.
- **It is the analog SVF.** The structure preserves the analog topology, so analog intuition
  (and later, analog nonlinearities placed at the right nodes) maps directly.

## The cell (matches `TptSvfCell`)

Per sample, with cutoff `fc`, sample rate `fs`, resonance-derived `Q`:

```
g  = tan(π · fc / fs)          // prewarped integrator gain
k  = 1 / Q                     // damping (resonance) term
a1 = 1 / (1 + g·(g + k))
a2 = g · a1
a3 = g · a2
```

State update (per channel, `ic1eq`/`ic2eq` are the two integrator memories):

```
v3 = v0 − ic2eq
v1 = a1·ic1eq + a2·v3
v2 = ic2eq + a2·ic1eq + a3·v3
ic1eq = 2·v1 − ic1eq
ic2eq = 2·v2 − ic2eq
```

Taps: `LP = v2`, `BP = v1`, `HP = v0 − k·v1 − v2`, `Notch = v0 − k·v1`. This is Andrew Simper's
*SvfLinearTrapOptimised* form (Cytomic), identical to the original `src/dsp/blocks/SVFFilter.cpp`
math; it is the canonical low-noise, low-op-count linear SVF. **No change to the linear core is
needed for v5** — verified against the fetched paper.

### Calibration & guards already in the code
- `Q = 0.5 + res² · 8.5` maps the 0..1 resonance control to a musical Q range. For
  self-oscillation (nonlinear Huggett) this range is opened up much further — but only *with* the
  resonance-loop saturator in place (see the nonlinear doc); never push `k → 0` on the bare
  linear cell, it diverges.
- Cutoff is clamped to `[16 Hz, 0.45·fs]`. The prewarp `g = tan(...)` blows up as `fc → fs/2`;
  the clamp is the guard.
- Coefficients are recomputed only on a cutoff/resonance change (`dirty_` flag), never per sample.

## Dual cell, slope, and separation

The Huggett model holds **two** cells (A, B):
- **12 dB** = cell A only.
- **24 dB** = A → B in series.
- **Separation** offsets cell B's cutoff by `cutB = cutoff · 2^separationOct`, the OSCar/Summit
  "separation" concept (the basis for the future nine dual routings — register **Q14**).

## Input-mixing variant (future)

Cytomic's *SvfInputMixing* form computes an output as a weighted mix of the HP/BP/LP responses
with a single set of coefficients. It is the cleaner basis for the **nine Summit dual routings**
(series and parallel LP/BP/HP combinations) when we implement them; noted here so the dual-routing
work starts from the right structure rather than re-deriving it. Out of scope for Plan 2.

## Per-voice state

Only the integrator memories (`ic1eq`, `ic2eq`, ×2 for stereo, ×2 cells) are per-voice. **All
coefficients (`g/k/a1/a2/a3`) are shared across voices** — they live on the `FilterModel`, set via
`setCommon` + the model bank, not per voice. This keeps the per-voice cache footprint tiny, which
matters at the 256-voice × stereo target (register Q2/Q11).
