# Dependency & Supply-Chain Analysis (SCA)

**Version:** 5.12 · **Date:** 2026-07-02 · Part of the [codebase health audit](README.md).

## Inventory

| Dependency | Version | How consumed | License |
|---|---|---|---|
| JUCE | **8.0.4** (vendored, `third_party/JUCE`) | full source in-tree | AGPLv3 / commercial dual (LICENSE.md present) |
| Cmajor toolchain | cmaj 1.0.3066 (build-time only, Docker jammy) | generates C++ headers, pinned in-repo | generated output shipped; toolchain not |
| GitHub Actions (Windows CI) | `build.yml` | build/smoke | n/a |

No package-manager dependencies (no CPM/vcpkg/Conan) — the supply-chain
surface is exactly one vendored framework plus pinned generated code. That is
a *small, auditable* surface; keep it that way.

## Findings

- **JUCE 8.0.4 pin:** upstream has moved past 8.0.4 (bug/security fixes in
  point releases). Vendoring means no automatic advisories — put a "check JUCE
  releases" item on a recurring cadence (anti-drift harness or a quarterly
  note). The 4 `Font` deprecations ([02](02-static-analysis.md)) are the first
  cost of drifting behind.
- **License posture:** JUCE dual license — confirm the intended distribution
  model before the demo/license-unlock milestone (v11+): AGPLv3 obligations
  vs a JUCE commercial seat. Flagging now because it becomes contractual at
  first paid distribution.
- **Cmajor generated headers** (`src/dsp/spine/cmajor/generated/`, ~7k lines,
  half the `src/` line count): pinned artifacts, good for reproducibility.
  Gaps: no header comment states the generator version + exact regeneration
  command at point of use (it lives in `tools/cmajor/cmaj-codegen.sh` +
  a memory note). Add a `generated/README.md` with: cmaj version, source
  `.cmajorpatch`, command, and "do not hand-edit".
- **Spike adapters are unlabeled (P4):** `CmajorSvfFilter`, `SvfLinearAdapter`,
  `NlSvfAdapter`, `NlSvfLeanAdapter`, `AsymDriveAdapter`, `WtOscAdapter` are
  referenced **only by tests** (equivalence/perf anchors for the ADR-0012 v6
  Cmajor path). Production reaches only `MoogLadderAdapter` (+
  `NlSvfDriveLeanAdapter` via its header). Add a `src/dsp/spine/cmajor/README.md`
  declaring which adapters are live vs v6 on-ramp, so future dead-code sweeps
  don't re-litigate them.
- `build_output.txt` (untracked, repo root) should be gitignored or deleted —
  build logs don't belong in the tree.
