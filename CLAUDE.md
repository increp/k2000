# k2000 / Bernie — Agent Contract

Rails for ANY agent (or human) working in this repo. Live status is never here —
it lives in the register, the dashboard, and git.

## Session ritual (do this FIRST)
1. Run `tools/drift-check --session` and read every WARN.
2. Read `docs/architecture/engine-questions.md` (the living register) before designing.
3. Roadmap truth is the dashboard: `cd tools/roadmap-dashboard && npm run dashboard`
   (`docs/roadmap/phases.md` is vision-only, no status).
4. Install the commit gate once per clone: `git config core.hooksPath .githooks`.

## Build rails
- ALWAYS `-j4`. Bare `-j` OOMs the JUCE compile (0-byte object -> confusing link error).
- `build/` is Release by convention. Debug work uses a separate named dir
  (e.g. `build-debug/`). `tools/drift-check --session` warns when this drifts.
- Suite: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
  (tee to `build/last-test-run.log` so drift-check can verify doc counts).
- Goldens regenerate ONLY on intentional change: `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests`,
  then justify the golden diff in the commit message.
- Opt-in heavy runs: `BERNIE_RUN_VOICEPERF=1` (perf pricing; Release only — the tool refuses Debug),
  `BERNIE_RUN_DISPARITY=1` (dense resonance sweep), `k2000_device_characterization` (deep characterization).

## Verification rails
- Perf numbers ONLY from Release builds (2026-07-02: a Debug-built table was ~10x wrong
  and briefly re-decided the voice-count target).
- Trusted smoke = Windows CI (`gh workflow run build.yml --ref <branch>`) into Ableton 12.
  Local Standalone is NOT a smoke target.
- Never claim tests/builds green without running them in this session.

## Process rails
- Question-heavy design: raise questions into the register, groom for consistency,
  THEN write specs/ADRs (docs/superpowers/specs + docs/decisions).
- Version every doc artifact (`Version: 5.xx`) — the artifact stream is NOT plugin
  SemVer (CMake `project(VERSION)`); name both explicitly when comparing.
- Non-ASCII strings reach JUCE only through `util::u8()`.
- Preset backward-compatibility is explicitly NOT a constraint (standing decision).
- One concern per markdown doc; no status tables in vision docs.

## Product constants (do not silently contradict)
- Voice target: **64 voices, always, at EVERY OS tier** (binding case 64 x os8; register Q2).
- All DSP voicing changes are HELD until real-hardware fingerprinting (SP-D);
  the standing stance is authenticity-purist.
- The synth is **Bernie** on every user-facing surface (plugin name, panel, docs — L6
  amended 2026-07-03), FX section **Ricky**; k2000 is the internal codename only
  (repo, CMake targets, class prefixes) pending a possible later identifier pass.
- Anchor references: Novation Summit + Kurzweil K2061/K2088 ONLY (K2000 dropped).
