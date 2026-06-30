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
    }
};
static MethodAgreementTests methodAgreementTestsInstance;
