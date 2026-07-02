# Anti-Drift Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the layered drift safety net from the 2026-07-02 spec: a three-tier `tools/drift-check` (session/commit/ci + self-test), a pre-commit hook, the `CLAUDE.md` agent contract, a Linux drift+sanitizer CI workflow, and 5 reference patches fingerprinted at all 4 OS tiers.

**Architecture:** One stdlib-only Python tool holds every check as a registered `(name, tiers, severity, fn)`; hook and CI call the same tool. The voicing layer lives where DSP lives — a `juce::UnitTest` in `k2000_tests` with `GoldenSet` baselines. Self-test fixtures prove each check catches its specimen.

**Tech Stack:** Python 3 (stdlib only), bash, GitHub Actions (ubuntu), C++17/JUCE 8 (`juce::UnitTest`, `testdsp::{Spectrum,Level,Metrics,GoldenSet}`).

## Global Constraints

- Work on branch `feat/anti-drift-harness` off `main`.
- **Bounded build parallelism `-j4`**, never bare `-j`.
- `tools/drift-check`: **python3 stdlib only**, executable, exit 0 = clean / 1 = any FAIL in the invoked tier; checks that lack optional inputs report `skip`, never a false `ok`; in `--ci`, parse failures are FAILs.
- Sentinel/tolerances (spec §5): band RMS ±0.5 dB, peak ±0.25 dB, THD proxy ±1.0 dB; handoff max age 14 days; all tunables in one constants block.
- Fingerprints: 5 patches × OS {1,2,4,8}, full production `Voice::render`, 48 kHz base, `GoldenSet("fingerprints/baseline")`, regeneration via `BERNIE_UPDATE_GOLDEN=1`.
- Suite bar: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests` → 0 failed (current: 264 blocks, `build/` is Release).
- Commits end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

## File Structure

- **Create** `tools/drift-check` — the orchestrator + all filesystem checks + self-test (single file, ~400 lines; checks are small functions, constants at top).
- **Create** `.githooks/pre-commit` — 3-line shell shim.
- **Create** `tests/RenderFingerprintTests.cpp` — the voicing layer.
- **Create** `tests/golden/fingerprints/baseline.csv` — generated (Task 5).
- **Create** `.github/workflows/drift.yml` — drift + sanitizer jobs.
- **Create** `CLAUDE.md` — the agent contract.
- **Modify** `tests/CMakeLists.txt` — add the fingerprint test source.

---

## Task 1: `tools/drift-check` skeleton, self-test framework, filesystem checks (#3, #5, #9)

**Files:**
- Create: `tools/drift-check` (mode 755)

**Interfaces:**
- Produces: CLI `drift-check [--session|--commit|--ci] [--root DIR] [--suite-log FILE] [--self-test]`; internal registry `CHECKS: list[Check]` with `Check(name, tiers, severity, fn)` where `fn(root: Path, ctx: dict) -> tuple[str, str]` returning `("ok"|"skip"|"warn"|"fail", message)`; self-test contract: `FIXTURES[name] = (make_pass(dir), make_fail(dir))`.

- [ ] **Step 1: Write the tool with the self-test harness and three checks**

Create `tools/drift-check`:

```python
#!/usr/bin/env python3
"""k2000 anti-drift harness (spec: docs/superpowers/specs/2026-07-02-anti-drift-harness-design.md).

Tiers: --session (fast facts + warnings), --commit (deterministic hard-fails),
--ci (everything; parse failures are FAILs). --self-test proves every check
against synthetic pass/fail fixtures so the harness itself cannot rot.
Stdlib only. Exit 0 = no FAIL in the invoked tier; 1 otherwise.
"""
import argparse, json, os, re, subprocess, sys, tempfile, time
from pathlib import Path

# ---- tunables (spec §3/§4) --------------------------------------------------
HANDOFF_MAX_AGE_DAYS = 14
ALLOWED_ROOT_MD      = {"README.md"}
BUILD_LOG_GLOBS      = ["*_output.txt", "build_output*.txt"]

# ---- registry ---------------------------------------------------------------
class Check:
    def __init__(self, name, tiers, severity, fn):
        self.name, self.tiers, self.severity, self.fn = name, tiers, severity, fn

CHECKS = []
FIXTURES = {}   # name -> (make_pass(dir), make_fail(dir))

def check(name, tiers, severity):
    def deco(fn):
        CHECKS.append(Check(name, set(tiers), severity, fn))
        return fn
    return deco

def fixture(name):
    def deco(fn):
        FIXTURES[name] = fn
        return fn
    return deco

# ---- checks -----------------------------------------------------------------
@check("root-md-litter", {"commit", "ci"}, "fail")
def chk_root_md(root, ctx):
    """No *.md at repo root except README.md (specimen: stale HANDOFF/REVIEW litter)."""
    stray = sorted(p.name for p in root.glob("*.md") if p.name not in ALLOWED_ROOT_MD)
    if stray:
        return "fail", f"stray root markdown: {', '.join(stray)} -> move under docs/"
    return "ok", "repo root clean"

@check("tracked-build-logs", {"commit", "ci"}, "fail")
def chk_build_logs(root, ctx):
    """No build logs in the tree; build*/ dirs gitignored (specimen: build_output.txt)."""
    for g in BUILD_LOG_GLOBS:
        hits = sorted(p.name for p in root.glob(g))
        if hits:
            return "fail", f"build log(s) at root: {', '.join(hits)} -> delete + gitignore"
    gi = root / ".gitignore"
    text = gi.read_text(encoding="utf-8") if gi.exists() else ""
    if not re.search(r"^build", text, re.M):
        return "fail", ".gitignore does not ignore build dirs (need build*/ entries)"
    return "ok", "no tracked build logs; build dirs ignored"

@check("roadmap-schema", {"commit", "ci"}, "fail")
def chk_roadmap(root, ctx):
    """roadmap.json parses; every item dict with 'kind' has id+status (dashboard contract)."""
    p = root / "tools/roadmap-dashboard/roadmap.json"
    if not p.exists():
        return "skip", "no roadmap.json"
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        return "fail", f"roadmap.json does not parse: {e}"
    bad = []
    def walk(o):
        if isinstance(o, dict):
            if "kind" in o and not ("id" in o and "status" in o):
                bad.append(o.get("id") or o.get("title", "?"))
            for v in o.values(): walk(v)
        elif isinstance(o, list):
            for v in o: walk(v)
    walk(data)
    if bad:
        return "fail", f"roadmap items missing id/status: {', '.join(map(str, bad[:5]))}"
    return "ok", "roadmap schema sound"

# ---- fixtures ---------------------------------------------------------------
@fixture("root-md-litter")
def fx_root_md(d, passing):
    (d / "README.md").write_text("# r\n")
    if not passing:
        (d / "HANDOFF.md").write_text("stale\n")

@fixture("tracked-build-logs")
def fx_build_logs(d, passing):
    (d / ".gitignore").write_text("build*/\n")
    if not passing:
        (d / "build_output.txt").write_text("log\n")

@fixture("roadmap-schema")
def fx_roadmap(d, passing):
    p = d / "tools/roadmap-dashboard"; p.mkdir(parents=True)
    item = {"id": "v1", "kind": "version", "status": "shipped"} if passing \
        else {"kind": "version", "title": "orphan"}
    (p / "roadmap.json").write_text(json.dumps({"items": [item]}))

# ---- engine ------------------------------------------------------------------
def run_tier(root, tier, ctx):
    failed = False
    for c in CHECKS:
        if tier not in c.tiers:
            continue
        try:
            status, msg = c.fn(root, ctx)
        except Exception as e:                       # a broken check must not pass silently
            status, msg = ("fail" if tier == "ci" else "warn"), f"check crashed: {e}"
        if status == "fail" and c.severity == "warn":
            status = "warn"
        print(f"[{status.upper():4}] {c.name}: {msg}")
        if status == "fail":
            failed = True
    return 1 if failed else 0

def self_test():
    bad = 0
    for c in CHECKS:
        fx = FIXTURES.get(c.name)
        if fx is None:
            print(f"[FAIL] self-test: no fixture for check '{c.name}'"); bad += 1
            continue
        for passing in (True, False):
            with tempfile.TemporaryDirectory() as td:
                d = Path(td); fx(d, passing)
                status, _ = c.fn(d, {})
                want_ok = status in ("ok", "skip") if passing else status in ("fail", "warn")
                if not want_ok:
                    print(f"[FAIL] self-test: {c.name} {'pass' if passing else 'fail'}-fixture -> {status}")
                    bad += 1
    print(f"self-test: {len(CHECKS)} checks, {bad} problems")
    return 1 if bad else 0

def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group()
    for t in ("session", "commit", "ci"):
        g.add_argument(f"--{t}", action="store_const", const=t, dest="tier")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--root", type=Path, default=None)
    ap.add_argument("--suite-log", type=Path, default=None)
    a = ap.parse_args()
    if a.self_test:
        sys.exit(self_test())
    root = a.root or Path(subprocess.run(["git", "rev-parse", "--show-toplevel"],
                          capture_output=True, text=True, check=True).stdout.strip())
    sys.exit(run_tier(root, a.tier or "session", {"suite_log": a.suite_log}))

if __name__ == "__main__":
    main()
```

Then: `chmod +x tools/drift-check`.

- [ ] **Step 2: Run the self-test to verify it passes (and fails when sabotaged)**

Run: `tools/drift-check --self-test`
Expected: `self-test: 3 checks, 0 problems`, exit 0.
Sanity-check the harness bites: temporarily change `ALLOWED_ROOT_MD` to `set()` — self-test must now report a problem for `root-md-litter`'s pass fixture; revert.

- [ ] **Step 3: Run --commit against the real repo**

Run: `tools/drift-check --commit`
Expected: all three checks `ok`, exit 0 (repo was cleaned 2026-07-02).

- [ ] **Step 4: Commit**

```bash
git add tools/drift-check
git commit -m "feat(drift): drift-check orchestrator + self-test + filesystem checks" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: Version-claim and doc-link checks (#1, #4)

**Files:**
- Modify: `tools/drift-check` (add two checks + fixtures in the same sections)

**Interfaces:**
- Consumes: registry decorators from Task 1.
- Produces: checks `version-claims` and `doc-links`, tiers `{commit, ci}`, severity fail.

- [ ] **Step 1: Add the checks**

Insert after `chk_roadmap` (checks section):

```python
@check("version-claims", {"commit", "ci"}, "fail")
def chk_version_claims(root, ctx):
    """Any 'plugin SemVer X.Y.Z' claim in docs/ must equal CMake project(VERSION)
    (specimen: the 5.9.0 claim born of misreading artifact stream 5.09)."""
    cm = (root / "CMakeLists.txt")
    if not cm.exists():
        return "skip", "no CMakeLists.txt"
    m = re.search(r"project\(\s*\S+\s+VERSION\s+([0-9.]+)", cm.read_text(encoding="utf-8"))
    if not m:
        return "fail", "CMakeLists.txt has no project(VERSION)"
    truth = m.group(1)
    bad = []
    for p in (root / "docs").rglob("*.md"):
        for c in re.findall(r"plugin SemVer[^0-9]{0,4}([0-9]+\.[0-9]+\.[0-9]+)",
                            p.read_text(encoding="utf-8", errors="replace")):
            if c != truth:
                bad.append(f"{p.relative_to(root)} claims {c}")
    if bad:
        return "fail", f"CMake VERSION is {truth} but: " + "; ".join(bad[:4])
    return "ok", f"all plugin-SemVer claims match {truth}"

@check("doc-links", {"commit", "ci"}, "fail")
def chk_doc_links(root, ctx):
    """Relative .md links under docs/ must resolve (specimen: the spec's broken L3 link)."""
    bad = []
    docs = root / "docs"
    if not docs.exists():
        return "skip", "no docs/"
    for p in docs.rglob("*.md"):
        for target in re.findall(r"\]\(([^)#\s]+\.md)\)",
                                 p.read_text(encoding="utf-8", errors="replace")):
            if target.startswith(("http://", "https://")):
                continue
            if not (p.parent / target).resolve().exists():
                bad.append(f"{p.relative_to(root)} -> {target}")
    if bad:
        return "fail", "dangling doc links: " + "; ".join(bad[:5])
    return "ok", "doc links resolve"
```

Insert fixtures (fixtures section):

```python
@fixture("version-claims")
def fx_version_claims(d, passing):
    (d / "CMakeLists.txt").write_text('project(k2000 VERSION 5.4.0 LANGUAGES C CXX)\n')
    (d / "docs").mkdir()
    v = "5.4.0" if passing else "5.9.0"
    (d / "docs/spec.md").write_text(f"distinct from plugin SemVer {v}\n")

@fixture("doc-links")
def fx_doc_links(d, passing):
    (d / "docs").mkdir()
    (d / "docs/a.md").write_text("see [b](b.md)\n")
    if passing:
        (d / "docs/b.md").write_text("# b\n")
```

- [ ] **Step 2: Self-test + real-repo run**

Run: `tools/drift-check --self-test && tools/drift-check --commit`
Expected: `self-test: 5 checks, 0 problems`; real repo all `ok` (the SP-A spec's claim was corrected to 5.4.0 on 2026-07-02 — if this run FAILS on a lingering claim, that is the check working: fix the doc, not the check).

- [ ] **Step 3: Commit**

```bash
git add tools/drift-check
git commit -m "feat(drift): version-claim + doc-link checks" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3: Session-tier checks (#6, #7, #8) + suite-count check (#2)

**Files:**
- Modify: `tools/drift-check`

**Interfaces:**
- Produces: checks `build-dir-type` (session, warn), `handoff-age` (session, warn), `register-aging` (session+ci, warn), `suite-count-claims` (ci fail; session warn when `build/last-test-run.log` exists). `--suite-log FILE` feeds `suite-count-claims` in CI.

- [ ] **Step 1: Add the checks**

```python
@check("build-dir-type", {"session"}, "warn")
def chk_build_type(root, ctx):
    """build/ must be Release per convention (specimen: the Debug perf-table fiasco)."""
    cache = root / "build/CMakeCache.txt"
    if not cache.exists():
        return "skip", "no build/ dir"
    m = re.search(r"^CMAKE_BUILD_TYPE:\w+=(\S+)", cache.read_text(encoding="utf-8"), re.M)
    if m and m.group(1) != "Release":
        return "warn", f"build/ is {m.group(1)}, convention is Release (perf numbers there are meaningless)"
    return "ok", "build/ is Release"

@check("handoff-age", {"session"}, "warn")
def chk_handoff_age(root, ctx):
    """Newest doc under docs/handoffs/ must be < HANDOFF_MAX_AGE_DAYS (specimen: 2026-06-20 HANDOFF)."""
    hd = root / "docs/handoffs"
    if not hd.exists() or not any(hd.glob("*.md")):
        return "skip", "no handoffs"
    newest = max(hd.glob("*.md"), key=lambda p: p.stat().st_mtime)
    age = (time.time() - newest.stat().st_mtime) / 86400.0
    if age > HANDOFF_MAX_AGE_DAYS:
        return "warn", f"{newest.name} is {age:.0f} days old — do not trust it as current state"
    return "ok", f"{newest.name} is {age:.0f} days old"

@check("register-aging", {"session", "ci"}, "warn")
def chk_register_aging(root, ctx):
    """Open (red) register questions whose resolve-at version already shipped (specimen: Q19)."""
    reg = root / "docs/architecture/engine-questions.md"
    rj  = root / "tools/roadmap-dashboard/roadmap.json"
    if not reg.exists() or not rj.exists():
        return "skip", "register or roadmap missing"
    shipped = set()
    def walk(o):
        if isinstance(o, dict):
            if o.get("kind") == "version" and o.get("status") == "shipped":
                shipped.add(str(o.get("id")))
            for v in o.values(): walk(v)
        elif isinstance(o, list):
            for v in o: walk(v)
    walk(json.loads(rj.read_text(encoding="utf-8")))
    aged = []
    for line in reg.read_text(encoding="utf-8").splitlines():
        if "\U0001F534" not in line or not line.startswith("| Q"):
            continue
        cols = [c.strip() for c in line.split("|")]
        if len(cols) < 6:
            continue
        qid, resolve_at = cols[1], cols[4]
        for v in re.findall(r"\bv[0-9.]+\b", resolve_at):
            if v in shipped:
                aged.append(f"{qid} (resolve at {v}, shipped)")
    if aged:
        return "warn", "aging open questions: " + "; ".join(aged[:5])
    return "ok", "no open questions past their resolve-at version"

@check("suite-count-claims", {"session", "ci"}, "fail")
def chk_suite_counts(root, ctx):
    """Doc 'Summary: N tests' claims must equal the actual suite count
    (specimen: the stale 227/138 counts). CI passes --suite-log; locally
    build/last-test-run.log is used when present."""
    log = ctx.get("suite_log") or (root / "build/last-test-run.log")
    log = Path(log)
    if not log.exists():
        return "skip", "no suite log (run the suite and tee to build/last-test-run.log)"
    m = re.search(r"Summary:\s+(\d+)\s+tests", log.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return "fail", f"{log} has no 'Summary: N tests' line"
    actual = m.group(1)
    bad = []
    for p in (root / "docs").rglob("*.md"):
        for c in re.findall(r"Summary:\s+(\d+)\s+tests", p.read_text(encoding="utf-8", errors="replace")):
            if c != actual:
                bad.append(f"{p.relative_to(root)} says {c}")
    if bad:
        return "fail", f"suite has {actual} tests but: " + "; ".join(bad[:4])
    return "ok", f"doc test-counts match ({actual})"
```

Fixtures:

```python
@fixture("build-dir-type")
def fx_build_type(d, passing):
    (d / "build").mkdir()
    t = "Release" if passing else "Debug"
    (d / "build/CMakeCache.txt").write_text(f"CMAKE_BUILD_TYPE:STRING={t}\n")

@fixture("handoff-age")
def fx_handoff_age(d, passing):
    hd = d / "docs/handoffs"; hd.mkdir(parents=True)
    p = hd / "h.md"; p.write_text("# h\n")
    if not passing:
        old = time.time() - (HANDOFF_MAX_AGE_DAYS + 5) * 86400
        os.utime(p, (old, old))

@fixture("register-aging")
def fx_register_aging(d, passing):
    (d / "docs/architecture").mkdir(parents=True)
    (d / "tools/roadmap-dashboard").mkdir(parents=True)
    (d / "tools/roadmap-dashboard/roadmap.json").write_text(
        json.dumps({"items": [{"id": "v5", "kind": "version", "status": "shipped"}]}))
    status = "\U0001F7E2" if passing else "\U0001F534"
    (d / "docs/architecture/engine-questions.md").write_text(
        f"| Q9 | q | why | v5 | {status} |\n")

@fixture("suite-count-claims")
def fx_suite_counts(d, passing):
    (d / "docs").mkdir(); (d / "build").mkdir()
    (d / "build/last-test-run.log").write_text("Summary: 264 tests, 0 failed\n")
    n = "264" if passing else "227"
    (d / "docs/manual.md").write_text(f"Expected: `Summary: {n} tests, 0 failed`\n")
```

- [ ] **Step 2: Self-test + real-repo session run**

Run: `tools/drift-check --self-test && ./build/tests/k2000_tests 2>/dev/null | tee build/last-test-run.log | tail -1 && tools/drift-check --session`
Expected: `self-test: 9 checks, 0 problems`; session tier prints ok/skip/warn lines, exit 0 (warns don't fail).

- [ ] **Step 3: Commit**

```bash
git add tools/drift-check
git commit -m "feat(drift): session-tier checks (build type, handoff age, register aging) + suite-count check" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 4: Pre-commit hook + hooks-installed session check

**Files:**
- Create: `.githooks/pre-commit` (mode 755)
- Modify: `tools/drift-check` (one more session check)

- [ ] **Step 1: Create the hook**

`.githooks/pre-commit`:

```bash
#!/bin/sh
# k2000 anti-drift gate. Bypass (sparingly): git commit --no-verify — CI is the backstop.
exec "$(git rev-parse --show-toplevel)/tools/drift-check" --commit
```

`chmod +x .githooks/pre-commit`, then install: `git config core.hooksPath .githooks`.

- [ ] **Step 2: Add the hooks-installed check**

```python
@check("hooks-installed", {"session"}, "warn")
def chk_hooks(root, ctx):
    """core.hooksPath must point at .githooks so the commit gate actually fires."""
    if not (root / ".githooks/pre-commit").exists():
        return "skip", "no .githooks yet"
    r = subprocess.run(["git", "-C", str(root), "config", "core.hooksPath"],
                       capture_output=True, text=True)
    if r.stdout.strip() != ".githooks":
        return "warn", "hooks not installed: run  git config core.hooksPath .githooks"
    return "ok", "pre-commit gate installed"
```

Fixture (self-test): git-dependent — fixture creates the file and a throwaway `git init` repo:

```python
@fixture("hooks-installed")
def fx_hooks(d, passing):
    subprocess.run(["git", "init", "-q", str(d)], check=True)
    (d / ".githooks").mkdir(); (d / ".githooks/pre-commit").write_text("#!/bin/sh\n")
    if passing:
        subprocess.run(["git", "-C", str(d), "config", "core.hooksPath", ".githooks"], check=True)
```

- [ ] **Step 3: Verify the hook fires**

Run: `tools/drift-check --self-test` → `10 checks, 0 problems`.
Then prove the gate: `touch DRIFTBAIT.md && git add DRIFTBAIT.md && git commit -m x` — expected: **commit blocked** with `[FAIL] root-md-litter: stray root markdown: DRIFTBAIT.md ...`. Clean up: `git restore --staged DRIFTBAIT.md && rm DRIFTBAIT.md`.

- [ ] **Step 4: Commit**

```bash
git add .githooks/pre-commit tools/drift-check
git commit -m "feat(drift): pre-commit gate + hooks-installed session check" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 5: Patch fingerprints (5 patches × 4 OS tiers, goldened)

**Files:**
- Create: `tests/RenderFingerprintTests.cpp`
- Modify: `tests/CMakeLists.txt` (add after `VoicePerfTests.cpp`)
- Create (generated): `tests/golden/fingerprints/baseline.csv`

**Interfaces:**
- Consumes: `Layer`/`Voice` production path (as `VoicePerfTests` does), `testdsp::{Spectrum,Level,Metrics}`, `testdsp::GoldenSet(name).check(t, key, value, tol)` + `flush()`, `BERNIE_UPDATE_GOLDEN`.
- Produces: golden keys `fp/<patch>/os<f>/band<k>` (k=0..9), `.../peak_dbfs`, `.../thd_db`.

- [ ] **Step 1: Write the test**

Create `tests/RenderFingerprintTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"
#include "testdsp/Spectrum.h"
#include "testdsp/Level.h"
#include "testdsp/Metrics.h"
#include "testdsp/GoldenIO.h"
#include <cmath>
#include <vector>

// Sound/voicing drift fingerprints (anti-drift spec §5): five reference patches
// covering every load-bearing signal path, rendered through the REAL production
// Voice::render at every OS factor, reduced to band-level signatures and
// goldened. Tolerances are musical, not sample-exact, so denormal-grade noise
// passes and audible drift fails. Intentional voicing changes regenerate via
// BERNIE_UPDATE_GOLDEN=1 (the golden diff is the audit trail; failures name
// patch + tier + metric).

namespace {

struct Patch { const char* name; ParamSnapshot s; };

ParamSnapshot base() {
    ParamSnapshot s {};
    s.oscWaveform = 0;                 // saw
    s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
    s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;
    s.algorithmId = 1;                 // "thru" — isolate the spine unless a patch says otherwise
    s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
    s.spineModel = 0; s.spineSlope = 1;
    return s;
}

std::vector<Patch> patches() {
    std::vector<Patch> v;
    { auto s = base(); v.push_back({ "init_saw", s }); }
    { auto s = base(); s.svfCutoffHz = 1200.0f; s.svfResonance = 0.7f; s.spineDrive = 0.5f;
      v.push_back({ "hug_lp24_drive", s }); }
    { auto s = base(); s.svfCutoffHz = 800.0f; s.svfResonance = 0.4f;
      s.huggettRouting = 7;            // "LP+HP" (see Parameters.cpp choice order)
      s.spineSeparationOct = 2.0f;
      v.push_back({ "hug_lp_hp_sep", s }); }
    { auto s = base(); s.spineModel = 1; s.moogMode = 0;
      s.svfCutoffHz = 900.0f; s.svfResonance = 0.8f;
      s.moogBassAmount = 0.7f; s.moogBassWave = 0; s.moogBassOctave = 1;
      v.push_back({ "moog_lp24_bass", s }); }
    { auto s = base(); s.algorithmId = 0;           // "shaper" in the graph
      s.wsDrive = 0.6f; s.wsMix = 1.0f;
      s.hpCutoffHz = 500.0f; s.hpSlope = 1;
      s.svfCutoffHz = 5000.0f; s.svfResonance = 0.2f;
      v.push_back({ "hp_shaper", s }); }
    return v;
}

} // namespace

struct RenderFingerprintTests : public juce::UnitTest {
    RenderFingerprintTests() : juce::UnitTest("RenderFingerprint") {}

    void runTest() override {
        const double sr = 48000.0;
        const int    N  = 512;
        const int    W  = 1 << 15;          // 32768-sample analysis window (last ~0.68 s)
        const int    blocks = (W / N) + 32; // warm-up + window

        testdsp::GoldenSet gs("fingerprints/baseline");

        for (const auto& p : patches()) {
            for (int os : { 1, 2, 4, 8 }) {
                beginTest(juce::String(p.name) + " @ os" + juce::String(os));

                Layer layer;
                layer.prepare(sr * os, N * os);
                layer.updateParameters(p.s);
                Voice v;
                v.setLayer(&layer);
                v.prepare(sr, N, os);
                v.noteOn(57, 1.0f);         // A3, 110 Hz

                std::vector<float> cap; cap.reserve((size_t) (blocks * N));
                std::vector<float> l((size_t) N), r((size_t) N);
                for (int b = 0; b < blocks; ++b) {
                    std::fill(l.begin(), l.end(), 0.0f);
                    std::fill(r.begin(), r.end(), 0.0f);
                    v.render(l.data(), r.data(), N);
                    cap.insert(cap.end(), l.begin(), l.end());
                }
                std::vector<float> win(cap.end() - W, cap.end());
                expect(testdsp::Metrics::finite(win), "render finite");

                auto mag = testdsp::Spectrum::magnitude(win);
                const juce::String kb = "fp/" + juce::String(p.name) + "/os" + juce::String(os);

                // 10 octave bands centered 31.25 Hz .. 16 kHz; Parseval band RMS in dBFS.
                for (int band = 0; band < 10; ++band) {
                    const double fc = 31.25 * std::pow(2.0, band);
                    const int b0 = std::max(1, (int) std::floor(fc / std::sqrt(2.0) * W / sr));
                    const int b1 = std::min((int) mag.size() - 1, (int) std::ceil(fc * std::sqrt(2.0) * W / sr));
                    double acc = 0.0;
                    for (int b = b0; b <= b1; ++b) acc += 2.0 * (double) mag[(size_t) b] * mag[(size_t) b];
                    const double rmsDb = 20.0 * std::log10(std::max(std::sqrt(acc) / (double) W, 1.0e-9));
                    gs.check(*this, kb + "/band" + juce::String(band), rmsDb, 0.5);
                }
                gs.check(*this, kb + "/peak_dbfs", testdsp::Level::peakDbfs(win), 0.25);
                const int fundBin = (int) std::lround(110.0 * W / sr);   // ~75
                gs.check(*this, kb + "/thd_db",
                         testdsp::Metrics::thdPlusNDb(mag, fundBin), 1.0);
            }
        }
        gs.flush();
    }
};

static RenderFingerprintTests renderFingerprintTestsInstance;
```

Add `RenderFingerprintTests.cpp` to `tests/CMakeLists.txt` immediately after `VoicePerfTests.cpp`.

- [ ] **Step 2: Run to verify it fails (missing goldens)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "RenderFingerprint|Summary:" | tail -5`
Expected: `RenderFingerprint` blocks FAIL (no committed baseline), suite reports failures.

- [ ] **Step 3: Generate the goldens, inspect, verify green**

Run: `mkdir -p tests/golden/fingerprints && BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests 2>/dev/null | grep Summary:`
Then: inspect `git status tests/golden/` (expect only `fingerprints/baseline.csv`, 240 rows) and sanity-read a few values (band RMS finite dB, peaks < 0 dBFS).
Run again without the env: `./build/tests/k2000_tests 2>/dev/null | grep -E "RenderFingerprint:|Summary:" | tail -3`
Expected: all `RenderFingerprint` blocks PASS; `Summary: ... 0 failed`.

- [ ] **Step 4: Prove it catches a voicing nudge**

Temporarily multiply the spine output by 1.07 (~+0.6 dB) inside `Voice::render`'s env loop (`outL[i] += baseL_[i] * env * 1.07f;`), rebuild, run — expected: fingerprint FAILs naming patches + tiers. Revert, rebuild, confirm green. (Spec §9 success criterion.)

- [ ] **Step 5: Commit**

```bash
git add tests/RenderFingerprintTests.cpp tests/CMakeLists.txt tests/golden/fingerprints/baseline.csv
git commit -m "feat(drift): patch fingerprints — 5 reference patches x 4 OS tiers, goldened" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 6: Linux CI — drift + sanitizer jobs

**Files:**
- Create: `.github/workflows/drift.yml`

- [ ] **Step 1: Write the workflow**

```yaml
name: Drift

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  workflow_dispatch:

jobs:
  drift:
    name: drift-check + suite (linux)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: JUCE linux deps
        run: sudo apt-get update && sudo apt-get install -y libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev
      - name: Configure (Release)
        run: cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
      - name: Build tests
        run: cmake --build build --target k2000_tests -j4
      - name: Run suite (log for drift-check)
        run: ./build/tests/k2000_tests 2>/dev/null | tee build/last-test-run.log | tail -3
      - name: Drift self-test
        run: tools/drift-check --self-test
      - name: Drift CI tier
        run: tools/drift-check --ci --suite-log build/last-test-run.log

  sanitizers:
    name: ASan+UBSan suite (linux)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: JUCE linux deps
        run: sudo apt-get update && sudo apt-get install -y libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev
      - name: Configure (ASan/UBSan)
        run: cmake -B build-asan -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
      - name: Build
        run: cmake --build build-asan --target k2000_tests -j4
      - name: Run suite under sanitizers
        run: ./build-asan/tests/k2000_tests 2>&1 | tail -3
```

- [ ] **Step 2: Verify locally what CI will run**

Run: `./build/tests/k2000_tests 2>/dev/null | tee build/last-test-run.log | tail -1 && tools/drift-check --ci --suite-log build/last-test-run.log && tools/drift-check --self-test`
Expected: suite 0 failed; `--ci` exit 0 (all ok/warn); self-test 0 problems.

- [ ] **Step 3: Commit (workflow validates on push of the PR)**

```bash
git add .github/workflows/drift.yml
git commit -m "ci(drift): linux drift-check + sanitizer suite workflow" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 7: CLAUDE.md — the agent contract

**Files:**
- Create: `CLAUDE.md`

- [ ] **Step 1: Write the contract** (rails only; pointers for status)

```markdown
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
- The synth is **Bernie**, FX section **Ricky**; repo codename stays k2000.
- Anchor references: Novation Summit + Kurzweil K2061/K2088 ONLY (K2000 dropped).
```

- [ ] **Step 2: Verify drift-check still clean + suite unaffected**

Run: `tools/drift-check --commit` — expected: FAIL on `root-md-litter` (CLAUDE.md at root)! **Amend the check's allowlist**: `ALLOWED_ROOT_MD = {"README.md", "CLAUDE.md"}` in `tools/drift-check`, re-run self-test + `--commit` → clean. (The gate biting its own PR is the system working.)

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md tools/drift-check
git commit -m "docs(drift): CLAUDE.md agent contract (+ allowlist it at root)" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage:** §3 architecture → Tasks 1–4, 6; §4 checks 1–10 → Tasks 1 (#3/#5/#9), 2 (#1/#4), 3 (#2/#6/#7/#8), 5 (#10); §5 fingerprints all tiers → Task 5; §6 CLAUDE.md → Task 7; §7 CI both jobs → Task 6; §8 self-test + skip-not-ok → Task 1 engine; §9 success criteria → Task 4 Step 3 (gate bites), Task 5 Step 4 (voicing nudge), Task 6 Step 2 (<2 s tiers are trivially met — pure filesystem). Hook-installed check (Task 4) is an addition beyond spec §4 (session warn) — consistent with its intent.
**2. Placeholder scan:** none; all code complete; commands with expected results.
**3. Type consistency:** `Check(name, tiers, severity, fn)` + `(status, msg)` contract consistent across Tasks 1–4; fixture signature `fx(dir, passing)` consistent; golden keys `fp/<patch>/os<f>/...` consistent between Task 5 code and commit message; `--suite-log` flows Task 1 CLI → Task 3 check → Task 6 workflow.

---

## Execution Handoff

1. **Subagent-Driven** — fresh subagent per task.
2. **Inline Execution** — this session, checkpoints per task (the mode used for M4; spend-limit rule).
