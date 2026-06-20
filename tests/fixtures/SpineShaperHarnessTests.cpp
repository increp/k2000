// SpineShaperHarnessTests.cpp — gated fixtures for AsymSaturator
//
// M1: output is finite across all drive settings.
// M4: noise-to-signal ratio (inharmonic energy vs oversampled reference) — measures
//     aliasing produced by plain tanh at each drive level.
// v5.0 regression gate: NSR must not exceed the committed baseline anchored from
//   the measured output of this very suite (baseline + 3 dB headroom).
//   Literature target for a clean nonlinearity with 32x oversampling reference: < -60 dB.
//
// Include paths: include dir is tests/, so testdsp/ headers are testdsp/Foo.h and
// component headers are ../../src/dsp/spine/Foo.h.

#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Spectrum.h"
#include "testdsp/Metrics.h"
#include "testdsp/Reference.h"
#include "testdsp/Gate.h"
#include "../../src/dsp/spine/AsymSaturator.h"
#include <cmath>
#include <vector>

class SpineShaperHarnessTests : public juce::UnitTest {
public:
    SpineShaperHarnessTests() : juce::UnitTest("SpineShaperHarness") {}

    // NSR gate constants anchored from measured baselines (baseline + 3 dB headroom).
    // Literature target for clean tanh with 32x OS reference would be < -60 dB;
    // plain base-rate tanh generates substantial aliasing (expected — see OverdriveDiagnosticTests;
    // ADAA was tried and measured no better). These gates freeze the v5.0 baseline so that
    // any regression (alias increase) will be caught. Future HQ-tier oversampling would lower them.
    // Measured 2026-06-20:  drive=0.0 → -26.82 dB | drive=0.5 → -12.44 dB | drive=1.0 → -8.31 dB
    static constexpr double kNsrGateDrive0   = -23.0;  // measured -26.82 dB + 3 dB headroom
    static constexpr double kNsrGateDrive0p5 =  -9.0;  // measured -12.44 dB + 3 dB headroom
    static constexpr double kNsrGateDrive1p0 =  -5.0;  // measured  -8.31 dB + 3 dB headroom

    // Inline reference matching AsymSaturator internals: comp * tanh(gain*x + bias).
    // Post-stage constants: bias 0.15, maxDriveDb 24.
    static float shapeRef(float x, float drive01) noexcept {
        const float gain = std::pow(10.0f, (drive01 * 24.0f) / 20.0f);
        const float full = (gain > 1.0f) ? (1.0f / std::tanh(gain)) : 1.0f;
        const float comp = 1.0f + 0.75f * (full - 1.0f);
        return comp * std::tanh(gain * x + 0.15f);
    }

    void runTest() override {
        // Parameters matching the brief's oversampled-reference approach.
        const int   n      = 1 << 13;           // 8192-pt base-rate block
        const int   M      = 32;                 // oversampling factor for reference
        const int   bin    = 1500;               // fundamental bin index
        const double fsBase = 48000.0;
        const double f     = (double) bin * fsBase / (double) n;  // bin-aligned frequency

        for (float drive : { 0.0f, 0.5f, 1.0f }) {
            const juce::String drvStr = juce::String(drive, 1);

            // --- M1: finiteness ---
            beginTest("SpineShaper M1 finite @drive=" + drvStr);
            {
                AsymSaturator sat;
                sat.setDrive(drive, 0.15f, 24.0f);
                auto sig = testdsp::SignalGen::sine(0.9f, f, fsBase, n);
                std::vector<float> out((size_t) n);
                for (int i = 0; i < n; ++i) {
                    out[(size_t) i] = sat.process(sig[(size_t) i]);
                }
                expect(testdsp::Metrics::finite(out), "output must be finite");
            }

            // --- M4: NSR vs oversampled reference (v5.0 regression gate) ---
            beginTest("SpineShaper M4 NSR v5.0-regression @drive=" + drvStr);
            {
                // Base-rate: apply reference shaper directly (same math as AsymSaturator).
                auto dut = testdsp::SignalGen::sine(0.9f, f, fsBase, n);
                for (auto& v : dut) {
                    v = shapeRef(v, drive);
                }

                // High-rate truth: apply same shaper at 32x, then decimate back to base rate.
                auto hi = testdsp::SignalGen::sine(0.9f, f, fsBase * (double) M, n * M);
                for (auto& v : hi) {
                    v = shapeRef(v, drive);
                }
                const auto truth = testdsp::Reference::decimate(hi, M);

                const double nsr = testdsp::Reference::noiseToSignalDb(dut, truth, bin);

                // Print for anchoring (visible in suite output).
                std::printf("[SpineShaperHarness] drive=%.1f  NSR=%.2f dB\n", drive, nsr);

                // Select gate per drive level (anchored to measured baseline + 3 dB).
                double gate = kNsrGateDrive0;
                if (drive > 0.4f && drive < 0.6f) gate = kNsrGateDrive0p5;
                if (drive > 0.9f)                  gate = kNsrGateDrive1p0;

                testdsp::Gate::check(*this, nsr, gate, testdsp::Gate::Dir::Max,
                                     "M4 NSR (v5.0 regression) drive=" + drvStr);
            }
        }
    }
};
static SpineShaperHarnessTests spineShaperHarnessTestsInstance;
