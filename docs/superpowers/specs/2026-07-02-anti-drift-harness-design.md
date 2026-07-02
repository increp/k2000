# Anti-Drift Harness — Design

**Version:** 5.14 (artifact; distinct from plugin SemVer 5.4.0)
**Date:** 2026-07-02
**Status:** Approved (brainstorm) — pending spec review
**Relates to:** the 2026-07-02 codebase audit (`docs/audit/2026-07-02/`), the fit-for-purpose review (`docs/reviews/2026-07-02-fit-for-purpose-review.md`), and the gold-standard engagement (item 4).

---

## 1. Purpose & premise

Drift is this project's chronic disease, in four confirmed strains — **AI/session drift** (agents diverging from plans/specs between sessions), **doc–code drift**, **DSP measurement drift**, and **sound/voicing drift**. 2026-07-02 alone produced four live specimens: the `build/` dir silently in Debug against the documented Release convention (corrupting the first published perf table), the "plugin SemVer 5.9.0" claim born of a version-stream collision with artifact numbering "5.09", a 12-day-stale root `HANDOFF.md` describing a pre-Moog world, and doc test-counts (`227`, `138`) frozen at old suite sizes.

The harness is a **layered safety net that makes drift either impossible to commit or impossible to miss** — one implementation of every check, enforced at three points, with the sound itself fingerprinted.

## 2. Decisions (resolved 2026-07-02, with the user)

1. **Enforcement points: all three** — session-start ritual, pre-commit hook, CI job.
2. **Failure policy: tiered** — deterministic facts HARD-FAIL (hook/CI); judgment signals WARN (session) and never block.
3. **Agent contract: full `CLAUDE.md`** — the rails live in git, not in any one agent's memory.
4. **Voicing layer: metric gates + end-to-end patch fingerprints at ALL OS tiers** (user: "I will use it at os8" — os1-only would guard a configuration nobody plays).
5. **Architecture: thin orchestrator (Approach A)** — one stdlib-only tool, no frameworks, no new dependencies.
6. Context decision recorded alongside: **voice target is 64, always, at every OS tier** (register Q2 final); the binding perf budget is 64 × os8 realtime.

## 3. Architecture

```
tools/drift-check                  python3, stdlib only; exit 0 = clean, 1 = FAIL in tier
  --session    fast facts + warnings (agent ritual; < 2 s, no build required)
  --commit     deterministic hard-fails only (hook path; < 2 s)
  --ci         everything, incl. suite-dependent checks (reads the test run's output)
  --self-test  every check proven against synthetic pass/fail fixtures in a temp dir
CLAUDE.md                          the agent contract (§6)
.githooks/pre-commit               thin shell → tools/drift-check --commit
                                   (installed once: git config core.hooksPath .githooks)
.github/workflows/drift.yml        Linux: Release configure → build k2000_tests → run suite
                                   → drift-check --ci; second job: ASan/UBSan suite
tests/RenderFingerprintTests.cpp   the voicing layer (§5), always-on in k2000_tests
tests/golden/fingerprints/*.csv    committed signatures
```

Each check is one registered function `(name, tiers, severity, fn)`; output is one line per check (`FAIL` / `WARN` / `ok` + a one-line remedy); tunables (age thresholds, tolerances) live in one constants block at the top of the tool. The hook stays bypassable (`--no-verify`) by design; CI is the backstop that catches bypassed drift.

## 4. Check inventory (v1 — each traces to a real specimen)

| # | Check | Tier | Bite | Specimen prevented |
|---|---|---|---|---|
| 1 | Every "plugin SemVer X" claim in `docs/` == CMake `project(VERSION)` | commit, ci | FAIL | the 5.9.0/5.09 collision |
| 2 | Doc "Summary: N tests" claims == the actual suite count | ci (session: warn vs last local run) | FAIL | stale 227/138 counts |
| 3 | No `*.md` at repo root except `README.md` | commit, ci | FAIL | HANDOFF/REVIEW litter |
| 4 | Relative `.md` links under `docs/` resolve | commit, ci | FAIL | the spec's broken L3 link |
| 5 | `build*/` gitignored; no tracked build logs (`*_output.txt` etc.) | commit, ci | FAIL | `build_output.txt` |
| 6 | `build/` cache is `CMAKE_BUILD_TYPE=Release` (if the dir exists) | session | WARN | the Debug perf-table fiasco |
| 7 | Newest doc under `docs/handoffs/` (if any) younger than 14 days, else flagged stale | session | WARN | the misleading 2026-06-20 HANDOFF |
| 8 | Register 🔴 questions whose "Resolve at" version is `shipped` in `roadmap.json` | session, ci | WARN | Q19-style silent aging |
| 9 | `roadmap.json` parses; every item has `id`/`kind`/`status` | commit, ci | FAIL | dashboard schema drift |
| 10 | Patch fingerprints match goldens (all OS tiers) | ci (lives in the suite) | FAIL | voicing drift; OS-chain regressions |

Out of scope for v1 (YAGNI until a specimen exists): semantic doc-content checks, WAV-level audio diffs, spec↔code API cross-validation, autoresearch integration.

## 5. Voicing layer — patch fingerprints

**Five reference patches**, chosen to cover every load-bearing signal path:
1. **Init saw** — osc → thru (baseline; no spine coloration beyond LP24 defaults).
2. **Huggett LP24, res 0.7, drive 0.5** — the nonlinear signature path.
3. **Huggett LP+HP routing, separation +2 oct** — dual-section + separation law.
4. **Moog LP24, res 0.8, bass sub-osc on** — second model + `setVoiceContext` path.
5. **HP-pre 500 Hz + shaper drive 0.6** — pre-filter + graph-block interaction.

Each patch renders **~1 s at 48 kHz through the full production `Voice::render` path at every OS factor {1, 2, 4, 8}** — 20 signatures. A signature is compact and musically toleranced, not sample-exact: 10 octave-band RMS values (dB, ±0.5 dB), output peak dBFS (±0.25 dB), THD+N proxy at the fundamental (±1 dB) — ~12 numbers. Sample-exact WAV diffs were rejected: any intentional voicing change would churn everything; band-level signatures catch audible drift while tolerating denormal-grade noise.

Failures name **patch + OS tier + metric**. Intentional voicing changes regenerate via the existing `BERNIE_UPDATE_GOLDEN=1` workflow; the golden diff in review is the audit trail. Cost: 20 renders ≈ well under 2 s of suite time at Release (measured voice costs, Q23).

## 6. CLAUDE.md — the agent contract

Sections (rails only; pointers into living docs for detail — no duplicated status):
- **Session ritual:** run `tools/drift-check --session` FIRST; read the engine-questions register + dashboard before designing; never trust a handoff older than its drift warning.
- **Build rails:** `-j4` always; `build/` is Release by convention (Debug work uses a separate named dir); golden regeneration workflow; the `BERNIE_*` env gates.
- **Verification rails:** perf numbers only from Release builds (cite the 2026-07-02 fiasco); smoke only via Windows CI (`gh workflow run build.yml`); never claim green without a run.
- **Process rails:** question-heavy design into the register, groomed before specs/ADRs; versioned artifacts (artifact stream ≠ plugin SemVer — name both explicitly); `util::u8()` for non-ASCII; preset back-compat is explicitly NOT a constraint; docs split per concern.
- **Product constants:** voice target 64 at every OS tier (binding case 64 × os8); DSP voicing HELD for hardware (SP-D); authenticity-purist standing decision.

## 7. CI workflow (`drift.yml`)

Job 1 **drift** (ubuntu): checkout → configure Release → build `k2000_tests` `-j4` → run suite (captures the count for check #2) → `tools/drift-check --ci --self-test`. Job 2 **sanitizers** (ubuntu): ASan+UBSan configure → build → full suite (the memory-safety baseline stays green). Both push-triggered on `main` + PRs; the existing Windows `build.yml` stays the manual smoke.

## 8. Error handling & self-trust

`drift-check --self-test` builds a temp fixture tree per check (one passing, one failing variant) and asserts the check's verdict on each — the harness cannot silently rot (CI runs the self-test every push). Checks that need optional inputs (no `build/` dir, no last-run output) report `skip`, never a false `ok`. Unknown/parse-failure states are FAILs, not skips, in `--ci`.

## 9. Success criteria

- All four 2026-07-02 specimens, replayed synthetically, are caught by the named check (self-test proves it).
- A deliberate 0.6 dB voicing nudge to any spine stage trips a fingerprint at the right patch/tier.
- `--session` completes < 2 s without a build; `--commit` < 2 s; hook installed and firing.
- Suite + drift CI green on main; ASan job green.

## 10. Deferred / follow-ups

- Fingerprints for future models (SEM+) — add a patch per model on arrival (append-only, like the registry).
- Weak-machine perf budget check (64 × os8 realtime on a reference machine) — joins CI when a reference machine exists (Q11).
- Autoresearch-driven drift repair — explicitly out (earmarked for SP-D per the standing note).
