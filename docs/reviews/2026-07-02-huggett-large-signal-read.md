# SP-B Phase 0 — The Huggett Large-Signal Read

**Version:** 5.15 (artifact) · **Date:** 2026-07-02
**Method:** `tests/LargeSignalTests.cpp` (`BERNIE_RUN_LARGESIGNAL=1`) — dense peak-find, then `testdsp::LevelResponse` level sweep (−100 → −6 dBFS, 2 dB steps) **at each filter's own resonant peak**. LP24, fc 1000 Hz, drive 0, os1, 96 kHz. Analysis-only per the standing authenticity-purist hold.

## The headline: Huggett's resonance nonlinearity is EXPANSIVE

The working theory ("the +89 dB peak is a small-signal artifact; the resonance
saturator self-limits at real levels") is **falsified**. Measured, res 0.9:

| input dBFS | Huggett gain dB | Huggett out peak dBFS | Moog gain dB | Moog out peak dBFS |
|---|---|---|---|---|
| −100 | 61.1 | −35.9 | 5.8 | −94.2 |
| −52 | 69.1 | +24.5 | 5.8 | −46.2 |
| −20 | 81.6 | +71.9 | 3.3 | −16.7 |
| −6 | **85.6** | **+89.2** | **−3.3** | **−9.3** |

- **Huggett gain RISES ~25 dB with input level** (61 → 86 dB). The
  "self-limiting" resonance saturator anti-limits: more input, more gain,
  toward a +89 dBFS output scream (linear amplitude ~28,000 — the SP-A
  "×28000" figure is not a linearization artifact; it is the *actual output*
  at musical input). THD+N at the peak sits between −2 and **+5 dB** —
  at −20 dBFS input there is more non-fundamental energy than fundamental.
- **Moog behaves like analog**: constant small-signal gain, then textbook
  compression (1 dB knee at −26 dBFS input, gain −3.3 dB by −6 dBFS; at
  res 1.0 it compresses ~90 dB of small-signal gain away). Output peak stays
  under −9 dBFS. Clean THD floor (−42 dB at the hottest point).
- Summary across resonance (gain small-signal → at −6 dBFS input):
  res 0.7: Huggett 54 → 69 · res 0.9: 61 → 86 · res 1.0: 64 → 96 dB.
  Moog: −3.7 → −4.9 · 5.8 → −3.3 · 88.5 → −1.5 dB.
- **Output-level disparity at musical input: ~98 dB** (Huggett +89 dBFS vs
  Moog −9 dBFS at the same −6 dBFS excitation).

## Why this is bigger than "no internal ceiling"

The L3 analysis explained the disparity as *unbounded linear gain* (two
cascaded Q≈41 cells, no ceiling) vs Moog's bounded loop. The large-signal read
shows the nonlinearity **actively increases** effective resonance with
amplitude — plausibly the asymmetric resonance-loop shaper (`satRes`, the
unit-slope-normalized asymmetric Padé tanh acting through the `fbExtra` delta)
rectifying the feedback and shifting the cell's operating point so effective
damping *falls* as level rises. Mechanism attribution needs a focused look at
`NlSvfCell::step()`; what is certain from measurement: **no physical OTA
filter expands** — every published VA/analog reference (and our own Moog)
compresses. This is a sign-of-behavior anomaly, not a calibration nuance.

In practice the ear is protected only by the default-ON output SafetyLimiter
(which hard-caps the +89 dBFS scream at the amp) — the spine itself is
producing enormously hot, heavily distorted signal at any musical level with
res ≥ 0.7.

## Register + decision needed (Q27)

Registered as **Q27**: is the expansive resonance loop a *defect* (fixable now
despite the voicing hold — no hardware answer can make expansion authentic) or
*character* to preserve until the Summit fingerprint rules? The hold was
designed for calibration questions; this finding is categorical. User decision
requested; analysis stops here per the standing rule.

## Follow-ups (SP-B proper)

- Drive axis (this read was drive 0 — the pre/post shapers add another level
  dimension).
- Mechanism dissection of `NlSvfCell::step()` at high amplitude (state
  trajectories, effective k vs level) — cheap, in-box, still analysis.
- Same read on the real Summit when SP-D lands (the decisive comparison).
