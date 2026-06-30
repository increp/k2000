#include <juce_core/juce_core.h>
#include "CharacterizationRunner.h"
#include "FilterUnderTest.h"
#include "../testdsp/GoldenIO.h"
#include <cstdio>
#include <string>
#include <vector>

// Self-sufficient heavy runner. CI and a plain developer use this directly;
// no skill required.
//
// Usage:
//   k2000_filter_characterization [--model moog|huggett|all] [--quick]
//
//   --quick  selects chz::coarseGrid() (~72 B1 points, fast/CI smoke mode).
//   default  selects chz::fullGrid()   (dense, slow; production characterization).
//
// Exit: 0 = all method-agreement deltas < 1.0 dB (PASS)
//       1 = at least one delta >= 1.0 dB (FAIL)

static int runOne(const juce::String& model, bool quick) {
    auto fut = (model == "moog") ? chz::makeMoogFut() : chz::makeHuggettFut();

    // Build output directory relative to cwd (the repo root when invoked from
    // the project root, which is what the task spec and CI use).
    auto outDir = juce::File::getCurrentWorkingDirectory()
                      .getChildFile("build/characterization")
                      .getChildFile(model);
    outDir.deleteRecursively();
    outDir.createDirectory();

    const chz::Grid grid = quick ? chz::coarseGrid() : chz::fullGrid();
    const char* gridName = quick ? "coarse (quick/CI)" : "full (dense/production)";

    std::printf("[%s] grid = %s\n", model.toRawUTF8(), gridName);
    std::fflush(stdout);

    auto summary = chz::CharacterizationRunner::run(*fut, grid, outDir);

    // Persist summary.csv next to the three battery CSVs.
    testdsp::GoldenIO::save(outDir.getChildFile("summary.csv"), summary);

    // Print human-readable digest: worst method-agreement delta, self-osc extremes,
    // worst alias and THD values, and the artifact output path.
    double worstDelta     = 0.0;
    // alias_db: 0 dB = full aliasing (os1 expected), very negative = clean.
    // Sentinel = -1.0 exactly; valid data may be anywhere in (-inf, 0].
    // Track the least-negative (worst) value at os1 and os8 separately.
    double worstAlias1    = std::numeric_limits<double>::lowest();  // os=1
    double bestAlias8     = std::numeric_limits<double>::lowest();  // os=8
    double worstThdDb     = std::numeric_limits<double>::lowest();
    double worstCentsErr  = 0.0;

    juce::String worstDeltaKey;
    juce::String worstAliasKey;

    for (const auto& kv : summary) {
        const juce::String& k = kv.first;
        const double v = kv.second;
        // Sentinel check: exactly -1.0 means not-found; skip.
        // Use a tight tolerance so valid values like -0.999 are not skipped.
        if (std::abs(v - (-1.0)) < 1.0e-9) continue;

        if (k.endsWith("method_delta_db")) {
            if (v > worstDelta) { worstDelta = v; worstDeltaKey = k; }
        } else if (k.endsWith("alias_db@os1")) {
            if (v > worstAlias1) { worstAlias1 = v; worstAliasKey = k; }
        } else if (k.endsWith("alias_db@os8")) {
            if (v > bestAlias8) bestAlias8 = v;
        } else if (k.endsWith("thd_db")) {
            if (v > worstThdDb) worstThdDb = v;
        } else if (k.endsWith("selfosc_cents_err")) {
            const double abs_v = v < 0.0 ? -v : v;
            if (abs_v > worstCentsErr) worstCentsErr = abs_v;
        }
    }

    const bool hasAlias = (worstAlias1 > std::numeric_limits<double>::lowest() + 1.0);
    std::printf("[%s] worst method-agreement delta = %.3f dB  (%s)\n",
                model.toRawUTF8(), worstDelta,
                worstDeltaKey.isEmpty() ? "n/a" : worstDeltaKey.toRawUTF8());
    if (hasAlias)
        std::printf("[%s] alias_db@os1 (worst/highest) = %.1f dB  "
                    "alias_db@os8 (best/lowest) = %.1f dB\n",
                    model.toRawUTF8(), worstAlias1, bestAlias8);
    else
        std::printf("[%s] alias_db: n/a\n", model.toRawUTF8());
    if (worstThdDb > std::numeric_limits<double>::lowest() + 1.0)
        std::printf("[%s] worst thd_db = %.1f dB\n", model.toRawUTF8(), worstThdDb);
    std::printf("[%s] worst selfosc_cents_err = %.1f cents\n",
                model.toRawUTF8(), worstCentsErr);
    std::printf("[%s] artifacts -> %s\n",
                model.toRawUTF8(), outDir.getFullPathName().toRawUTF8());
    std::fflush(stdout);

    // Gate: method-agreement is the headline gate (< 1.0 dB = pass).
    return (worstDelta < 1.0) ? 0 : 1;
}

int main(int argc, char** argv) {
    juce::String model = "all";
    bool quick = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--model" && i + 1 < argc) {
            model = juce::String(argv[++i]);
        } else if (arg == "--quick") {
            quick = true;
        }
    }

    const char* gridLabel = quick ? "coarseGrid" : "fullGrid";
    std::printf("k2000_filter_characterization  model=%s  grid=%s\n\n",
                model.toRawUTF8(), gridLabel);

    int rc = 0;
    if (model == "all") {
        rc |= runOne("moog",    quick);
        rc |= runOne("huggett", quick);
    } else {
        rc = runOne(model, quick);
    }

    std::printf("\n%s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
