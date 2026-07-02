#include <juce_core/juce_core.h>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"
#include "testdsp/Spectrum.h"
#include "testdsp/Level.h"
#include "testdsp/Metrics.h"
#include "testdsp/GoldenIO.h"
#include <cmath>
#include <vector>

// Sound/voicing drift fingerprints (anti-drift spec §5): five reference patches
// covering every load-bearing signal path, rendered through the REAL production
// Voice::render at every OS factor, reduced to band-level signatures and
// goldened. Tolerances are musical, not sample-exact, so denormal-grade noise
// passes and audible drift fails. Intentional voicing changes regenerate via
// BERNIE_UPDATE_GOLDEN=1 (the golden diff is the audit trail; failures name
// patch + tier + metric).

namespace {

struct Patch { const char* name; ParamSnapshot s; };

ParamSnapshot base() {
    ParamSnapshot s {};
    s.oscWaveform = 0;                 // saw
    s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
    s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;
    s.algorithmId = 1;                 // "thru" — isolate the spine unless a patch says otherwise
    s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
    s.spineModel = 0; s.spineSlope = 1;
    return s;
}

std::vector<Patch> patches() {
    std::vector<Patch> v;
    { auto s = base(); v.push_back({ "init_saw", s }); }
    { auto s = base(); s.svfCutoffHz = 1200.0f; s.svfResonance = 0.7f; s.spineDrive = 0.5f;
      v.push_back({ "hug_lp24_drive", s }); }
    { auto s = base(); s.svfCutoffHz = 800.0f; s.svfResonance = 0.4f;
      s.huggettRouting = 7;            // "LP+HP" (see Parameters.cpp choice order)
      s.spineSeparationOct = 2.0f;
      v.push_back({ "hug_lp_hp_sep", s }); }
    { auto s = base(); s.spineModel = 1; s.moogMode = 0;
      s.svfCutoffHz = 900.0f; s.svfResonance = 0.8f;
      s.moogBassAmount = 0.7f; s.moogBassWave = 0; s.moogBassOctave = 1;
      v.push_back({ "moog_lp24_bass", s }); }
    { auto s = base(); s.algorithmId = 0;           // "shaper" in the graph
      s.wsDrive = 0.6f; s.wsMix = 1.0f;
      s.hpCutoffHz = 500.0f; s.hpSlope = 1;
      s.svfCutoffHz = 5000.0f; s.svfResonance = 0.2f;
      v.push_back({ "hp_shaper", s }); }
    return v;
}

} // namespace

struct RenderFingerprintTests : public juce::UnitTest {
    RenderFingerprintTests() : juce::UnitTest("RenderFingerprint") {}

    void runTest() override {
        const double sr = 48000.0;
        const int    N  = 512;
        const int    W  = 1 << 15;          // 32768-sample analysis window (last ~0.68 s)
        const int    blocks = (W / N) + 32; // warm-up + window

        testdsp::GoldenSet gs("fingerprints/baseline");

        for (const auto& p : patches()) {
            for (int os : { 1, 2, 4, 8 }) {
                beginTest(juce::String(p.name) + " @ os" + juce::String(os));

                Layer layer;
                layer.prepare(sr * os, N * os);
                layer.updateParameters(p.s);
                Voice v;
                v.setLayer(&layer);
                v.prepare(sr, N, os);
                v.noteOn(57, 1.0f);         // A3, 110 Hz

                std::vector<float> cap; cap.reserve((size_t) (blocks * N));
                std::vector<float> l((size_t) N), r((size_t) N);
                for (int b = 0; b < blocks; ++b) {
                    std::fill(l.begin(), l.end(), 0.0f);
                    std::fill(r.begin(), r.end(), 0.0f);
                    v.render(l.data(), r.data(), N);
                    cap.insert(cap.end(), l.begin(), l.end());
                }
                std::vector<float> win(cap.end() - W, cap.end());
                expect(testdsp::Metrics::finite(win), "render finite");

                auto mag = testdsp::Spectrum::magnitude(win);
                const juce::String kb = "fp/" + juce::String(p.name) + "/os" + juce::String(os);

                // 10 octave bands centered 31.25 Hz .. 16 kHz; Parseval band RMS in dBFS.
                for (int band = 0; band < 10; ++band) {
                    const double fc = 31.25 * std::pow(2.0, band);
                    const int b0 = std::max(1, (int) std::floor(fc / std::sqrt(2.0) * W / sr));
                    const int b1 = std::min((int) mag.size() - 1,
                                            (int) std::ceil(fc * std::sqrt(2.0) * W / sr));
                    double acc = 0.0;
                    for (int b = b0; b <= b1; ++b)
                        acc += 2.0 * (double) mag[(size_t) b] * mag[(size_t) b];
                    const double rmsDb = 20.0 * std::log10(std::max(std::sqrt(acc) / (double) W, 1.0e-9));
                    gs.check(*this, kb + "/band" + juce::String(band), rmsDb, 0.5);
                }
                gs.check(*this, kb + "/peak_dbfs", testdsp::Level::peakDbfs(win), 0.25);
                const int fundBin = (int) std::lround(110.0 * W / sr);   // ~75
                gs.check(*this, kb + "/thd_db",
                         testdsp::Metrics::thdPlusNDb(mag, fundBin), 1.0);
            }
        }
        gs.flush();
    }
};

static RenderFingerprintTests renderFingerprintTestsInstance;
