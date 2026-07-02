# Bernie — Fit-for-Purpose & Blind-Spot Review

**Version:** 5.13 (artifact) · **Date:** 2026-07-02
**Reviewer:** Claude Fable 5 (gold-standard engagement, item 3)
**Inputs:** `docs/roadmap/phases.md` (vision), `tools/roadmap-dashboard/roadmap.json` (v1–v25 plan), `docs/architecture/engine-questions.md` (register), the 2026-07-02 codebase audit, SP-A results, shipped code at `ac4d209`.

---

## 1. Verdict

**The program is fit for its stated purpose, and unusually well-run — but the plan
has one hole, one unpriced physics bill, and one missing feedback loop.** The
engineering culture (question register → spec → ADR → TDD → gates → measurement-first
authenticity) is genuinely industry-grade; the audit found architecture most commercial
plugins don't have (zero include cycles, JUCE-free DSP, clean RT path). What follows
focuses entirely on where the *plan* — not the code — can fail.

## 2. The hole: basic modulation is unscoped (highest-priority finding)

Locked decision **L2** defines the constant Summit spine as *"filter + drive → VCA,
**plus the modulation system (amp/mod envelopes, LFOs, mod matrix, voice modes)**,
always present."* The shipped reality is one amp envelope. The dashboard's only
modulation item is **v16 — "Modulation Deepening"** (tentative), and no version
v5–v15 scopes building LFOs, mod envelopes, or the matrix at all. The UI has carried
reserved placeholder panels ("Mod Envs", "LFO 1-4", "Mod Matrix") since v4.5.

Consequences of leaving this as-is:
- The product identity ("you can never reach a dead control") is violated by the
  front panel itself for another ~10 versions.
- A synth with two calibrated filters and **no LFO** loses to a $49 plugin in the
  first 30 seconds of any real player's evaluation — filter authenticity is inaudible
  without motion.
- **Q7** (mod-matrix addressing across changing graphs) is scheduled for v7, but is far
  easier to answer with a *working* basic matrix targeting spine params first.

**Recommendation:** scope "Modulation Basics" (2 LFOs, 1 mod env, small fixed-slot
matrix targeting spine common-core params) as a v5.x point release — before or in
parallel with v6 design. Rename v16 stays "Deepening" and becomes true.

## 3. The unpriced bill: 256 voices × full stereo × nonlinear OS filters

Q1/Q2 locked *full stereo* and *256 voices* on fidelity grounds, explicitly deferring
the CPU reckoning to Q11 — whose budgets "remain to be set." Nothing shipped so far has
measured a per-voice cost. Reference points worth internalizing: the real **Summit is
16 voices** of this filter *in silicon*; the software VAs famous for this filter class
(Diva et al.) ship quality tiers precisely because ~a few dozen such voices saturate a
core. 256 stereo Huggett voices at an 8× OS tier is plausibly **two orders of
magnitude** over a realistic budget.

This is not an argument to abandon 256 — it is an argument to **price it now**:
the perf harness (`NlSvfPerfTests`, `MoogPipelineTests` machinery + SP-A) can measure
voice-seconds-per-second per model × OS tier × stereo in an afternoon. Then either
(a) 256 stands with an explicit quality-tier scheme (like the OS Live/Render split
already shipped — the pattern exists), or (b) Q2 gets honestly re-resolved. A number
beats a hope; "performance is a gate" is currently a principle with no gate wired.

## 4. The missing loop: no second human

v1→v25 currently plans a commercial flagship with exactly one player. The
authenticity program has a hardware ground truth (SP-D); the *product* has no
equivalent. Presets, envelope feel, velocity response, knob ranges, GUI flow — every
"gold industry standard" judgment except measurement is currently self-referential.
**Recommendation:** 1–2 trusted musicians on the Windows CI builds from v6 onward
(they already exist as artifacts); a 30-minute play session per shipped version, three
questions asked. Cheap, compounding, and it front-runs the v13 commercialization cliff.

## 5. Concentrated risks (tracked, but worth restating with mitigations)

- **v6 Dynamic VAST is the single largest technical unknown** (Q5/Q6/Q7/Q10 all
  red) and its difficulty is multiplied by the Cmajor GO decision: the spike proved
  *fixed* fused processors, not **runtime-restructured graphs** under the 256-voice
  in-place model. Before v6 design: decide *curated topology set vs free wiring*
  (register Q21) and spike *dynamic* graph rebuild in Cmajor (Q22). Note the K2000
  itself shipped **fixed algorithms** — a curated set is historically authentic and
  ~10× cheaper; free wiring is the K2088 "Dynamic VAST" ideal. This is a product
  decision, not just a technical one.
- **Summit excitation method** (SP-D top risk) has no written plan B. De-risk order:
  fingerprint **Arturia Mini V first** (software, zero rig risk, proves the method
  end-to-end), and draft the Summit fallback (characterize oscillator+filter jointly
  at pinned osc settings, deconvolve the known source; self-osc pitch/amplitude
  fingerprinting needs no input at all).
- **Q19** (stepped `spine.filterModel` automation semantics) is 🔴 open while the
  selector is already shipped and automatable — latent host-compat surprises. Cheap to
  close; close it before a host bug report does.
- **Licensing** (audit SCA): JUCE AGPL-vs-commercial becomes contractual at first paid
  distribution (v13); decide by v11.
- **"Security-scan CI baseline"** is listed as a continuous thread but not wired. The
  audit produced the pieces: ASan/UBSan suite (now clean end-to-end) + `-fanalyzer`
  are one nightly Linux CI job away; `pluginval --strictness-level 10` belongs in the
  Windows workflow.

## 6. What is *right* and should be defended

- **Measurement before voicing** (authenticity-purist decision) — rare discipline;
  SP-A made it real. Don't let filter-tweak temptation reorder it.
- **Append-only FilterModel registry + in-place per-voice state** (L7/Q18) — the
  audit's P0 was the one violation of this discipline's spirit, and it's fixed.
- **The question register itself** — Q17's plan-1 findings *predicted* the P0 bug
  class ("dangle every Voice's SpineFilterSlot::model_") a version before ASan proved
  it. The register works; keep grooming it.
- **Vision/status separation** (phases.md carries no status) — the cleanest
  anti-drift device in the repo.

## 7. New register questions raised by this review

Q21–Q26, appended to `docs/architecture/engine-questions.md`: v6 scope (curated vs
free wiring) · Cmajor dynamic-graph spike · per-voice CPU pricing → Q2 re-check ·
modulation-basics scheduling · Summit excitation plan B (Arturia first) · beta-player
loop. Register grooming pass included.

## 8. Suggested order of the next moves

1. **Modulation Basics** scoping decision (§2) — changes what v6 even needs.
2. **CPU pricing measurement** (§3) — an afternoon; informs every 🔴 question.
3. **Q21/Q22 v6-shape decisions** (§5) — before any v6 design work.
4. Anti-drift harness (engagement item 4) — folds in the audit's checkable invariants.
5. GUI sub-project — after §2's decision, since modulation panels are its biggest
   unknown.
