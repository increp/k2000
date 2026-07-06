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
#ifndef _WIN32
        {   // runner detection priority chain (Task 1, v1.1)
            auto runnerOf = [&](const char* envName, const char* envVal) {
                ::unsetenv("BERNIE_RUNNER"); ::unsetenv("GITHUB_ACTIONS"); ::unsetenv("CLAUDECODE");
                if (envName != nullptr) ::setenv(envName, envVal, 1);
                // Use a counter-based kind to avoid filename collision (timestamps are second-precision).
                // The runner detection itself is agnostic to the kind field, so this doesn't affect the test.
                static int counter = 0;
                auto kind = juce::String("r") + juce::String(counter++);
                runlog::Writer w(kind, dir, 0, 1 << 30);
                w.start(juce::StringArray{"x"});
                juce::StringArray lines; lines.addLines(w.file().loadFileAsString().trim());
                auto parsed = juce::JSON::parse(lines[0]);
                ::unsetenv("BERNIE_RUNNER"); ::unsetenv("GITHUB_ACTIONS"); ::unsetenv("CLAUDECODE");
                return parsed.getProperty("runner", "").toString();
            };
            expectEquals(runnerOf("BERNIE_RUNNER", "dashboard"), juce::String("dashboard"));
            expectEquals(runnerOf("GITHUB_ACTIONS", "true"),     juce::String("ci"));
            expectEquals(runnerOf("CLAUDECODE", "1"),            juce::String("claude"));
            expectEquals(runnerOf(nullptr, nullptr),             juce::String("terminal"));
        }
#endif
        dir.deleteRecursively();
    }
};
static RunLogTests runLogTests;
