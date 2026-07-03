# Anti-aliasing: Antiderivative Antialiasing (ADAA) + the HQ oversampling tiers

> **Version:** 5.01 · **Status:** architecture reference, written 2026-06-17.
> **Purpose:** how the spine de-aliases its nonlinear stages within the heap-free, no-per-voice-
> `juce::dsp::Oversampling` constraint (register **Q12**). Pairs with
> [nonlinear-filter-modeling.md](nonlinear-filter-modeling.md). **Sources** (saved in
> `docs/incoming_research/`): Parker, Zavalishin, Le Bivic, *Reducing the Aliasing of Nonlinear
> Waveshaping…*, DAFx-16; Holters, *Antiderivative Antialiasing for Stateful Systems*, DAFx-19.

## The problem and the constraint

The linear filter does not alias — only the **nonlinear** stages (drive shapers, resonance
saturator) generate harmonics above Nyquist that fold back as inharmonic "digital" aliasing.

The classic fix — wrap the path in `juce::dsp::Oversampling` — is **forbidden per voice** (Q12 /
L7): that object is heap-based, runs the *whole* filter at 2–8×, and can't be cheaply duplicated for
the Plan-3 crossfade. So the default de-aliasing is **ADAA**, and oversampling is offered only as
**hand-rolled, optional HQ tiers**.

## 1st-order ADAA (the default, on the drive shapers)

Replace the instantaneous `y[n] = f(x[n])` with the average of `f` over the segment between samples:

```
        F0(x[n]) − F0(x[n−1])
y[n] = ───────────────────────          F0' = f,  choose F0(0) = 0
            x[n] − x[n−1]
```

For our `tanh` shaper, `F0(x) = log(cosh(x))`, so:

```
y[n] = ( logcosh(x[n]) − logcosh(x[n−1]) ) / ( x[n] − x[n−1] )
logcosh(z) = |z| + log1p(exp(−2|z|)) − log(2)        // numerically stable
```

**Ill-conditioned case** (`x[n] ≈ x[n−1]` → divide blows up): fall back to evaluating `f` at the
**segment midpoint** `f((x[n] + x[n−1]) / 2)` — the analytic limit of the formula.

**Delay:** exactly **½ sample** (1st order). Fixed, inaudible, consistent across voices — no host
latency reporting needed (unlike the `Oversampling` object). Flush the `x[n−1]`/`F0` memory on
`reset()` to avoid denormal drift on silence.

## 2nd-order ADAA — HQ only

2nd order (linear/triangular kernel) needs the second antiderivative `F1 = ∫ x·f(x)`; for `tanh`
this contains the **dilogarithm Li₂** — expensive, recommended to tabulate. It also worsens the
ill-conditioning. Keep it as a documented HQ option, **not** the default.

## How good is ADAA? (measured, DAFx-16 Table 1)

For a *hard clipper* at input gain 10, matching a given alias floor needs ≈ **12×** oversampling
with no kernel, **4×** with 1st-order ADAA, **3×** with 2nd-order. So 1st-order ADAA ≈ a **3×
reduction in required oversampling** for a hard clipper — and **more** for our *soft* tanh shapers.
Takeaway: ADAA alone is excellent for soft drive; for very hard drive, **pair ADAA with
oversampling** — which is exactly what the HQ tiers do.

## ADAA inside the feedback loop? (why the resonance saturator stays base-rate)

The resonance saturator is inside a stateful feedback loop. DAFx-19 (Holters) shows ADAA *can* go
there, two ways: (a) **fuse** the antialiased nonlinearity into the following trapezoidal integrator
(removes the half-sample delay, usable in feedback); (b) recast the **whole state-space** at a
reduced rate and antialias the state update too. But both: need **tabulated antiderivatives** for the
implicit nonlinearity, and the reduced-rate substitution **distorts the frequency response at high
Q** — precisely the resonance regime we care about (the paper flags the high-Q all-pass pitfall).
Applying ADAA to the *output only* gives little benefit. → We keep the **resonance saturator at base
rate**; its alias products are masked near the resonant peak, and this avoids a per-sample solve.

## The Bernie policy (resolves Q12)

| Quality tier | Drive-shaper AA | Filter / resonance | Notes |
|---|---|---|---|
| **Light** (default) | 1st-order ADAA | base rate | full 256-voice budget; no oversampling object |
| **Normal** | shapers in 2× region | 2× inline | hand-rolled polyphase half-band |
| **Heavy** | shapers in 8× region | 8× inline | for low-polyphony / leads |
| **Full** | shapers in 32× region | 32× inline | low-poly / render |

- Oversampling is a **hand-rolled, heap-free inline** cascade of 2× half-band stages wrapping only
  the nonlinear region — **never** `juce::dsp::Oversampling`, so it stays compatible with the
  Plan-3 in-place crossfade.
- The quality switch is **global** (not per-Layer/automatable) with **two independent selectors**:
  `quality` (live) and `renderQuality` (offline/non-realtime). Set live = Light and render = Full
  if you want.
- 32× across all 256 voices live is not real-time on normal CPUs (by design choice — "global switch,
  user manages CPU"); it shines on low-voice patches and on render.

## Cross-references
- Nonlinear stages that need this: [nonlinear-filter-modeling.md](nonlinear-filter-modeling.md).
- Linear core (no AA needed): [tpt-svf-core.md](tpt-svf-core.md).
- Constraint origin: [engine-questions.md](engine-questions.md) Q12 / L7;
  [v5 spec](../specs/2026-06-16-v5-constant-summit-voice-design.md).
