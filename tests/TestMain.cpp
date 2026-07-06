#include <juce_core/juce_core.h>
#include "testdsp/RunLog.h"
#include <cstdio>
#include <cstdlib>

namespace {
// Streams a live progress event as each test result lands, so the Franklin
// dashboard shows a moving counter/ETA for suite runs (not only chz runs), and the
// steady file writes keep long suites (disparity/voiceperf) from tripping the
// stall detector. The total is an estimate from the previous suite run's count —
// JUCE cannot report the total until the run finishes — so it is -1 until a prior
// run exists, in which case the card shows a live count without a percentage.
struct ProgressRunner : juce::UnitTestRunner {
    runlog::Writer& log;
    int total;
    ProgressRunner(runlog::Writer& l, int t) : log(l), total(t) {}
    void resultsUpdated() override {
        const int n = getNumResults();
        juce::String label;
        if (n > 0)
            if (const auto* r = getResult(n - 1))
                label = r->unitTestName + " / " + r->subcategoryName;
        log.progress(n, total, label);
    }
};
} // namespace

int main(int argc, char** argv) {
    // Estimate the total BEFORE our own runlog file exists, so the current (empty)
    // suite file is never the newest one lastSuiteTestCount() examines.
    const int estTotal = runlog::lastSuiteTestCount();
    runlog::Writer log("suite");
    { juce::StringArray a; for (int i = 0; i < argc; ++i) a.add(argv[i]); log.start(a, {}, {}, estTotal); }
    const auto t0 = juce::Time::currentTimeMillis();

    ProgressRunner runner(log, estTotal);
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
