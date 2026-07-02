# Hardware-Correlation Acceptance Criterion

**Version:** 5.11 (artifact; distinct from plugin SemVer)
**Date:** 2026-07-02
**Status:** Adopted (SP-A M4) — the hardware half is satisfied in SP-D
**Source:** SP-A design spec §5.3/§5.4/§9
(`docs/superpowers/specs/2026-07-01-device-characterization-core-design.md`)

## The criterion

The characterization framework is **trusted for authenticity judgments** when
BOTH hold, through the calibrated capture chain (loopback + calibration tone,
`testdsp::CaptureCal`):

1. **Ruler proof.** The physical reference standard (a passive filter whose
   response is computable from its component values) is recovered within
   tolerance (≤ ~1 dB; finalized on first capture).
2. **Model correlation.** In-box devices correlate with their hardware
   counterparts (Summit ↔ Huggett, Arturia ↔ Moog) within a stated bound —
   OR a disagreement is attributable to the model, because (1) already
   vouches for the ruler.

**Prerequisites:** the absolute-level axis (shipped: SP-A M2–M4) and a solved
Summit excitation method (the top SP-D risk — no public schematic; how to
drive a known signal through only the Summit's filter).

## Trust ladder (spec §9 — no claim outruns its evidence)

| State | Evidence | Status |
|---|---|---|
| Math-proven | synthetic known-answer net green in `k2000_tests` | **Achieved** (M2–M4) |
| Capture-chain validated | loopback + physical reference recovered | SP-D |
| Hardware-vouched | criterion (1) + (2) satisfied | SP-D |

## Tolerances (spec §5.4)

| Check | Tolerance | Basis |
|---|---|---|
| Dual-method agreement (in-band) | ≤ 0.6 dB | achieved on real models |
| Synthetic corner recovery | ≤ 2 % | log probe grid resolution |
| Synthetic peak-gain / level recovery | ≤ 0.5 dB / ≤ 0.1 dB | lock-in accuracy |
| Synthetic THD recovery | ≤ 1–2 dB | FFT/window bound |
| Self-osc pitch | ± 3 % ≤ 4 kHz, report above | v5 standard |
| Physical-reference recovery | ≤ ~1 dB (finalize on first capture) | real-world chain |

Accuracy (does the ruler measure right?) and authenticity (is the filter true
to hardware?) stay distinct: this criterion makes the ruler trustworthy;
authenticity judgments and any DSP-voicing changes remain SP-D-guided.
