# Franklin Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Version:** 5.16 (artifact) · **Spec:** `docs/superpowers/specs/2026-07-03-franklin-dashboard-design.md` (v5.15) · **Branch:** `feat/franklin-dashboard` (exists; spec + Q28 committed)

**Goal:** Franklin — live runs + full archive + deviations + browser run control on the roadmap-dashboard, fed by NDJSON runlogs from the C++ suite and characterization runner.

**Architecture:** C++ producers append NDJSON events (start/progress/test/end) to `.franklin/runs/`; the plain-Node dashboard server scans/serves them, spawns/stops whitelisted runs, and polls `gh` for CI; a new Franklin tab renders live cards, CI strip, archive, and a deviations-first run detail. A ~290-entry test catalog explains every suite test; a drift-check rule keeps it complete.

**Tech Stack:** C++17 + JUCE (producers) · Node 24 native TS + `node --test` + esbuild (dashboard, **no new npm deps**) · Python stdlib (drift-check).

## Global Constraints

- Builds ALWAYS `-j4`; suite run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests | tee build/last-test-run.log`.
- `CharacterizationRunner` is **untouched**. Consequence (spec §4 amendment): `progress` events carry NO per-point metrics — the Progress sink signature is `(done, total, label)` only; all numbers ride `end.checks[]` from the summary. The deviations panel is fully served by `end.checks` + suite failure messages.
- Suite-visible changes keep `docs/filter-validation/README.md` expected-count + `build/last-test-run.log` aligned (drift-check `suite-count-claims`).
- Suite test identity = `unitTestName` + `subcategoryName` (one JUCE result per `beginTest`; 290 today). Catalog keys: `"<unitTestName> / <subcategoryName>"`.
- Runlog is best-effort: a write error disables it for the run; it must never fail a measurement. Env: `BERNIE_NO_RUNLOG=1`, `BERNIE_RUNLOG_DIR` (default `./.franklin/runs`).
- Dashboard: no new dependencies; templates whitelist only (never arbitrary commands); server binds localhost; archives are never deleted (compaction only).
- The chz binary takes `--model moog|huggett|all` (flag, NOT positional) and `--quick`.
- New docs carry `Version:` fields (charter 5.17, dashboard manual 5.18).

**Sanctioned amendments (recorded during execution):**
1. `progress` events carry NO per-point metrics (Progress sink is `(done,total,label)`; runner untouchable) — all numbers ride `end.checks[]` + suite failure messages.
2. CI cache TTL split: success 60 s / failure 10 s (review-directed; transient gh flakes recover fast).
3. Archive kind/outcome filters (spec §8.4) dropped from the v1 UI — the single newest-first table + deviations-first detail covers triage; revisit if the archive grows unwieldy (final whole-branch review, 2026-07-05).

---

### Task 1: runlog::Writer (C++ producer core)

**Files:**
- Create: `tests/testdsp/RunLog.h`, `tests/testdsp/RunLog.cpp`, `tests/RunLogTests.cpp`
- Modify: `tests/CMakeLists.txt` (RunLog.cpp + RunLogTests.cpp into `k2000_tests`; RunLog.cpp into `k2000_device_characterization`), `.gitignore` (+`.franklin/`), `docs/filter-validation/README.md` (count 290 → 291)

**Interfaces (Produces):**
```cpp
namespace runlog {
juce::String jsonEscape(const juce::String& s);          // \" \\ \n \r \t and control chars -> \u00XX
class Writer {
public:
    explicit Writer(const juce::String& kind);            // "chz" | "suite"; opens <dir>/<utc yyyymmdd-hhmmss>-<kind>-<pid>.ndjson
    bool enabled() const;
    void start(const juce::StringArray& argv, const juce::String& model = {},
               const juce::String& grid = {}, int total = -1);
    void progress(int done, int total, const juce::String& label);   // throttled: >=1s gap; >=10s after 3600s of run; done==total always written
    void test(const juce::String& name, const juce::String& sub, int passes,
              int failures, const juce::StringArray& messages);
    struct Check { juce::String name; double measured;
                   double expected = std::numeric_limits<double>::quiet_NaN();  // NaN = info-only
                   juce::String verdict; };                // "pass" | "fail" | "info"
    void end(const juce::String& outcome, double durationS,
             const std::vector<Check>& checks = {}, int tests = -1, int failed = -1);
    // test seams:
    Writer(const juce::String& kind, const juce::File& dir, int64_t throttleMs, int64_t slowAfterMs);
    juce::File file() const;
private:
    void line(const juce::String& jsonObj);                // append + flush; on failure enabled_=false
    juce::File file_; bool enabled_ = true;
    int64_t t0Ms_ = 0, lastMs_ = 0, throttleMs_ = 1000, slowAfterMs_ = 3600 * 1000;
};
}
```
Event JSON (exact keys, one object per line): `{"ev":"start","ts":<epoch-ms>,"kind":"suite","argv":["…"],"pid":123,"buildType":"Release","gitSha":"<env BERNIE_GIT_SHA if set>","model":"…","grid":"…","total":29088}` (model/grid/total/gitSha omitted when empty/-1/unset) · `{"ev":"progress","ts":…,"done":9,"total":126,"label":"…"}` · `{"ev":"test","ts":…,"name":"…","sub":"…","ok":true,"passes":3,"failures":0,"messages":[]}` · `{"ev":"end","ts":…,"outcome":"pass","durationS":12.3,"tests":291,"failed":0,"checks":[{"name":"…","measured":0.42,"expected":1.0,"verdict":"pass"}]}`. `buildType` from `#ifdef NDEBUG` → "Release" else "Debug".

- [ ] **Step 1: Write the failing test** — `tests/RunLogTests.cpp` with ONE `beginTest` section (keeps the suite count math = +1):

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/RunLog.h"

class RunLogTests : public juce::UnitTest {
public:
    RunLogTests() : juce::UnitTest("RunLog") {}
    void runTest() override {
        beginTest("writer emits well-formed throttled NDJSON and disables cleanly");

        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("runlog-test-" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
        dir.createDirectory();

        {   // throttle 0 = write everything; slowAfter huge
            runlog::Writer w("suite", dir, 0, 1 << 30);
            expect(w.enabled());
            w.start(juce::StringArray{"k2000_tests"});
            w.progress(1, 2, "half");
            w.test("RunLog", "escaping \"quotes\"\nnewline", 3, 1, juce::StringArray{"expected 1\n but was 2"});
            w.end("fail", 0.5, {}, 291, 1);

            juce::StringArray lines;
            lines.addLines(w.file().loadFileAsString().trim());
            expectEquals(lines.size(), 4);
            auto parsed = juce::JSON::parse(lines[2]);          // the test event
            expectEquals(parsed.getProperty("ev", "").toString(), juce::String("test"));
            expectEquals(parsed.getProperty("sub", "").toString(), juce::String("escaping \"quotes\"\nnewline"));
            expect(!static_cast<bool>(parsed.getProperty("ok", true)));
            expectEquals(parsed.getProperty("messages", juce::var()).getArray()->getReference(0).toString(),
                         juce::String("expected 1\n but was 2"));
            auto endEv = juce::JSON::parse(lines[3]);
            expectEquals(endEv.getProperty("outcome", "").toString(), juce::String("fail"));
            expectEquals(static_cast<int>(endEv.getProperty("tests", 0)), 291);
        }
        {   // throttle: huge gap -> mid progress dropped, final done==total kept
            runlog::Writer w("chz", dir, 1 << 30, 1 << 30);
            w.start(juce::StringArray{"chz"}, "moog", "quick", 4);
            w.progress(1, 4, "a"); w.progress(2, 4, "b"); w.progress(4, 4, "done");
            juce::StringArray lines;
            lines.addLines(w.file().loadFileAsString().trim());
            expectEquals(lines.size(), 3);                       // start + first progress + final
            expect(lines[2].contains("\"done\":4"));
        }
        {   // BERNIE_NO_RUNLOG disables (POSIX setenv — tests run on Linux locally and in CI;
            // the Windows CI job builds but ctest runs the same code path via _putenv is NOT
            // needed: guard with #ifndef _WIN32 and expect(true) on Windows)
#ifndef _WIN32
            ::setenv("BERNIE_NO_RUNLOG", "1", 1);
            runlog::Writer w("suite", dir, 0, 1 << 30);
            expect(!w.enabled());
            ::unsetenv("BERNIE_NO_RUNLOG");
#endif
        }
        dir.deleteRecursively();
    }
};
static RunLogTests runLogTests;
```
(If `juce::SystemStats::setEnvironmentVariable` doesn't exist in JUCE 8, use `::setenv("BERNIE_NO_RUNLOG","1",1)` / `::unsetenv` — POSIX is fine, tests run on Linux/CI both.)

- [ ] **Step 2: Add files to CMake, run test to verify it fails**

In `tests/CMakeLists.txt` (all paths are relative to `tests/`): add `RunLogTests.cpp` after `RunnerProgressTests.cpp` and `testdsp/RunLog.cpp` at the end of the `add_executable(k2000_tests …)` list; add `testdsp/RunLog.cpp` to the `add_executable(k2000_device_characterization …)` list as well.

Run: `cmake --build build --target k2000_tests -j4 2>&1 | tail -5` → compile FAILS (`RunLog.h: No such file`). That is the failing state; now implement.

- [ ] **Step 3: Implement `RunLog.h`/`RunLog.cpp`**

`RunLog.h`: the interface block above verbatim (plus `#pragma once`, includes `<juce_core/juce_core.h>`, `<vector>`, `<limits>`, `<cstdint>`).

`RunLog.cpp` core (complete):
```cpp
#include "RunLog.h"
namespace runlog {

juce::String jsonEscape(const juce::String& s) {
    juce::String out;
    for (auto t = s.getCharPointer(); !t.isEmpty(); ++t) {
        auto c = *t;
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (c < 0x20) out << "\\u" + juce::String::toHexString((int) c).paddedLeft('0', 4);
                else          out << juce::String::charToString(c);
        }
    }
    return out;
}

static juce::File defaultDir() {
    if (const char* d = std::getenv("BERNIE_RUNLOG_DIR"))
        return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(d));
    return juce::File::getCurrentWorkingDirectory().getChildFile(".franklin/runs");
}

Writer::Writer(const juce::String& kind) : Writer(kind, defaultDir(), 1000, 3600 * 1000) {}

Writer::Writer(const juce::String& kind, const juce::File& dir, int64_t throttleMs, int64_t slowAfterMs)
    : throttleMs_(throttleMs), slowAfterMs_(slowAfterMs) {
    if (std::getenv("BERNIE_NO_RUNLOG") != nullptr && juce::String(std::getenv("BERNIE_NO_RUNLOG")) == "1") { enabled_ = false; return; }
    dir.createDirectory();
    t0Ms_ = juce::Time::currentTimeMillis();
    auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    file_ = dir.getChildFile(stamp + "-" + kind + "-" + juce::String((int) juce::SystemStats::getProcessIdentifier()) + ".ndjson");
    if (!file_.create().wasOk()) enabled_ = false;
}

bool Writer::enabled() const { return enabled_; }
juce::File Writer::file() const { return file_; }

void Writer::line(const juce::String& jsonObj) {
    if (!enabled_) return;
    if (!file_.appendText(jsonObj + "\n", false, false, "\n")) enabled_ = false;   // best-effort
}

void Writer::start(const juce::StringArray& argv, const juce::String& model,
                   const juce::String& grid, int total) {
    if (!enabled_) return;
    juce::String a; for (const auto& s : argv) a << (a.isEmpty() ? "" : ",") << "\"" << jsonEscape(s) << "\"";
#ifdef NDEBUG
    const char* bt = "Release";
#else
    const char* bt = "Debug";
#endif
    juce::String j;
    j << "{\"ev\":\"start\",\"ts\":" << juce::Time::currentTimeMillis()
      << ",\"kind\":\"" << jsonEscape(file_.getFileName().contains("-suite-") ? "suite" : "chz")
      << "\",\"argv\":[" << a << "],\"pid\":" << (int) juce::SystemStats::getProcessIdentifier()
      << ",\"buildType\":\"" << bt << "\"";
    if (const char* sha = std::getenv("BERNIE_GIT_SHA")) j << ",\"gitSha\":\"" << jsonEscape(sha) << "\"";
    if (model.isNotEmpty()) j << ",\"model\":\"" << jsonEscape(model) << "\"";
    if (grid.isNotEmpty())  j << ",\"grid\":\""  << jsonEscape(grid)  << "\"";
    if (total >= 0)         j << ",\"total\":"   << total;
    j << "}";
    line(j);
}

void Writer::progress(int done, int total, const juce::String& label) {
    if (!enabled_) return;
    const auto now = juce::Time::currentTimeMillis();
    const auto gap = (now - t0Ms_ > slowAfterMs_) ? throttleMs_ * 10 : throttleMs_;
    if (done != total && now - lastMs_ < gap) return;
    lastMs_ = now;
    juce::String j;
    j << "{\"ev\":\"progress\",\"ts\":" << now << ",\"done\":" << done
      << ",\"total\":" << total << ",\"label\":\"" << jsonEscape(label) << "\"}";
    line(j);
}

void Writer::test(const juce::String& name, const juce::String& sub, int passes,
                  int failures, const juce::StringArray& messages) {
    if (!enabled_) return;
    juce::String m; for (const auto& s : messages) m << (m.isEmpty() ? "" : ",") << "\"" << jsonEscape(s) << "\"";
    juce::String j;
    j << "{\"ev\":\"test\",\"ts\":" << juce::Time::currentTimeMillis()
      << ",\"name\":\"" << jsonEscape(name) << "\",\"sub\":\"" << jsonEscape(sub)
      << "\",\"ok\":" << (failures == 0 ? "true" : "false")
      << ",\"passes\":" << passes << ",\"failures\":" << failures
      << ",\"messages\":[" << m << "]}";
    line(j);
}

void Writer::end(const juce::String& outcome, double durationS,
                 const std::vector<Check>& checks, int tests, int failed) {
    if (!enabled_) return;
    juce::String c;
    for (const auto& ch : checks) {
        c << (c.isEmpty() ? "" : ",") << "{\"name\":\"" << jsonEscape(ch.name)
          << "\",\"measured\":" << juce::String(ch.measured, 6);
        if (!std::isnan(ch.expected)) c << ",\"expected\":" << juce::String(ch.expected, 6);
        c << ",\"verdict\":\"" << jsonEscape(ch.verdict) << "\"}";
    }
    juce::String j;
    j << "{\"ev\":\"end\",\"ts\":" << juce::Time::currentTimeMillis()
      << ",\"outcome\":\"" << jsonEscape(outcome) << "\",\"durationS\":" << juce::String(durationS, 3);
    if (tests >= 0)  j << ",\"tests\":" << tests;
    if (failed >= 0) j << ",\"failed\":" << failed;
    j << ",\"checks\":[" << c << "]}";
    line(j);
}
} // namespace runlog
```

- [ ] **Step 4: Build + run, verify pass, verify count**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests | tee build/last-test-run.log | tail -2`
Expected: `Summary: 291 tests, 0 failed`. Update `docs/filter-validation/README.md` expected line to 291. `tools/drift-check --session` → all OK.

- [ ] **Step 5: Commit**
```bash
git add tests/testdsp/RunLog.h tests/testdsp/RunLog.cpp tests/RunLogTests.cpp tests/CMakeLists.txt .gitignore docs/filter-validation/README.md
git commit -m "feat(franklin): runlog::Writer NDJSON producer core + tests"
```

---

### Task 2: Suite tap in TestMain

**Files:** Modify: `tests/TestMain.cpp`

**Interfaces (Consumes):** `runlog::Writer` from Task 1. **Produces:** every suite run appends a `*-suite-*.ndjson` with start + one `test` event per beginTest section (`name`=unitTestName, `sub`=subcategoryName, failure `messages` from JUCE) + end{outcome, tests, failed}. This file is the name source for the catalog (Task 3) and drift rule (Task 4).

- [ ] **Step 1: Implement the tap** (JUCE result objects accumulate `messages` as failures happen; a result is complete once the run finishes — emit all in the post-run loop, which keeps ordering exact and needs no subclass):

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/RunLog.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    runlog::Writer log("suite");
    { juce::StringArray a; for (int i = 0; i < argc; ++i) a.add(argv[i]); log.start(a); }
    const auto t0 = juce::Time::currentTimeMillis();

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failedTests = 0, totalTests = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        const auto* r = runner.getResult(i);
        totalTests++;
        failedTests += r->failures;
        log.test(r->unitTestName, r->subcategoryName, r->passes, r->failures, r->messages);
        std::printf("[%s] %s: %d passes, %d failures\n",
            r->failures == 0 ? "PASS" : "FAIL",
            r->unitTestName.toRawUTF8(), r->passes, r->failures);
    }
    std::printf("\nSummary: %d tests, %d failed\n", totalTests, failedTests);
    log.end(failedTests > 0 ? "fail" : "pass",
            (juce::Time::currentTimeMillis() - t0) / 1000.0, {}, totalTests, failedTests);
    return failedTests > 0 ? 1 : 0;
}
```
Note: per-test events are written after the run (a 2-minute suite needs no mid-run liveness; the dashboard shows suite runs via start + end + tests). The stdout format is byte-identical to before — drift-check's count parser is untouched.

- [ ] **Step 2: Build, run, verify the runlog**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests | tee build/last-test-run.log | tail -2`
Expected: `Summary: 291 tests, 0 failed`, and:
```bash
ls .franklin/runs/ | tail -1                                  # <stamp>-suite-<pid>.ndjson
grep -c '"ev":"test"' .franklin/runs/$(ls .franklin/runs | tail -1)   # 291
```

- [ ] **Step 3: Verify BERNIE_NO_RUNLOG**: `BERNIE_NO_RUNLOG=1 ./build/tests/k2000_tests | tail -1` → same summary, no new file in `.franklin/runs`.

- [ ] **Step 4: Commit** — `git add tests/TestMain.cpp && git commit -m "feat(franklin): suite tap — per-test runlog events with failure messages"`

---

### Task 3: chz sink + end.checks in characterize_main

**Files:** Modify: `tests/characterization/characterize_main.cpp`

**Interfaces (Consumes):** `runlog::Writer`. **Produces:** `*-chz-*.ndjson` per invocation: start{model, grid, total? (omitted; totals arrive via progress)}, throttled progress events, end{outcome, checks[]} where checks = every summary key as `{name, measured, verdict:"info"}` EXCEPT `*method_delta_db` keys which get `expected:1.0` and verdict pass/fail — mirroring the binary's real gate.

- [ ] **Step 1: Implement.** In `main()`: create `runlog::Writer log("chz");` before the model loop; `log.start(argvArray, model, quick ? "quick" : "full");`. In `runOne(...)` (pass `runlog::Writer&` as a parameter): wrap the progress lambda —

```cpp
    chz::CharacterizationRunner::Progress progress =
        [&](int done, int total, const juce::String& label) {
            if (showProgress) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t0).count();
                const double eta = done > 0 ? elapsed / done * (total - done) : 0.0;
                std::fprintf(stderr, "\r[%s] %d/%d (%d%%)  %5.0fs elapsed  eta %5.0fs  %-60.60s",
                             model.toRawUTF8(), done, total, (int) ((100LL * done) / total),
                             elapsed, eta, label.toRawUTF8());
                if (done == total) std::fputc('\n', stderr);
                std::fflush(stderr);
            }
            log.progress(done, total, "[" + model + "] " + label);
        };
```
(the lambda is now unconditional; the stderr half keeps its `showProgress` gate, the runlog half is gated internally by the Writer). After the summary loop in `runOne`, build checks and return them to `main` (change `runOne` to also fill a `std::vector<runlog::Writer::Check>&` out-param):
```cpp
    for (const auto& kv : summary) {
        if (std::abs(kv.second - (-1.0)) < 1.0e-9) continue;
        runlog::Writer::Check c; c.name = "[" + model + "] " + kv.first; c.measured = kv.second;
        if (kv.first.endsWith("method_delta_db")) { c.expected = 1.0; c.verdict = kv.second < 1.0 ? "pass" : "fail"; }
        else c.verdict = "info";
        checksOut.push_back(c);
    }
```
In `main`, after all models: `log.end(rc == 0 ? "pass" : "fail", elapsedSeconds, allChecks);` (accumulate a `std::vector<runlog::Writer::Check>` across `runOne` calls; time the whole main with the same steady_clock pattern as runOne).

- [ ] **Step 2: Build + live verification (bounded)**

```bash
cmake --build build --target k2000_device_characterization -j4 2>&1 | tail -2
timeout 90 ./build/tests/k2000_device_characterization --model moog --quick; echo "exit=$?"
F=.franklin/runs/$(ls .franklin/runs | grep chz | tail -1)
head -c 300 "$F"; echo; grep -c '"ev":"progress"' "$F"
```
Expected: a `start` line with `"model":"moog","grid":"quick"`; ≥1 progress events with `[moog]`-prefixed labels; if the 90 s timeout kills it, NO end event (that is the stalled case, by design). If the quick run completes on this machine (~minutes), expect the `end` event with `checks[]` including `method_delta_db` entries with `"expected":1.0`.

- [ ] **Step 3: Full-suite sanity + commit**

Run suite (unchanged count 291): `./build/tests/k2000_tests | tail -1`.
```bash
git add tests/characterization/characterize_main.cpp
git commit -m "feat(franklin): chz runlog sink + end.checks from summary (runner untouched)"
```

---

### Task 4: Test catalog — full backfill

**Files:** Create: `docs/franklin/test-catalog.json`

**Interfaces (Produces):** JSON consumed by Task 5 (drift rule) and Task 9/10 (UI): `{ "version": 1, "entries": [ { "key": "<unitTestName> / <subcategoryName>", "file": "tests/….cpp", "what": "…", "why": "…", "deviationMeans": "…", "links": ["…"] } ] }`.

- [ ] **Step 1: Enumerate the 291 exact keys from the fresh suite runlog**

```bash
F=.franklin/runs/$(ls .franklin/runs | grep suite | tail -1)
python3 - "$F" <<'EOF'
import json, sys
keys = []
for line in open(sys.argv[1]):
    try: e = json.loads(line)
    except json.JSONDecodeError: continue
    if e.get("ev") == "test": keys.append(f'{e["name"]} / {e["sub"]}')
print(len(keys))
json.dump({"version": 1, "entries": [{"key": k, "file": "", "what": "", "why": "", "deviationMeans": "", "links": []} for k in keys]},
          open("docs/franklin/test-catalog.json", "w"), indent=1)
EOF
```
Expected: prints `291`; skeleton written.

- [ ] **Step 2: Backfill every entry.** For each entry: locate the `beginTest("<sub>")` in `tests/` (`grep -rn 'beginTest("<sub>' tests/`), read the surrounding test, fill `file`, `what` (what it measures/asserts, 1-2 sentences), `why` (what it guards — name the register Q / spec / incident when one exists), `deviationMeans` (what a failure tells you physically/musically), `links` (doc paths or register IDs like "Q27"). Quality bar — three worked examples to match:

```json
{ "key": "HuggettBoundedResonance / voice at res 1.0 os8 stays bounded",
  "file": "tests/HuggettBoundedResonanceTests.cpp",
  "what": "Renders a full voice at maximum resonance, os8, and asserts the output peak stays below -1 dBFS (was +22 dBFS before the fix).",
  "why": "Guards the Q27 defect fix: the resonance loop anti-damped (transposed satRes operands, no state bound) and blew up to a perceived dropout; only the safety limiter prevented hearing damage.",
  "deviationMeans": "The resonance loop is expanding again — state blow-up / limiter-slamming screech is back; treat as a hearing-safety regression, not a voicing change.",
  "links": ["Q27", "docs/reviews/2026-07-02-huggett-large-signal-read.md"] }
```
```json
{ "key": "RunLog / writer emits well-formed throttled NDJSON and disables cleanly",
  "file": "tests/RunLogTests.cpp",
  "what": "Round-trips runlog NDJSON through juce::JSON::parse (escaping), verifies the 1s/10s progress throttle keeps first+final events, and that BERNIE_NO_RUNLOG=1 disables writing.",
  "why": "Franklin's dashboard trusts these files as its only data source; malformed JSON or unthrottled writes would corrupt or bloat the archive.",
  "deviationMeans": "Runlog files may be unparseable or oversized — the Franklin tab could show garbage or the archive could grow unboundedly.",
  "links": ["docs/superpowers/specs/2026-07-03-franklin-dashboard-design.md"] }
```
```json
{ "key": "Halfband2x / golden equivalence after zero-skip optimization",
  "file": "tests/OversamplingTests.cpp",
  "what": "Asserts the zero-skipping halfband path is sample-identical to the reference dense convolution on golden fixtures.",
  "why": "The Q23 perf optimization (os2 0.51%/voice) is only legal because it is bit-transparent; this is the guard that keeps 'faster' from becoming 'different'.",
  "deviationMeans": "The optimized resampler no longer matches the reference — oversampled tiers are changing the sound, not just the cost.",
  "links": ["Q23"] }
```
(Adjust the third example's key/file to the real emitted name — these illustrate depth, not exact keys.) No entry may be left with empty `what`/`why`/`deviationMeans`.

- [ ] **Step 3: Validate + commit**

```bash
python3 -c "
import json; d = json.load(open('docs/franklin/test-catalog.json'))
bad = [e['key'] for e in d['entries'] if not (e['file'] and e['what'] and e['why'] and e['deviationMeans'])]
print('entries:', len(d['entries']), 'incomplete:', len(bad)); print('\n'.join(bad[:10]))"
```
Expected: `entries: 291 incomplete: 0`.
```bash
git add docs/franklin/test-catalog.json
git commit -m "docs(franklin): full test catalog backfill — what/why/deviationMeans for all 291 suite tests"
```

---

### Task 5: drift-check rule `franklin-catalog`

**Files:** Modify: `tools/drift-check` (one `@check`, one `@fixture`)

**Interfaces (Consumes):** catalog from Task 4; suite runlog from Task 2. **Produces:** `franklin-catalog` in tiers {session, ci}, severity `warn`.

- [ ] **Step 1: Add the check** (place after the existing checks, following the house pattern):

```python
@check("franklin-catalog", {"session", "ci"}, "warn")
def chk_franklin_catalog(root, ctx):
    """Every suite test has a catalog entry and vice versa (Franklin explains every test)."""
    cat_p = root / "docs/franklin/test-catalog.json"
    if not cat_p.exists():
        return "warn", "docs/franklin/test-catalog.json missing"
    try:
        cat = {e["key"] for e in json.loads(cat_p.read_text(encoding="utf-8"))["entries"]}
    except (json.JSONDecodeError, KeyError, TypeError) as e:
        return "warn", f"test-catalog.json unreadable: {e}"
    runs_dir = root / ".franklin/runs"
    suites = sorted(runs_dir.glob("*-suite-*.ndjson")) if runs_dir.exists() else []
    if not suites:
        return "skip", "no suite runlog in .franklin/runs — run the suite once"
    seen = set()
    for line in suites[-1].read_text(encoding="utf-8").splitlines():
        try: e = json.loads(line)
        except json.JSONDecodeError: continue
        if e.get("ev") == "test": seen.add(f'{e["name"]} / {e["sub"]}')
    missing, orphaned = sorted(seen - cat), sorted(cat - seen)
    if missing or orphaned:
        parts = []
        if missing:  parts.append(f"{len(missing)} test(s) without catalog entry: {', '.join(missing[:3])}…")
        if orphaned: parts.append(f"{len(orphaned)} catalog entr(ies) without test: {', '.join(orphaned[:3])}…")
        return "warn", "; ".join(parts)
    return "ok", f"catalog covers all {len(seen)} suite tests"
```

- [ ] **Step 2: Add the self-test fixture** (match the existing `fx(dir, passing)` signature used by the other fixtures):

```python
@fixture("franklin-catalog")
def fx_franklin_catalog(dir, passing):
    runs = dir / ".franklin/runs"; runs.mkdir(parents=True)
    (runs / "20260703-000000-suite-1.ndjson").write_text(
        '{"ev":"test","name":"A","sub":"b","ok":true,"passes":1,"failures":0,"messages":[]}\n', encoding="utf-8")
    entries = [{"key": "A / b", "file": "tests/x.cpp", "what": "w", "why": "y", "deviationMeans": "d", "links": []}] if passing else []
    cat = dir / "docs/franklin"; cat.mkdir(parents=True)
    (cat / "test-catalog.json").write_text(json.dumps({"version": 1, "entries": entries}), encoding="utf-8")
```

- [ ] **Step 3: Verify** — `tools/drift-check --self-test` → all fixtures pass including `franklin-catalog`; `tools/drift-check --session` → `[OK  ] franklin-catalog: catalog covers all 291 suite tests`.

- [ ] **Step 4: Commit** — `git add tools/drift-check && git commit -m "feat(drift): franklin-catalog rule — no suite test without an explanation"`

---

### Task 6: shared Franklin types + server runs module

**Files:**
- Create: `tools/roadmap-dashboard/src/franklinTypes.ts`, `tools/roadmap-dashboard/server/runs.ts`, `tools/roadmap-dashboard/server/runs.test.ts`

**Interfaces (Produces):**
```ts
// src/franklinTypes.ts (shared by server + frontend)
export type RunKind = "chz" | "suite";
export type RunStatus = "running" | "stalled" | "pass" | "fail" | "error" | "stopped";
export interface RunCheck { name: string; measured: number; expected?: number; verdict: "pass" | "fail" | "info"; }
export interface TestEvent { name: string; sub: string; ok: boolean; passes: number; failures: number; messages: string[]; }
export interface ProgressEvent { ts: number; done: number; total: number; label: string; }
export interface RunSummary {
  id: string; kind: RunKind; startedAt: number; status: RunStatus; sizeBytes: number;
  done?: number; total?: number; label?: string; durationS?: number;
  model?: string; grid?: string; tests?: number; failed?: number; pid?: number;
}
export interface RunDetail extends RunSummary {
  argv?: string[]; gitSha?: string; buildType?: string;
  testsList: TestEvent[]; checks: RunCheck[]; progressTail: ProgressEvent[];
}
// server/runs.ts
export const STALL_MS = 120_000;
export function parseRun(id: string, text: string, mtimeMs: number, nowMs: number, sizeBytes: number): { summary: RunSummary; detail: RunDetail };
export function listRuns(dir: string): Promise<RunSummary[]>;             // newest first
export function readRun(dir: string, id: string): Promise<RunDetail | null>;  // id = filename; reject ids with path separators
export function compactFinished(dir: string): Promise<number>;            // returns #files compacted
export const COMPACT_MARKER = '{"ev":"meta","compacted":true}';
export const MAX_PROGRESS_KEPT = 500;
```
Status rules: `end.outcome` present → that outcome; else `nowMs - mtimeMs > STALL_MS` → `stalled`; else `running`. Compaction: only files WITH an `end` event and WITHOUT the marker; rewrite = marker line + start + downsampled progress (evenly indexed to ≤500) + all test lines + end, via `<file>.tmp` + rename. `progressTail` in detail = last 50 progress events. Malformed/truncated lines are skipped everywhere.

- [ ] **Step 1: Write failing tests** — `server/runs.test.ts` (node:test + tmp dirs, same style as `server.test.ts`): cases: (1) parse a complete suite run → status pass, tests/failed populated; (2) parse a chz run w/o end, fresh mtime → running w/ done/total/label from last progress; (3) stale mtime → stalled; (4) truncated final line skipped; (5) `listRuns` orders newest first + sizeBytes set; (6) `compactFinished` shrinks a 2000-progress-line finished file to ≤ 500+marker+start+end and is idempotent; (7) `readRun("../etc/passwd")` → null. Write out real NDJSON fixture strings inline (reuse the event shapes from Task 1).
- [ ] **Step 2: Run** — `cd tools/roadmap-dashboard && npm test` → new tests FAIL (module missing).
- [ ] **Step 3: Implement `runs.ts`** per the interface block (node:fs/promises readdir/stat/readFile/writeFile/rename; pure `parseRun` doing the line loop with try/JSON.parse; `listRuns` maps files `*.ndjson` → parseRun on content).
- [ ] **Step 4: Run tests** — `npm test` → PASS (existing tests stay green).
- [ ] **Step 5: Commit** — `git add src/franklinTypes.ts server/runs.ts server/runs.test.ts && git commit -m "feat(franklin): runs module — scan/parse/status/stall/compaction"`

---

### Task 7: server control module (start/stop/stale-binary)

**Files:** Create: `tools/roadmap-dashboard/server/control.ts`, `tools/roadmap-dashboard/server/control.test.ts`

**Interfaces (Produces):**
```ts
export interface Template { id: string; label: string; bin: string; args: string[]; env: Record<string, string>; }
export function templates(params?: { model?: string; grid?: string }): Template[];  // fixed list; chz args derived from params
export function startRun(rootDir: string, templateId: string, params: { model?: string; grid?: string }):
  Promise<{ ok: true; pid: number } | { ok: false; error: string }>;
export function stopRun(rootDir: string, runsDir: string, id: string):
  Promise<{ ok: true } | { ok: false; error: string }>;
export interface StaleInfo { binary: string; stale: boolean; binaryMtimeMs: number; newestSourceMtimeMs: number; }
export function staleBinaryInfo(rootDir: string): Promise<StaleInfo[]>;
```
Templates (whitelist, exact): `suite` → `build/tests/k2000_tests` env{}; `suite-disparity` → same bin, env `{BERNIE_RUN_DISPARITY:"1"}`; `suite-voiceperf` → env `{BERNIE_RUN_VOICEPERF:"1"}`; `chz` → `build/tests/k2000_device_characterization`, args `["--model", model]` + (`grid==="quick"` ? `["--quick"]` : `[]`), model ∈ {moog, huggett, all}, grid ∈ {quick, full} — anything else → `{ok:false}`. Spawn: `spawn(bin, args, { cwd: rootDir, env: {...process.env, ...t.env, BERNIE_GIT_SHA: <git rev-parse HEAD, best-effort>}, detached: true, stdio: ["ignore", fd, fd] })` with fd = open `<runsDir>/<stamp>-<templateId>.log`; `child.unref()`. Stop: read the run file's `start` event → pid + argv[0] basename; read `/proc/<pid>/cmdline`; if it does not contain the basename → `{ok:false, error:"pid mismatch"}`; else `process.kill(pid, "SIGTERM")`, `setTimeout(5000)` → if `/proc/<pid>` still exists `SIGKILL`; then append `{"ev":"end","ts":…,"outcome":"stopped","durationS":…}` to the run file. Stale check: binaries × mtime vs max mtime over `src/**` + `tests/**` source files (`.h/.cpp/.cmajor`).

- [ ] **Step 1: Write failing tests** — spawn/stop against a fake: template test injects a temp "binary" (`#!/bin/sh\nsleep 30` script) by testing `stopRun` against a run file whose `start` event records a real spawned `sleep`-script pid (write the NDJSON fixture yourself, spawn the script with node, then stopRun must SIGTERM it and append the stopped end event); pid-mismatch test: fixture pid = a live pid whose cmdline is NOT the recorded binary (use `process.pid` with argv0 "k2000_tests" → mismatch → refuse); template validation test: unknown template id and bad model rejected; staleBinaryInfo test on a tmp tree (source newer than binary → stale:true).
- [ ] **Step 2: Run** — `npm test` → FAIL (module missing).
- [ ] **Step 3: Implement `control.ts`** per interface (node:child_process spawn/execFile, node:fs).
- [ ] **Step 4: Run tests** — `npm test` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(franklin): control module — whitelisted spawn, pid-verified stop, stale-binary check"`

---

### Task 8: server CI module + API wiring

**Files:**
- Create: `tools/roadmap-dashboard/server/ci.ts`, `tools/roadmap-dashboard/server/ci.test.ts`
- Modify: `tools/roadmap-dashboard/server/server.ts`, `tools/roadmap-dashboard/server/server.test.ts`

**Interfaces (Produces):**
```ts
// ci.ts
export interface CiCheck { name: string; status: string; conclusion: string | null; url: string; }
export interface CiBranch { ref: string; title: string; checks: CiCheck[]; }
export interface CiPayload { available: boolean; fetchedAt: number; branches: CiBranch[]; }
export function parsePrList(json: string): CiBranch[];        // from: gh pr list --json number,title,headRefName,statusCheckRollup
export function parseRunList(json: string): CiBranch;         // from: gh run list --branch main --limit 5 --json displayTitle,status,conclusion,url,workflowName
export function getCi(rootDir: string): Promise<CiPayload>;   // execFile("gh", …) both commands; 60s in-memory cache; errors -> {available:false}
```
Server routes added to `createServer` (keep existing ones intact): `GET /api/runs` → `{ runs: await listRuns(runsDir), disk: totalBytes }` and fire-and-forget `compactFinished(runsDir)`; `GET /api/runs/<id>` → detail or 404; `GET /api/ci` → `getCi`; `GET /api/control/templates` → `{ templates: templates(), stale: await staleBinaryInfo(rootDir) }`; `POST /api/control/start` body `{templateId, params}`; `POST /api/control/stop` body `{id}`. `runsDir` = `join(rootDir, "../../.franklin/runs")` — add `franklinRunsDir` to `Options` with that default so tests can inject a tmp dir. Also change the boot `listen` call at the bottom of `server.ts` to bind `127.0.0.1` explicitly (spec §10 localhost-only; today's bare `listen(port)` binds all interfaces).

- [ ] **Step 1: Failing tests** — ci.test.ts: `parsePrList`/`parseRunList` against inline fixture JSON copied from real gh output shapes (statusCheckRollup entries `{name, status, conclusion}` — note `__typename`-style variance: tolerate missing conclusion); `getCi` with `gh` absent (point PATH at empty tmp dir) → `{available:false}`. server.test.ts additions: `/api/runs` returns seeded tmp run; `/api/runs/evil%2F..%2Fname` → 404; `/api/control/start` with unknown template → 400.
- [ ] **Step 2: Run** — `npm test` → FAIL.
- [ ] **Step 3: Implement** ci.ts + server.ts wiring per interfaces.
- [ ] **Step 4: Run** — `npm test` → PASS (all suites).
- [ ] **Step 5: Commit** — `git commit -m "feat(franklin): CI poller + /api/runs|ci|control endpoints"`

---

### Task 9: explanations module (catalog + battery templates)

**Files:** Create: `tools/roadmap-dashboard/src/franklinExplain.ts`, `tools/roadmap-dashboard/src/franklinExplain.test.ts`; Modify: `tools/roadmap-dashboard/server/server.ts` (serve `GET /api/catalog` → the JSON file at `docs/franklin/test-catalog.json`, path from a new `Options.catalogPath` default `join(rootDir, "../../docs/franklin/test-catalog.json")`; 200 `{version:0,entries:[]}` when absent)

**Interfaces (Produces):**
```ts
export interface CatalogEntry { key: string; file: string; what: string; why: string; deviationMeans: string; links: string[]; }
export function catalogLookup(entries: CatalogEntry[], name: string, sub: string): CatalogEntry | null;   // key = `${name} / ${sub}`
export function explainChzLabel(label: string): { title: string; body: string };
```
`explainChzLabel` parses labels shaped `[model] model/MODE/fcN B1 res0.90 drv0.00 os4 live 96000` (tolerate the leading `[model] ` prefix added by Task 3, and unknown shapes → `{title: label, body: "Unrecognized operating-point label."}`). Battery templates (exact strings, keyed by the `B1`…`B4` token):
- B1: title `Magnitude response (dual-method)`, body: `Measures the frequency response of <MODEL> <MODE> at cutoff <FC> Hz (res <RES>, drive <DRV>, <OS>x oversampling, <RATE> Hz, <live|render>). Two independent methods — stepped-sine and ESS deconvolution — must agree within 1 dB; disagreement means the measurement itself cannot be trusted (see docs/filter-validation/acceptance-criterion.md).`
- B2: title `Resonance & self-oscillation`, body: `At maximum resonance, measures the self-oscillation pitch against the commanded cutoff (±3% gate below ~4 kHz per the ratified standard) and the resonant peak behavior.`
- B3: title `THD & aliasing split`, body: `Drives the filter into its nonlinearity and splits distortion into harmonic content vs aliased content per oversampling tier — aliasing must fall as the OS factor rises.`
- B4: title `Phase / group delay (descriptive only)`, body: `Records phase and group delay from the deconvolved impulse response. Descriptive-only per register Q20 (IR not time-aligned); numbers are reported, not gated.`

- [ ] **Step 1: Failing tests** — lookup hit/miss; explain parses the Task-3 label format (assert model/mode/fc appear in body; B4 mentions "Q20"); unknown label → fallback.
- [ ] **Step 2: Run** — FAIL. **Step 3:** implement. **Step 4:** `npm test` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(franklin): explanations — catalog lookup + generated battery prose"`

---

### Task 10: Franklin tab UI

**Files:**
- Create: `tools/roadmap-dashboard/src/franklin.ts`, `tools/roadmap-dashboard/src/franklinRender.ts`, `tools/roadmap-dashboard/src/franklinRender.test.ts`
- Modify: `tools/roadmap-dashboard/src/main.ts`, `tools/roadmap-dashboard/index.html`, `tools/roadmap-dashboard/src/styles.css`

**Interfaces (Consumes):** `/api/runs`, `/api/runs/<id>`, `/api/ci`, `/api/control/*`, `/api/catalog`, franklinExplain. **Produces:** pure renderers (tested):
```ts
export function renderRunsPage(runs: RunSummary[], ci: CiPayload, diskBytes: number, stale: StaleInfo[]): string;
export function renderRunDetail(d: RunDetail, catalog: CatalogEntry[]): string;
export function deviationRows(d: RunDetail): { severity: 0 | 1 | 2; title: string; detail: string }[];
```
`deviationRows` ranking (spec §8): severity 0 = failed tests (each failure message) and checks with verdict `fail`; severity 1 = `stalled`/`error`/`stopped` run states; severity 2 = info-margin rows for every check with `expected` present (show measured vs expected + delta) and the three named margins when their keys appear (`method_delta_db`, `selfosc_cents_err`, `noise_floor_dbfs`). Sort ascending by severity, then |delta| descending.

UI composition:
- `index.html`: `<title>Bernie — Dashboard</title>`, add `<nav id="tabs"><button data-view="roadmap">Roadmap</button><button data-view="franklin">Franklin</button></nav>` above `#app`.
- `main.ts`: keep the existing roadmap flow as view "roadmap"; add view state — on "franklin", hand `#app` to `franklin.ts`'s `mountFranklin(app)` and stop roadmap painting; polling intervals: runs 2 s, ci 10 s (the server caches gh at 60 s); clear intervals on view switch.
- `franklin.ts`: fetch loop + event delegation: Stop buttons (`POST /api/control/stop`, confirm dialog), New-run form (template select + model/grid selects shown only for chz + stale-binary warning chip when `stale` for the template's binary + Start button), Re-run action on archive rows (start with the row's recorded template/params — carry `templateId`+`params` into `RunSummary` via the start event's argv mapping in runs.ts: derive `{templateId, params}` server-side: argv[0] basename `k2000_tests` + env markers unavailable → map by binary + args: `--model X` → chz template with params; suite env flags are NOT recoverable from argv → re-run for suite rows always plain `suite`; acceptable, label the button "re-run (plain)" for suite rows).
- Sections in order: Active cards → CI strip → New-run form → Archive table (+ disk footprint in the section header, `MB` with one decimal) → detail drawer under the clicked row (fetch detail, render deviations panel FIRST, then tests list with catalog prose expandable per row, then chz label explanation + metadata argv/gitSha/buildType + CSV path note `build/characterization/<model>/`).

- [ ] **Step 1: Failing renderer tests** — `franklinRender.test.ts`: running chz card shows done/total/% + ETA text computed from `startedAt`/`done`/`total` (pass a fixed `now`); stalled run shows the stalled badge; deviationRows ordering (a fail beats a margin; margins sorted by |delta|); archive row shows duration + outcome; detail renders catalog prose for a failing test (messages listed red) and B1 explanation for a chz label.
- [ ] **Step 2: Run** — FAIL. **Step 3:** implement renderers (`franklinRender.ts` pure string-builders like `render.ts`; escape all label/message text with the same `esc()` helper pattern used in `render.ts` — check it exists there, else add one to franklinRender). **Step 4:** `npm test` → PASS.
- [ ] **Step 5: Wire `franklin.ts` + main.ts/index.html/styles**, build the bundle: `npm run build` → succeeds; `npm run dashboard` and manually verify: tab switch works, a live suite run (`./build/tests/k2000_tests` in another terminal) appears within ~2 s of starting… — actually suite events land at end-of-run (Task 2 note): verify instead with `timeout 60 ./build/tests/k2000_device_characterization --model moog --quick` showing a live card with a moving counter; Stop button kills it and the row archives as `stopped`.
- [ ] **Step 6: Commit** — `git add -A tools/roadmap-dashboard && git commit -m "feat(franklin): Franklin tab — live cards, CI strip, run control, archive, deviations-first detail"`

---

### Task 11: naming + docs deliverables

**Files:**
- Create: `docs/franklin/charter.md` (Version 5.17), `docs/franklin/dashboard.md` (Version 5.18)
- Modify: `docs/architecture/engine-questions.md` (locked table: new row L8), `CLAUDE.md` (constants line), `tools/roadmap-dashboard/README.md` (Franklin tab section), `README.md` (one line under Documentation linking `docs/franklin/`), `tools/roadmap-dashboard/roadmap.json` (add item `franklin-dashboard`, kind feature, status in-progress, under the current engagement/v5 grouping — keep schema: id+status+kind mandatory)

Content requirements:
- `charter.md`: one page, vision-only, no status: Franklin = the measurement/validation product; remit table mapping — SP-A core (shipped, PR #8) · SP-B filter profile · SP-C oscillator profile · SP-D hardware bridge (Summit/Arturia capture; Q25) · external-VST capture harness (roadmap `v5 — External VST test harness`) · future effect batteries (drive/saturation/distortion/chorus) · the suite gates + drift rules + this dashboard. Epigraph: *"Franklin is what keeps us honest and our synths musical."* (user, 2026-07-03).
- `dashboard.md`: how to run (`npm run dashboard` → Franklin tab), the event-file contract (point at spec §4), env vars (`BERNIE_NO_RUNLOG`, `BERNIE_RUNLOG_DIR`), what stalled means, compaction semantics (never deletes), how re-run/stop behave, where chz CSVs land.
- Register L8 row: `| L8 | The measurement/validation product is **Franklin** (ruled 2026-07-03) — SP-A/B/C/D, the external-VST harness, suite gates + drift rules, and the runs dashboard are all Franklin. Bernie = synth, Ricky = FX, Franklin = the instrument. | user ruling; spec v5.15 |`
- CLAUDE.md constants line append: `- The measurement product is **Franklin** (SP-A/B/C/D + dashboards); .franklin/ is its state dir.`

- [ ] **Step 1: Write all files** per requirements. **Step 2:** `tools/drift-check --session` → all OK (register/roadmap/doc-links rules exercise these). **Step 3: Commit** — `git commit -m "docs(franklin): charter v5.17, dashboard manual v5.18, register L8, CLAUDE.md constant"`

---

### Task 12: end-to-end verification + PR

- [ ] **Step 1: Full local gauntlet**
```bash
cmake --build build --target k2000_tests k2000_device_characterization -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -2       # Summary: 291 tests, 0 failed
tools/drift-check --ci                                                   # all OK incl. franklin-catalog
cd tools/roadmap-dashboard && npm test && npm run build && cd ../..
```
- [ ] **Step 2: Live smoke** — `npm run dashboard`; start a `chz --model moog --quick` from the Franklin tab; watch the card move; Stop it; confirm archive row `stopped`; confirm the suite's last run shows 291 tests with catalog prose in detail; confirm CI strip shows this branch once pushed.
- [ ] **Step 3: Push + PR**
```bash
git push -u origin feat/franklin-dashboard
gh pr create --title "feat: Franklin — live runs dashboard, full archive, run control, test catalog" --body "Implements spec v5.15: runlog NDJSON producers (suite tap + chz sink; CharacterizationRunner untouched), .franklin/runs full archive with finish-time compaction (never deletes), Franklin tab (live cards / CI strip / new-run form with stale-binary warning / archive / deviations-first detail), whitelisted run control with pid-verified stop, gh-polled CI strip, 291-entry test catalog + franklin-catalog drift rule, charter v5.17 + manual v5.18 + register L8. Spec amendment: progress events carry no per-point metrics (Progress sink is (done,total,label) and the runner is untouchable) — all numbers ride end.checks + suite failure messages. Suite 291/0, drift-check --ci clean, npm test green."
```
Expected: windows + linux + ASan/UBSan green (CI never runs the dashboard server; the linux job picks up the new suite test + drift rule).

---

## Self-review notes (kept for the executor)

- Spec §4 `progress.metrics` is deliberately dropped (Global Constraints) — runner untouchable; deviations ride `end.checks` + suite messages. Flag this in the PR body.
- Suite `test` events are written post-run (TestMain loop) — mid-suite liveness was not a requirement; chz runs are the 40 h case and they stream live.
- Type names are pinned in Task 6's `franklinTypes.ts` block; Tasks 8–10 import from there — do not re-declare.
- Catalog completeness is enforced by Task 5's rule from day one; if Task 4's count differs from 291 (suite may grow during this branch), regenerate the skeleton and reconcile BEFORE Task 5 lands.
