#include <juce_core/juce_core.h>
#include "testdsp/CaptureCal.h"
#include "testdsp/SteppedSine.h"
#include "testdsp/SignalGen.h"
#include "characterization/ReferenceDevices.h"
#include "characterization/CharacterizationRunner.h"   // logFreqs
#include <cmath>
#include <vector>

// Loopback-calibration math proven on a synthetic capture chain (spec §5.2):
// chain = gentle biquad tilt + fixed -6.02 dB pad. A device measured THROUGH
// that chain, then compensated with the chain's loopback calibration, must
// recover the device's exact analytic response. The -18 dBFS calibration tone
// through the pad must recover the pad as the level offset.

namespace {

// The synthetic capture chain: known coloration (biquad) + known pad (x0.5).
struct SyntheticChain {
    chz::AnalyticBiquad bq;
    float pad = 0.5f;
    void reset() { bq.reset(); }
    void process(float* m, int n) {
        bq.process(m, n);
        for (int i = 0; i < n; ++i) m[i] *= pad;
    }
};

// A device observed through the chain (device first, chain second — as a mic
// or interface would color a hardware filter's output).
struct DeviceThroughChain {
    chz::AnalyticBiquad dev;
    SyntheticChain*     chain = nullptr;
    void reset() { dev.reset(); chain->reset(); }
    void process(float* m, int n) { dev.process(m, n); chain->process(m, n); }
};

} // namespace

struct CaptureCalTests : public juce::UnitTest {
    CaptureCalTests() : juce::UnitTest("CaptureCal") {}

    void runTest() override {
        using testdsp::CaptureCal;
        const double sr = 48000.0;
        auto probes = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 100);

        // Chain coloration: mild LP tilt (fc 8 kHz, Q ~ 0.7) + -6.02 dB pad.
        SyntheticChain chain;
        chz::OperatingPoint chainOp;
        chainOp.cutoffHz       = 8000.0;
        chainOp.resonance      = 0.021;    // Q ~ 0.7 (0.5 + 0.021*9.5)
        chainOp.hostSampleRate = sr;
        chain.bq.setOperatingPoint(chainOp);

        beginTest("loopback calibration captures the chain response");
        auto cal = CaptureCal::calibrateChain(chain, probes, sr, 0.25f);
        expectEquals((int) cal.freqs.size(), (int) probes.size());
        // Spot-check: at 100 Hz the chain is just the pad (biquad ~ flat there).
        expectWithinAbsoluteError(cal.chainMagDb.front(), -6.02, 0.15);

        beginTest("compensation recovers the device's analytic response through the chain");
        DeviceThroughChain thru;
        thru.chain = &chain;
        chz::OperatingPoint devOp;
        devOp.cutoffHz       = 1000.0;
        devOp.resonance      = 0.4737;     // Q ~ 5
        devOp.hostSampleRate = sr;
        thru.dev.setOperatingPoint(devOp);

        auto measured = testdsp::SteppedSine::transfer(thru, probes, sr, 0.25f);
        auto corrected = CaptureCal::compensate(cal, measured.magDb);
        double worst = 0.0;
        for (size_t i = 0; i < probes.size(); ++i)
            worst = std::max(worst, std::abs(corrected[i] - thru.dev.trueMagDb(probes[i])));
        expect(worst < 0.2, "compensated response within 0.2 dB of analytic truth");

        beginTest("calibration tone recovers the chain's level offset");
        auto tone = testdsp::SignalGen::sine(0.12589254f, 1000.0, sr, 1 << 14); // -18 dBFS
        for (auto& v : tone) v *= 0.5f;                                          // the pad
        expectWithinAbsoluteError(CaptureCal::levelOffsetFromTone(tone, -18.0), -6.02, 0.05);
    }
};

static CaptureCalTests captureCalTestsInstance;
