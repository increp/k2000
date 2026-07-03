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

## Addendum — dropout forensics (same day)

Q27 was RULED a defect the same day on a user field report: *"at max resonance
the sound disappears completely and then re-appears"*, with the default-ON
safety limiter credited with preventing hearing damage. Forensic reproduction
(`BERNIE_RUN_DROPOUT=1`, 20 s runs): **no NaN/Inf and no silent windows** at
either the bare-filter level (res 1.0, −6 dBFS sine) or the full Voice path
(saw, res 1.0, os8) — but the voice path sustains **~+22 dBFS output**
(maxAbs ≈ 13) continuously. Conclusion: the perceived dropout is most likely
the **safety limiter slamming ~25 dB+ of gain reduction** against the
screaming filter (audible as disappearance) and releasing (reappearance) —
possibly compounded, before 2026-07-02, by the since-fixed re-prepare
use-after-free (audit P0), which fired on OS changes. Either way the source is
this defect; fixing the expansive loop removes the trigger.

## Addendum 2 — THE FIX (same day, fix/huggett-bounded-resonance)

Root cause confirmed by reading `NlSvfCell::step()`: the satRes delta had its
operands transposed — `v0 -= k·s·(satRes(bp) − bp)` is POSITIVE band-pass
feedback growing with amplitude (anti-damping). Fix: (1) operand swap, so the
delta is extra damping that grows as the loop saturates; (2) OTA-style soft
state rails at ±4 (the absolute bound real integrators have). Mirrored into
`NlSvf.cmajor`/`NlSvfDrive.cmajor` and regenerated (bit-exact equivalence
kept). Post-fix, res 0.9: gain 61 dB small-signal → **+8 dB at −6 dBFS in**
(was +86); voice path at res 1.0/os8 peaks at **−1.5 dBFS** (was +22);
1 dB knee at −83 dBFS (analog-early); small-signal response and Moog
byte-identical. Both `// CALIB` dials (rail level, asymmetry) remain SP-D
calibration targets — the *law* is now the right shape; the *numbers* await
the real Summit.

## Addendum 3 — UAT iteration: the whistle and the knob (2026-07-03)

User audition of the first fix found two regressions the metrics missed:
**(a) no self-oscillation at max resonance** — the old "whistle" had been powered
by the anti-damping defect itself (linear Q caps at ~50, always decaying), so
removing the defect removed the whistle; **(b) a click + sudden character change
at the first resonance increment** — the rails engaged binarily at full
strength. Fix iteration: the top 5 % of the resonance range is now genuinely
**regenerative** (damping fades through zero to −0.012 at max — how analog
actually crosses the oscillation threshold), with the state rails setting the
whistle amplitude; the rails (and the saturation delta via |k|) blend in
**proportionally with resonance**, so engagement is continuous in the knob.
Regression tests added: sustained-whistle (kick → amplitude at 3 s within 50 %
of 0.5 s, rail-limited) and a twin-run click-burst guard. Both `.cmajor`
sources mirrored + regenerated, equivalence bit-exact; goldens churned
surgically once more (Huggett-active rows only; Moog + init_saw byte-identical).
`kOscStart`/`kOscDepth` join the `// CALIB` dials for SP-D.

## Follow-ups (SP-B proper)

- Drive axis (this read was drive 0 — the pre/post shapers add another level
  dimension).
- Mechanism dissection of `NlSvfCell::step()` at high amplitude (state
  trajectories, effective k vs level) — cheap, in-box, still analysis.
- Same read on the real Summit when SP-D lands (the decisive comparison).
