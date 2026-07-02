#include <juce_core/juce_core.h>
#include "testdsp/MethodAgreement.h"
#include "testdsp/Harmonics.h"
#include "testdsp/SteppedSine.h"
#include "testdsp/Sweep.h"
#include "testdsp/TransferFunction.h"
#include "testdsp/EssResponse.h"
#include "testdsp/ProcessAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>

struct MethodAgreementTests : public juce::UnitTest {
    MethodAgreementTests() : juce::UnitTest("MethodAgreement") {}

    static std::vector<double> logFreqs(double f0, double f1, int n) {
        std::vector<double> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = f0 * std::pow(f1 / f0, (double) i / (double) (n - 1));
        return v;
    }

    void runTest() override {
        const double sr = 96000.0;

        beginTest("maxMagDeltaDb is 0 for identical curves, exact for a known offset");
        {
            std::vector<double> a { -1.0, -6.0, -12.0 }, b = a;
            expectWithinAbsoluteError(testdsp::MethodAgreement::maxMagDeltaDb(a, b), 0.0, 0.0);
            for (auto& v : b) v -= 0.5;
            expectWithinAbsoluteError(testdsp::MethodAgreement::maxMagDeltaDb(a, b), 0.5, 1.0e-9);
        }

        beginTest("stepped-sine and ESS agree on a synthetic LP (the dual-method gate)");
        {
            auto freqs = logFreqs(50.0, 20000.0, 60);

            // Stepped-sine reference.
            testdsp::CellAdapter ca; ca.cutoff = 1000.0f; ca.res = 0.0f; ca.tap = NlSvfCell::LP;
            ca.prepare(sr);
            auto stepped = testdsp::SteppedSine::transfer(ca, freqs, sr, 0.05f);

            // ESS: drive the same filter with the sweep, calibrated against the identity
            // reference (EssResponse) so it lands in the same unity-gain (0 dB) frame as
            // the stepped-sine method. Bare deconvolution magnitude is offset by a constant
            // (sweep amplitude + band-limited-sinc gain); the reference divides it out.
            testdsp::CellAdapter cb; cb.cutoff = 1000.0f; cb.res = 0.0f; cb.tap = NlSvfCell::LP;
            cb.prepare(sr);
            auto ess = testdsp::EssResponse::measure(cb, 20.0, 24000.0, 1.0, sr, 0.05f, freqs);

            const double delta = testdsp::MethodAgreement::maxMagDeltaDb(stepped.magDb, ess.magDb);
            logMessage("dual-method max |dMag| = " + juce::String(delta, 3) + " dB");
            expect(delta < 1.0, "stepped vs ESS disagree by " + juce::String(delta, 3) + " dB (> 1 dB)");
        }

        beginTest("Harmonics::thdDb ~ floor for a clean passthrough");
        {
            struct Passthrough { void reset() {} void process(float*, int) {} };
            Passthrough p;
            expect(testdsp::Harmonics::thdDb(p, 1000.0, sr, 0.5f) < -60.0, "clean signal low THD");
        }

        // EssResponse's MAGNITUDE calibration is delay-invariant — this is exactly WHY the
        // magnitude path (and the magnitude-only method-agreement gate) is trustworthy. A pure
        // D-sample delay is all-pass (|H| = 0 dB everywhere); the reference calibration must
        // reproduce that regardless of the deconvolution's bulk IR latency.
        //
        // KNOWN LIMITATION (tracked follow-up): absolute PHASE / GROUP DELAY from EssResponse
        // are NOT yet trustworthy. The deconvolved IR sits at the linear-convolution centre
        // (~N samples), so its huge bulk latency wraps the phase far faster than the probe grid
        // can unwrap — a pure 30-sample delay reads group delay off by ~9x. The fix is to
        // time-align the IR (shift to a common reference origin) before the transfer function.
        // Until then the response.csv phaseRad/groupDelaySec columns are descriptive-only; no
        // gate depends on them, and the magnitude ruler below is unaffected.
        beginTest("EssResponse magnitude is delay-invariant (a pure delay reads ~0 dB all-pass)");
        {
            struct DelayAdapter {
                int D = 0; std::vector<float> buf; size_t pos = 0;
                void reset() { buf.assign((size_t) std::max(1, D), 0.0f); pos = 0; }
                void process(float* b, int n) {
                    if (D == 0) return;
                    for (int i = 0; i < n; ++i) {
                        const float out = buf[pos]; buf[pos] = b[i];
                        pos = (pos + 1) % buf.size(); b[i] = out;
                    }
                }
            };
            const int D = 30;
            DelayAdapter d; d.D = D; d.reset();
            auto freqs = logFreqs(300.0, 6000.0, 40);
            auto r = testdsp::EssResponse::measure(d, 20.0, 24000.0, 1.0, sr, 0.05f, freqs);
            double worstMag = 0.0;
            for (size_t i = 0; i < freqs.size(); ++i)
                if (freqs[i] >= 500.0 && freqs[i] <= 4000.0)
                    worstMag = std::max(worstMag, std::abs(r.magDb[i]));   // all-pass -> 0 dB
            logMessage("EssResponse pure-delay magnitude worst |dB| = " + juce::String(worstMag, 3)
                       + " (all-pass expects 0 dB; phase/GD time-alignment is a tracked follow-up)");
            expect(worstMag < 0.5, "pure delay is all-pass within 0.5 dB: " + juce::String(worstMag, 3));
        }
    }
};
static MethodAgreementTests methodAgreementTestsInstance;
