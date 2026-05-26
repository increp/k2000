#include <juce_core/juce_core.h>
#include <cstdio>

int main(int, char**) {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failedTests = 0;
    int totalTests = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        const auto* r = runner.getResult(i);
        totalTests++;
        failedTests += r->failures;
        std::printf("[%s] %s: %d passes, %d failures\n",
            r->failures == 0 ? "PASS" : "FAIL",
            r->unitTestName.toRawUTF8(),
            r->passes, r->failures);
    }
    std::printf("\nSummary: %d tests, %d failed\n", totalTests, failedTests);
    return failedTests > 0 ? 1 : 0;
}
