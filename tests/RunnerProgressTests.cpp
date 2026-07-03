#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include <vector>

// Engagement item 6 (user, 2026-07-01): "I want to see testing live, with
// progress bars or at least numbers on the screen." The runner takes an
// optional progress sink; every measured point reports (done, total, label).
// Contract: done is strictly increasing, ends exactly at total, total is known
// from the first call, labels are non-empty. Existing call sites pass nothing
// and are unaffected.

struct RunnerProgressTests : public juce::UnitTest {
    RunnerProgressTests() : juce::UnitTest("RunnerProgress") {}

    void runTest() override {
        beginTest("runner reports monotonic progress ending at the announced total");
        auto fut = chz::makeMoogFut();
        chz::Grid g;
        g.modes       = { chz::Mode::LP24, chz::Mode::HP };
        g.cutoffs     = { 1000.0 };
        g.resonances  = { 0.0, 0.9 };
        g.drives      = { 0.0 };
        g.osFactors   = { 1, 2 };
        g.osModes     = { chz::OsMode::Live };
        g.hostRates   = { 96000.0 };
        g.probeFreqs  = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 12);

        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_progress_test");
        outDir.deleteRecursively();
        outDir.createDirectory();

        std::vector<int> dones;
        int total = -1;
        bool labelsOk = true;
        auto summary = chz::CharacterizationRunner::run(
            *fut, g, outDir,
            [&](int done, int tot, const juce::String& label) {
                dones.push_back(done);
                if (total < 0) total = tot;
                if (tot != total) labelsOk = false;      // total must be stable
                if (label.isEmpty()) labelsOk = false;
            });

        expect(!dones.empty(), "progress sink fired");
        expect(total > 0, "total announced");
        bool monotonic = true;
        for (size_t i = 1; i < dones.size(); ++i)
            if (dones[i] <= dones[i - 1]) monotonic = false;
        expect(monotonic, "done strictly increases");
        expect(dones.back() == total,
               "final done == total (" + juce::String(dones.back()) + "/" + juce::String(total) + ")");
        expect(labelsOk, "labels non-empty, total stable");
        expect(summary.count("moog/LP24/fc1000/corner_hz") == 1, "summary still produced");
        outDir.deleteRecursively();
    }
};

static RunnerProgressTests runnerProgressTestsInstance;
