# Bernie (repo k2000) — Session Handoff

**Version:** 5.16 (artifact) · **Date:** 2026-07-03
Written for a fresh session with zero memory. Concrete paths and symbols throughout.
**Supersedes** `2026-06-20-session-handoff.md` (archived here; pre-Moog, do not trust).
**FIRST ACTION of any session:** run `tools/drift-check --session` and read `CLAUDE.md` (the agent contract) — both were built this session and are the rails.

---

## 1. What happened (2026-07-02 → 03): the "gold-standard engagement", Fable took over from Opus

One giant arc: finish SP-A → full codebase audit → doc sweep → fit-for-purpose review → perf truth → anti-drift harness → SP-B Phase 0 → the Q27 Huggett defect found/ruled/FIXED → live-progress tooling. The product is **Bernie** (FX section **Ricky**); **K2000 naming is ruled fully retired** (rename branch pending, see §6).

## 2. Repo state RIGHT NOW

- **main @ `7af930e`** (= PR #10). Contains: SP-A complete (M1–M4, PR #8), the audit + P0 use-after-free fix, doc sweep, fit-for-purpose review, halfband optimization, **anti-drift harness (PR #9)**, SP-B Phase 0 large-signal read (PR #10).
- **Open PR #11** `fix/huggett-bounded-resonance` — the Q27 fix + whistle/click UAT iteration. **ALL CHECKS GREEN. Gate = the user's EARS**: audition the Windows artifact (max-res whistle blooms & holds; first knob millimeter seamless; no dropouts) before merging.
- **Open PR #12** `feat/chz-live-progress` — runner progress sink + heavy-runner stderr ETA line.
- **Merge-order note:** #11 then #12; both bump the suite count → `docs/filter-validation/README.md` "Expected: Summary: N tests" will conflict/need one final bump (≈290) — the drift CI will refuse to let you get it wrong.
- Suite: main 285 blocks · PR #11 289 · PR #12 286. All 0-failed, ASan/UBSan clean, Windows CI green on all heads.

## 3. The Q27 story (the session's centerpiece — read `docs/reviews/2026-07-02-huggett-large-signal-read.md`)

1. SP-A measured Huggett's small-signal resonant peak at **+89 dB** vs Moog +4.9.
2. SP-B Phase 0 (`BERNIE_RUN_LARGESIGNAL=1`, `tests/LargeSignalTests.cpp`) falsified the "self-limiting" theory: gain **ROSE** with input (+61→+86 dB, output +89 dBFS at −6 dBFS in) — the resonance nonlinearity was **anti-damping**. User field report (dropouts at max res = the safety limiter slamming; "unusable") → **ruled a DEFECT, fix authorized** despite the voicing hold.
3. Root cause: **transposed operands** in `NlSvfCell::step()` — `(satRes(bp) − bp)` instead of `(bp − satRes(bp))` = positive BP feedback growing with amplitude. Fixed + **OTA-style soft state rails** (`rail(x)=padTanh(x/4)·4`).
4. UAT iteration (user's ears): (a) the whistle had died — the old self-osc was *powered by the defect*; now the top 5 % of the res knob is genuinely **regenerative** (`kOscStart=0.95`, `kOscDepth=0.012` in `recompute()`; damping fades through zero; rails set whistle amplitude); (b) first-increment click — rails/delta now **blend ∝ resSat** (continuous knob). Delta uses `|k_|` (k goes negative at the top).
5. Everything mirrored into `src/dsp/spine/cmajor/NlSvf.cmajor` + `NlSvfDrive.cmajor`, regenerated (**bit-exact equivalence kept**). Golden churn was surgical 3×: **Moog + init_saw byte-identical every time**; only Huggett-resonance-active rows moved.
6. Post-fix law: res .9 @ −6 dBFS in → +8 dB gain, out ≈ +2 dBFS; voice at res 1.0/os8 peaks −1.5 dBFS (was +22). Dense-sweep peak +25.9 vs Moog +4.9 (disparity 21 dB); tiny-signal (+61 dB) character intact. Filters remain clearly distinct (passband: Huggett keeps bass, Moog drops −12.6 dB; knees −83 vs −26 dBFS; asymmetric-even vs ladder-odd harmonics).
7. **All voicing numbers are `// CALIB` dials for SP-D** (rail level, asymmetry b=0.18, Q taper, kOscStart/Depth). The authenticity ruling still waits for real-Summit fingerprinting; Arturia-first de-risk (register Q25).

## 4. The anti-drift harness (built + already earning: 3 CI catches on its own PRs)

- `tools/drift-check` — 10 checks, `--session` / `--commit` / `--ci` / `--self-test` (fixture-proven). Pre-commit gate live (`git config core.hooksPath .githooks` — once per clone). `.github/workflows/drift.yml` = Linux drift job + **ASan/UBSan suite job** on every push/PR.
- **Patch fingerprints**: `tests/RenderFingerprintTests.cpp` — 5 reference patches × os{1,2,4,8}, 240 golden metrics in `tests/golden/fingerprints/baseline.csv`. Voicing changes churn them ON PURPOSE (`BERNIE_UPDATE_GOLDEN=1`, justify in the commit).
- `CLAUDE.md` = the rails (session ritual, `-j4`, Release-only perf, golden workflow, `BERNIE_*` env gates, product constants).
- Suite-count discipline: `docs/filter-validation/README.md` quotes the exact count; update it when the suite grows or drift CI fails the push (by design).

## 5. Perf truth (register Q23) + the trap that almost re-decided the product

- **TRAP:** the first pricing table was measured on a **Debug build dir** (drift!) and was ~10× wrong — it briefly re-decided the voice target. `VoicePerfTests` now REFUSES Debug; perf numbers only from Release (CLAUDE.md rail).
- Real numbers (Release, dev box, per voice): os1 0.13–0.33 % · os2 0.51 % (after the halfband-zeros optimization, was 1.67) · os4 ~1.3–2.0 % · os8 2.9 %.
- **Voice target FINAL (user): 64 voices, always, at EVERY OS tier** — binding case 64×os8 ≈ 1.9 cores → multicore voice rendering is a hard requirement (v9 area). `VoiceManager` today: single-threaded, `std::array<Voice,64>`.
- `Halfband2x` exploits exact halfband zeros (golden-equivalence-guarded); `tools/cmajor/cmaj-codegen.sh` takes `MAXFRAMES` env — **generated NlSvf/NlSvfDrive headers need `MAXFRAMES=512`** (script default 32 shrinks I/O arrays → lean-adapter overflow, ASan-caught). Codegen needs Docker: `sg docker -c "MAXFRAMES=512 tools/cmajor/cmaj-codegen.sh <patch> <out.h>"` — INPUT is the `.cmajorpatch`.

## 6. Decisions register (all in `docs/architecture/engine-questions.md`, Q1–Q27)

New/changed this session: **Q21** v6 scope gated on **Q22** Cmajor dynamic-graph spike (before v6 design) · **Q23** measured (above) · **Q24 🟢 Modulation Basics = v5.x point release BEFORE v6** (roadmap `v5-modulation-basics`; the mod panels are also the next GUI increment — run GUI+ModBasics as one design cycle) · **Q26** beta players: not yet (re-raise post-ModBasics) · **Q27 🟢 fixed** (above) · **Q2 FINAL 64@all tiers** · roadmap `v5-thermal-bloom` (noise-seeded self-osc onset, level calibrated at SP-D) · **Bernie rename ruled** (supersedes L6; memory `feedback_bernie_naming`; own branch AFTER #11/#12 merge — touches targets/classes/CI/docs; scope depth [repo name? class prefixes?] to confirm with user at kickoff).

## 7. Next steps, in order

1. **User auditions PR #11** (whistle bloom+hold, click-free first increment, no dropouts) → merge #11 → merge #12 → reconcile the README count (drift CI arbitrates).
2. **Bernie rename branch** (see §6).
3. **Grid-economy review** (engagement item 7): `chz::fullGrid()` = 36 000 points/model; design targeted grids (the new ETA readout makes the cost visible).
4. **GUI + Modulation Basics** combined design cycle (brainstorm → spec; inputs: audit's per-section-component extraction, Q9, tiered-immediacy preference).
5. **Q22 Cmajor dynamic-graph spike** before any v6 design.
6. SP-B proper (drive-axis large-signal, `NlSvfCell` mechanism dissection notes) · B4 time-align (Q20) · pluginval + clang-tidy/cppcheck install (audit tool gap) · thermal-bloom item.

## 8. Verify / build (see CLAUDE.md for the full rails)

```bash
tools/drift-check --session                       # ALWAYS FIRST
cmake --build build --target k2000_tests -j4      # build/ is Release by convention
./build/tests/k2000_tests 2>/dev/null | tee build/last-test-run.log | tail -1
# opt-in heavies: BERNIE_RUN_LARGESIGNAL=1 · BERNIE_RUN_DISPARITY=1 · BERNIE_RUN_VOICEPERF=1 (Release only)
# heavy runner (now with live ETA): ./build/tests/k2000_device_characterization --model all --quick
```
