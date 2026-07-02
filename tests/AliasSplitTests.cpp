#include <juce_core/juce_core.h>
#include "testdsp/Metrics.h"
#include "testdsp/Spectrum.h"
#include "testdsp/SignalGen.h"
#include "characterization/ReferenceDevices.h"
#include <cmath>
#include <vector>

// Audibility-split aliasing: inharmonic energy below the fundamental is the
// exposed/dissonant half. Proven two ways: (1) a hand-built spectrum with exact
// energies; (2) the EngineeredAliaser reference device, whose injected tone's
// frequency and level are known by construction.

struct AliasSplitTests : public juce::UnitTest {
    AliasSplitTests() : juce::UnitTest("AliasSplit") {}

    void runTest() override {
        using testdsp::Metrics;

        beginTest("hand-built spectrum: exact below/above split");
        std::vector<float> mag(64, 0.0f);
        mag[10] = 1.0f;     // fundamental (bin 10)
        mag[20] = 0.5f;     // harmonic (2x) — must be EXCLUDED from both halves
        mag[5]  = 0.1f;     // inharmonic below
        mag[25] = 0.05f;    // inharmonic above (not a multiple of 10)
        const auto sp = Metrics::aliasSplit(mag, 10);
        expectWithinAbsoluteError(sp.belowDb, 10.0 * std::log10(0.01),   1.0e-6);
        expectWithinAbsoluteError(sp.aboveDb, 10.0 * std::log10(0.0025), 1.0e-6);

        beginTest("EngineeredAliaser below the fundamental: split recovers 20log10(a/A)");
        const double sr = 48000.0;
        const int    N  = 1 << 14;
        chz::EngineeredAliaser dev;
        dev.sr       = sr;
        dev.aliasHz  = 64.0 * sr / N;      // bin 64 — below the bin-256 fundamental
        dev.aliasAmp = 0.01f;
        dev.reset();
        auto sig = testdsp::SignalGen::binAlignedSine(0.5f, 256, N);
        dev.process(sig.data(), N);
        auto m = testdsp::Spectrum::magnitude(sig);
        const auto s1 = Metrics::aliasSplit(m, 256);
        expectWithinAbsoluteError(s1.belowDb, 20.0 * std::log10(0.01 / 0.5), 0.1);
        expect(s1.aboveDb < -60.0, "no engineered energy above the fundamental");

        beginTest("EngineeredAliaser above the fundamental: split recovers it above");
        dev.aliasHz = 700.0 * sr / N;      // bin 700 — above, not a multiple of 256
        dev.reset();
        auto sig2 = testdsp::SignalGen::binAlignedSine(0.5f, 256, N);
        dev.process(sig2.data(), N);
        auto m2 = testdsp::Spectrum::magnitude(sig2);
        const auto s2 = Metrics::aliasSplit(m2, 256);
        expectWithinAbsoluteError(s2.aboveDb, 20.0 * std::log10(0.01 / 0.5), 0.1);
        expect(s2.belowDb < -60.0, "no engineered energy below the fundamental");
    }
};

static AliasSplitTests aliasSplitTestsInstance;
