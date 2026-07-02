# Nonlinear Filter Modeling — making the SVF a Huggett (gray-box)

> **Version:** 5.01 · **Status:** architecture reference, written 2026-06-17.
> **Purpose:** how the Huggett character is built *on top of* the linear
> [TPT/ZDF core](tpt-svf-core.md) — the nonlinear stages of register **Q15**, grounded in
> measured data. This is the load-bearing implementation reference for **v5 Plan 2** (nonlinear
> Huggett + HP pre-filter). Aliasing of these stages → [antialiasing-adaa.md](antialiasing-adaa.md).
> **Primary source** (saved in `docs/incoming_research/`, see
> `INDEX` (gitignored local archive `docs/incoming_research/`)): Köper, Holters, Esqueda,
> Parker, *A Virtual Analog Model of the EDP Wasp VCF*, DAFx-22 — the only paper with **measured**
> OTA/diode curves from a Huggett-lineage filter. Companion to
> [huggett-filter.md](huggett-filter.md) / [huggett-filter-dossier.md](huggett-filter-dossier.md).

## The three nonlinear sites (Q15)

Per the documented Peak/Summit signal path, distortion sits **before**, **inside**, and **after**
the filter. We model three asymmetric tanh-class stages:

1. **Pre-filter input drive** — asymmetric shaper on the cell input (the common `spine.drive`; the
   HP pre-stage has its own).
2. **Resonance-loop saturator** — inside the feedback path; makes self-oscillation self-limit.
3. **Post-filter drive** — asymmetric shaper after the cell (`spine.huggett.postDrive`).

Asymmetry is load-bearing: Huggett-family filters are "dirty/edgy," not symmetric/warm.

## Why tanh is principled, not a guess (measured)

The DAFx-22 Wasp paper **measured** the CA3080 OTA and fit its transconductance to a hyperbolic
tangent:

```
i_OTA = α · i_bias · tanh( β · v_OTA / (2·V_TH) ),   V_TH ≈ 25 mV
        α = 0.8635,  β = 0.9408        (optimised against the real chip)
```

The LM13700 / NJM13600 used across the OSCar → Bass Station II → Peak/Summit lineage share this
differential-pair tanh law. So our gray-box tanh has a measured basis; **α, β (and our drive
biases) are calibration constants** — start from these and re-pin against the user's own Summit.

**One caveat that the Wasp paper makes explicit (and that we must respect):** the Wasp gets extra
dirt from **CMOS inverters (CD4069)** used as its integrator/summing amps. The OSCar/BS2/Peak/
Summit lineage uses op-amps / OTA buffers there, **not** CMOS inverters. → **Transfer the OTA tanh;
do NOT port the CMOS-inverter model.** The Summit is the *clean* end of the family; the Wasp is the
extreme. (Confirms Q15's "gray-box now, white-box later.")

## Resonance-loop saturator — the "spit"

In the real Wasp the resonance feedback is limited by **antiparallel diodes** (1N4148, D1/D2;
Shockley law, `Is = 2.52 nA`, `η = 1.752`), and the paper shows the output becomes **asymmetric at
high resonance** because of those diodes. That is the hardware basis for our resonance saturator.

**Implementation (the `fbExtra` delta technique).** The linear Cytomic cell pre-solves the feedback
algebraically, so we cannot drop a `tanh` literally inside it without losing the closed form.
Instead, inject only the *nonlinear correction* computed from the previous bandpass output:

```
fbExtra = k · ( φ(bp_prev) − bp_prev )     // φ = asymmetric saturator; 0 when small
v0eff   = v0 − fbExtra                      // feed correction into the cell input
// ... then the existing linear a1/a2/a3 solve with v0eff ...
bp_prev = v1                                // store for next sample
```

Why this is the right trade:
- **Preserves the closed-form solve** — no per-sample Newton iteration (~1 tanh/sample extra).
- **Bit-for-bit linear at low level** — when the signal is small `φ(x) ≈ x`, so `fbExtra ≈ 0` and
  the filter *is* the validated linear core. Existing `SpineFilterTests` stay green.
- **Self-limits as a genuine limit cycle** — effective loop gain `k·φ'(A)` falls as amplitude `A`
  grows, so self-oscillation settles at a musical level instead of diverging. The one-sample-delayed
  feedback is what gives the aggressive "spit" (the 24 dB LP "acidic cobra").

The saturator is **not** antialiased (kept at base rate): its products land near the resonant peak
and are largely masked, and ADAA-in-a-feedback-loop needs a per-sample solve (see the ADAA doc).

## Drive shapers (pre / post)

- **Form:** asymmetric `tanh(g·x + bias)` (bias gives even harmonics → "dirty"). `bias` is a fixed
  per-stage voicing constant, not a user knob. The HP pre-stage uses *lighter* bias than the main.
- **Level compensation (tone, not loudness):** a high-drive tanh both adds harmonics and compresses
  RMS, so naively cranking drive gets quieter/duller. Apply partial RMS compensation (~70–80%) so
  the knob still feels like it pushes but A/B calibration compares *tone*.
- **DC blocker:** asymmetric shapers inject signal-dependent DC. One one-pole DC blocker per channel
  at the **voice output** (after post-drive): `y = x − x₁ + R·y₁`, `R ≈ 1 − 2π·fc/fs`, `fc ≈ 5–10 Hz`.
  Keep `fc` low so it doesn't thin bass (the Huggett is not supposed to thin bass like a ladder).

## ZDF solve strategy (cost-bounded)

| Strategy | Cost | Use |
|---|---|---|
| Full Newton (iterate to convergence) | ~15–40 transcendentals/sample | **Reject** — ~10–25× linear cost |
| Single Newton step (linearize once/sample) | ~2 tanh/sample | HQ tier only |
| **Instantaneously-linearized ZDF** | ~0 extra in hot loop | **Default** |

Default = keep the linear Cytomic cell and add a cheap **per-block** `g_eff` droop that captures the
OTA "darkens when loud" tell without a per-sample transcendental:

```
drive_norm = slow_envelope(|cell input|) · inv_headroom    // one-pole, ~5 ms
g_eff      = g_nominal / (1 + 0.20 · drive_norm²)           // recompute a1/a2/a3 from g_eff
```

The `0.20` is subtle and is a Summit-calibration constant. The dominant character comes from the
resonance saturator and the drive shapers, not this droop.

## Fast tanh

- **Resonance saturator:** a **monotonic, bounded** Padé approximation (e.g. 7/6) — accuracy near
  zero matters for self-oscillation pitch stability; a non-monotonic approx can create spurious
  limit cycles.
- **Drive shapers:** keep true `tanh` + its antiderivative `log(cosh)` because ADAA needs the
  antiderivative (see the ADAA doc). The `log` cost is acceptable since ADAA already costs a divide.

## Performance notes (256 voices × stereo)
- **Branch-free hot loop** templated on `<Mode, Slope, DriveOn>`; the `DriveOn = false` path is the
  pure linear filter (zero nonlinear overhead when drive is off).
- **Stereo-as-2-lanes** SIMD (L/R share all coefficients); voices-as-lanes only if the perf gate
  (Q11) demands.
- Denormals: `ScopedNoDenormals` + an explicit flush on the longest-decaying state (resonance loop).

## White-box future / hardware grounding (not Plan 2)
For a future component-accurate model: OSCar = LM13600 OTAs, two 12 dB filters in series, ±5 V
(schematic saved at `docs/incoming_research/oscar-filter-schematic-airburst.jpg`); Bass Station II
"Classic" = two −12 dB LM13700 stages, HC4051 mode switch, per-OTA resonance CV (closest *circuit*
reference to Peak/Summit). Peak/Summit filter IC is inferred (LM13700), not teardown-verified.

## Calibration constants to pin against the Summit
`α, β` (OTA tanh), pre/post drive `bias` values, the `g_eff` droop coefficient, the resonance-Q
taper, and the HP-stage's lighter biases. Method (dossier Stage 3): self-osc pitch-vs-note,
resonance sweeps at 12 vs 24 dB, drive level sweeps (even/odd harmonic ratio), separation sweeps.
