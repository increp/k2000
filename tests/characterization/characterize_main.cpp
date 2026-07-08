#include <juce_core/juce_core.h>
#include "CharacterizationRunner.h"
#include "FilterUnderTest.h"
#include "../testdsp/GoldenIO.h"
#include "../testdsp/RunLog.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Self-sufficient heavy runner. CI and a plain developer use this directly;
// no skill required.
//
// Usage:
//   k2000_device_characterization [--model moog|huggett|all] [--grid <name>] [--quick]
//
//   --grid quick|full|spd|osalias|rates|largesig|deep  selects the grid (default full).
//     quick     chz::coarseGrid()      (~72 B1 points, fast/CI smoke mode)
//     full      chz::fullGrid()        (dense, slow; ~40 h/model production characterization)
//     spd       chz::spdGrid()         SP-D hardware-comparison map (~75 min/model)
//     osalias   chz::osAliasGrid()     OS/aliasing verification (~10 min/model)
//     rates     chz::hostRateGrid()    host-rate portability spot-check (~8 min/model)
//     largesig  chz::largeSignalGrid() drive/resonance law (~10 min/model)
//     deep      all four purpose grids above, in sequence, per model (~1.7-2.0 h/model)
//   --quick  is an alias for --grid quick (back-compat).
//
// Exit: 0 = all method-agreement deltas < 1.0 dB (PASS)
//       1 = at least one delta >= 1.0 dB (FAIL)
//       2 = unrecognised --grid name (usage error)

namespace {

// One purpose-grid id + its factory + its stdout/outdir label. "quick"/"full"
// are handled separately (bare outDir, no sub-grid label) — this table only
// covers the four purpose grids plus "deep" iterates it.
struct NamedGrid {
    const char* id;
    chz::Grid (*factory)();
};

const NamedGrid kPurposeGrids[] = {
    { "spd",      chz::spdGrid },
    { "osalias",  chz::osAliasGrid },
    { "rates",    chz::hostRateGrid },
    { "largesig", chz::largeSignalGrid },
};

} // namespace

// Runs one grid for one model. gridId is the sub-grid label used for the outDir
// nesting and progress-label prefix: empty for quick/full (bare build/characterization/
// <model>/, back-compat with the /characterize-filter skill + dashboard), non-empty
// for a purpose grid (build/characterization/<model>/<gridId>/ so `deep`'s four
// sub-grids of the same model don't wipe each other).
static int runOne(const juce::String& model, const chz::Grid& grid, const juce::String& gridId,
                   runlog::Writer& log, std::vector<runlog::Writer::Check>& checksOut) {
    auto fut = (model == "moog") ? chz::makeMoogFut() : chz::makeHuggettFut();

    // Build output directory relative to cwd (the repo root when invoked from
    // the project root, which is what the task spec and CI use).
    auto modelDir = juce::File::getCurrentWorkingDirectory()
                        .getChildFile("build/characterization")
                        .getChildFile(model);
    auto outDir = gridId.isEmpty() ? modelDir : modelDir.getChildFile(gridId);
    // Only wipe the directory this run actually owns: the bare model dir for
    // quick/full, or just the sub-grid dir for a purpose grid — so `deep`'s
    // sibling sub-grids (already written earlier in this same invocation)
    // survive the next sub-grid's run.
    outDir.deleteRecursively();
    outDir.createDirectory();

    const juce::String labelPrefix = "[" + model + "]" + (gridId.isEmpty() ? "" : " [" + gridId + "]") + " ";
    std::printf("%sgrid = %s\n", labelPrefix.toRawUTF8(),
                (gridId.isEmpty() ? juce::String("(unnamed)") : gridId).toRawUTF8());
    std::fflush(stdout);

    // Live progress (engagement item 6): one overwriting status line on stderr —
    // stdout stays clean for the machine-readable digest. Disable with
    // BERNIE_NO_PROGRESS=1 (e.g. when redirecting stderr to a log).
    const bool showProgress = std::getenv("BERNIE_NO_PROGRESS") == nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    chz::CharacterizationRunner::Progress progress =
        [&](int done, int total, const juce::String& label) {
            if (showProgress) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t0).count();
                const double eta = done > 0 ? elapsed / done * (total - done) : 0.0;
                std::fprintf(stderr, "\r%s%d/%d (%d%%)  %5.0fs elapsed  eta %5.0fs  %-60.60s",
                             labelPrefix.toRawUTF8(), done, total, (int) ((100LL * done) / total),
                             elapsed, eta, label.toRawUTF8());
                if (done == total) std::fputc('\n', stderr);
                std::fflush(stderr);
            }
            log.progress(done, total, labelPrefix + label);
        };

    auto summary = chz::CharacterizationRunner::run(*fut, grid, outDir, progress);

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
    std::printf("%sworst method-agreement delta = %.3f dB  (%s)\n",
                labelPrefix.toRawUTF8(), worstDelta,
                worstDeltaKey.isEmpty() ? "n/a" : worstDeltaKey.toRawUTF8());
    if (hasAlias)
        std::printf("%salias_db@os1 (worst/highest) = %.1f dB  "
                    "alias_db@os8 (best/lowest) = %.1f dB\n",
                    labelPrefix.toRawUTF8(), worstAlias1, bestAlias8);
    else
        std::printf("%salias_db: n/a\n", labelPrefix.toRawUTF8());
    if (worstThdDb > std::numeric_limits<double>::lowest() + 1.0)
        std::printf("%sworst thd_db = %.1f dB\n", labelPrefix.toRawUTF8(), worstThdDb);
    std::printf("%sworst selfosc_cents_err = %.1f cents\n",
                labelPrefix.toRawUTF8(), worstCentsErr);
    std::printf("%sartifacts -> %s\n",
                labelPrefix.toRawUTF8(), outDir.getFullPathName().toRawUTF8());
    std::fflush(stdout);

    // chz sink: every summary key becomes an info check EXCEPT method_delta_db,
    // which mirrors the binary's real gate (expected 1.0 dB, pass/fail verdict).
    for (const auto& kv : summary) {
        if (std::abs(kv.second - (-1.0)) < 1.0e-9) continue;
        runlog::Writer::Check c; c.name = labelPrefix + kv.first; c.measured = kv.second;
        if (kv.first.endsWith("method_delta_db")) { c.expected = 1.0; c.verdict = kv.second < 1.0 ? "pass" : "fail"; }
        else c.verdict = "info";
        checksOut.push_back(c);
    }

    // Gate: method-agreement is the headline gate (< 1.0 dB = pass).
    return (worstDelta < 1.0) ? 0 : 1;
}

// Runs the "deep" sequence (all four purpose grids in order) for one model,
// within the SAME runlog Writer/checks accumulator as any other grid choice.
static int runDeepOne(const juce::String& model, runlog::Writer& log,
                      std::vector<runlog::Writer::Check>& checksOut) {
    int rc = 0;
    const int numSubGrids = (int) (sizeof(kPurposeGrids) / sizeof(kPurposeGrids[0]));
    for (int i = 0; i < numSubGrids; ++i) {
        const auto& ng = kPurposeGrids[i];
        std::fprintf(stderr, "\n[%s] deep: sub-grid %d/%d (%s)\n",
                     model.toRawUTF8(), i + 1, numSubGrids, ng.id);
        rc |= runOne(model, ng.factory(), ng.id, log, checksOut);
    }
    return rc;
}

static void printUsage() {
    std::fprintf(stderr,
        "usage: k2000_device_characterization [--model moog|huggett|all] "
        "[--grid quick|full|spd|osalias|rates|largesig|deep] [--quick]\n");
}

int main(int argc, char** argv) {
    juce::String model = "all";
    juce::String gridName = "full";

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--model" && i + 1 < argc) {
            model = juce::String(argv[++i]);
        } else if (arg == "--quick") {
            gridName = "quick";
        } else if (arg == "--grid" && i + 1 < argc) {
            gridName = juce::String(argv[++i]);
        }
    }

    static const char* kKnownGrids[] = { "quick", "full", "spd", "osalias", "rates", "largesig", "deep" };
    bool known = false;
    for (const char* g : kKnownGrids) if (gridName == g) { known = true; break; }
    if (!known) {
        std::fprintf(stderr, "error: unrecognised --grid '%s'\n", gridName.toRawUTF8());
        printUsage();
        return 2;
    }

    std::printf("k2000_device_characterization  model=%s  grid=%s\n\n",
                model.toRawUTF8(), gridName.toRawUTF8());

    runlog::Writer log("chz");
    {
        juce::StringArray a;
        for (int i = 0; i < argc; ++i) a.add(argv[i]);
        log.start(a, model, gridName);
    }
    const auto mainT0 = std::chrono::steady_clock::now();

    int rc = 0;
    std::vector<runlog::Writer::Check> allChecks;
    std::vector<juce::String> models;
    if (model == "all") { models.push_back("moog"); models.push_back("huggett"); }
    else                { models.push_back(model); }

    for (const auto& m : models) {
        if (gridName == "deep") {
            rc |= runDeepOne(m, log, allChecks);
        } else if (gridName == "quick") {
            rc |= runOne(m, chz::coarseGrid(), "", log, allChecks);
        } else if (gridName == "full") {
            rc |= runOne(m, chz::fullGrid(), "", log, allChecks);
        } else if (gridName == "spd") {
            rc |= runOne(m, chz::spdGrid(), "spd", log, allChecks);
        } else if (gridName == "osalias") {
            rc |= runOne(m, chz::osAliasGrid(), "osalias", log, allChecks);
        } else if (gridName == "rates") {
            rc |= runOne(m, chz::hostRateGrid(), "rates", log, allChecks);
        } else if (gridName == "largesig") {
            rc |= runOne(m, chz::largeSignalGrid(), "largesig", log, allChecks);
        }
    }

    const double elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - mainT0).count();
    log.end(rc == 0 ? "pass" : "fail", elapsedSeconds, allChecks);

    std::printf("\n%s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
