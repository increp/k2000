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
