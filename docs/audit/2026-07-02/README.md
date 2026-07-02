# Codebase Health Audit — Executive Summary

**Version:** 5.12 (artifact; distinct from plugin SemVer)
**Date:** 2026-07-02
**Auditor:** Claude Fable 5 (gold-standard engagement, sub-project 1)
**Scope:** hand-written `src/` (~5k lines) + `tests/` infrastructure + build + docs. Cmajor-generated headers audited for provenance only.
**Baseline:** `feat/device-characterization-core` @ `6b9ee16` — 261 tests, 0 failed.

## Method

| Pass | Tooling | Report |
|---|---|---|
| Runtime (DAST-equivalent) | ASan + UBSan instrumented full test suite (GCC 13.3) | [01-memory-safety.md](01-memory-safety.md) |
| SAST | `gcc -fanalyzer -Wextra -Wshadow` on all 28 hand-written TUs via compile_commands | [02-static-analysis.md](02-static-analysis.md) |
| Architecture | scripted include-graph (cycles, layering, JUCE leakage) + manual read of the full audio path | [03-architecture.md](03-architecture.md) |
| SCA | third_party inventory, licenses, version pinning, generated-code provenance | [04-dependencies.md](04-dependencies.md) |
| Docs | last-commit-date staleness inventory | [05-doc-staleness.md](05-doc-staleness.md) |

Tool gap: `clang-tidy`, `cppcheck`, `valgrind`, `pluginval` are not installed on this box (no passwordless sudo). One `sudo apt install cppcheck clang-tidy valgrind` enables a deeper follow-up pass; `pluginval` (host-side plugin validation) is the highest-value addition for a shipping VST.

## Findings, ranked

| # | Severity | Finding | Where |
|---|---|---|---|
| 1 | **P0 — crash** | Heap-use-after-free: `SpineFilterSlot::prepare` destroys per-voice filter state through a stale `FilterModel*` after `Layer::prepare` has recreated the models. Fires on any OS-factor change / Live↔Offline re-prepare. | [01](01-memory-safety.md) |
| 2 | P1 | ASan coverage is **partial**: the run aborts at finding #1, so the rest of the suite is unverified under sanitizers. Re-run after the fix. | [01](01-memory-safety.md) |
| 3 | P2 | ~~Version-surface drift~~ **RESOLVED same day**: CMake 5.4.0 is correct (bump history is consistent); the SP-A spec misread the doc-artifact stream (5.09) as plugin SemVer "5.9.0" — spec corrected. Panel label already derives from `JucePlugin_VersionString`. Anti-drift check must treat the two numbering streams as distinct. | [02](02-static-analysis.md) |
| 4 | P2 | ~~4 deprecated `juce::Font(float,int)` call sites~~ **RESOLVED same day**: migrated to `FontOptions`; VST3 builds with zero deprecation warnings. | [02](02-static-analysis.md) |
| 5 | P3 | `params/Parameters.h → LayerRouting.h` is the codebase's only upward (params→top) layer dependency. | [03](03-architecture.md) |
| 6 | P3 | `Layer::prepare` hardcodes `models_[0]` as Huggett (`dynamic_cast` on index 0) while Moog is found by scan — fragile registry assumption. | [03](03-architecture.md) |
| 7 | P3 | ~92 `-Wsign-conversion` warnings in older TUs (benign index conversions; known deferred cleanup) + `params::snapshot` re-does ~30 string-keyed map lookups per layer per block (cache the `std::atomic<float>*` once). | [02](02-static-analysis.md), [03](03-architecture.md) |
| 8 | P4 | 5 Cmajor adapters are test-only spike artifacts (v6 on-ramp) with no label saying so; generated headers lack a regeneration note at point of use. | [04](04-dependencies.md) |
| 9 | P4 | Repo-root litter: stale `HANDOFF.md` (2026-06-20, pre-Moog), `REVIEW-PR7-filter-validation.md`, `ROADMAP-PR7-filter-validation-L3.md`, untracked `build_output.txt`. | [05](05-doc-staleness.md) |

## Verdicts on the requested categories

- **Dead/unused code:** essentially none in production paths. The PR #4 cleanup held. Five Cmajor adapters are test-anchored spike code kept deliberately (label them).
- **Circular dependencies:** **zero** header cycles (scripted DFS over the full `src/` include graph).
- **Bad include relationships:** one upward edge (`params → LayerRouting.h`); everything else flows downward (top → dsp/params/gui/util).
- **DSP/UI coupling:** none. `src/dsp/` includes no JUCE GUI module; only `juce_core` in 2 files (string names, adapter). Editor touches DSP only through the processor's atomics.
- **God classes:** none. Largest hand-written file is `PluginEditor.cpp` (440 lines) — disciplined (Sections/LabeledKnob/ParamBinder) but recommend per-section component extraction *before* the GUI sub-project grows it.
- **Ownership/lifetime:** one **P0 defect** (stale non-owning `FilterModel*` in voice slots across re-prepare) and one good pattern worth keeping (editor's `ParamBinder` declared last with documented destruction order).
- **Audio-thread safety:** parameter path is RT-safe (atomic raw-value reads; no locks/allocations in the steady path). Two documented borderline cases: Live↔Offline transition re-prepares (allocates) from the audio thread; `setLatencySamples` from that path.
- **Processor/editor/DSP/params/presets separation:** good. Processor is 146 lines and orchestration-only; state save/load is a single well-commented XML root; presets intentionally have no back-compat machinery (per standing decision).
- **JUCE-in-wrong-layer:** `testdsp/` L0 uses `juce_dsp` FFT by design (documented harness decision); `src/dsp` is effectively JUCE-free. No violations found.

## Recommended action order

1. Fix the P0 use-after-free (stable `models_` — create once in `Layer::prepare`, re-prepare in place) + regression test + full ASan re-run.
2. Reconcile the version surface (CMake VERSION ↔ docs ↔ panel label; derive the label from `JucePlugin_VersionString` per the standing note).
3. FontOptions migration (4 sites, mechanical).
4. Fold items 5–9 into the doc sweep / anti-drift harness sub-projects.
