#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"
#include "../src/dsp/spine/HuggettHpStage.h"
#include "../src/dsp/spine/MoogLadder.h"

// End-to-end coverage of the Layer-driven audio path: build a Layer, prepare
// it, drive a Voice through it with a known snapshot, assert audio characteristics.
class LayerTests : public juce::UnitTest {
public:
    LayerTests() : juce::UnitTest("Layer") {}

    void runTest() override {
        const double sr = 48000.0;
        const int    N  = 256;

        beginTest("Layer prepares and a Voice renders through it");
        {
            Layer layer;
            layer.prepare(sr, N);

            ParamSnapshot s {};
            s.oscWaveform = 3;          // sine
            s.oscCoarse = 0; s.oscFine = 0;
            s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
            s.wsDrive = 0.0f; s.wsMix = 0.0f;
            s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
            s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;
            s.masterGainDb = 0.0f;
            layer.updateParameters(s);

            Voice v;
            v.setLayer(&layer);
            v.prepare(sr, N);
            v.noteOn(69, 1.0f);    // A4

            std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
            v.render(outL.data(), outR.data(), N);

            float energy = 0.0f;
            for (float x : outL) energy += x * x;
            expectGreaterThan(energy, 1e-4f, "Layer-driven Voice should produce audio");
        }

        beginTest("Layer exposes a configured HP stage");
        {
            Layer layer; layer.prepare(48000.0, 256);
            ParamSnapshot s; s.hpCutoffHz = 2000.0f; s.hpResonance = 0.0f;   // cutoff>0 = HP on
            s.hpSlope = 1;
            layer.updateParameters(s);
            expect(layer.hpStage() != nullptr, "hp stage present");
            // Filter a low tone through the exposed HP stage -> attenuated.
            std::unique_ptr<HuggettHpStage::State> st(layer.hpStage()->makeState());
            layer.hpStage()->reset(*st);
            const int Nhp=8192; float peak=0;
            for (int i=0;i<Nhp;++i){ float x=std::sin(2.0*juce::MathConstants<double>::pi*200.0*i/48000.0);
                float l=x,r=x; layer.hpStage()->processStereo(*st,&l,&r,1); if(i>Nhp/2) peak=std::max(peak,std::abs(l)); }
            expect(peak < 0.6f, "HP attenuates 200 Hz: " + juce::String(peak));
        }

        beginTest("pre-built models: spineModel(id) is stable across updateParameters");
        {
            Layer layer; layer.prepare(48000.0, 512);
            const FilterModel* m0a = layer.spineModel(0);
            ParamSnapshot s; s.spineModel = 0;
            layer.updateParameters(s);
            const FilterModel* m0b = layer.spineModel(0);
            expect(m0a != nullptr, "model 0 not built");
            expect(m0a == m0b, "model instance was rebuilt on update (should be pre-built/stable)");
            expect(layer.spineModel() == m0a, "current model should be id 0");
        }

        beginTest("Layer routes Moog params to the Moog instance and processes through it");
        {
            // Block size and sample rate used throughout this test
            const double testSr = 48000.0;
            const int    testN  = 512;

            Layer layer; layer.prepare(testSr, testN);
            ParamSnapshot s;
            s.spineModel   = 1;          // select Moog
            s.svfCutoffHz  = 1000.0f;    // cutoff well above the test tone (100 Hz)
            s.svfResonance = 0.2f;

            // -- Type check: active model is a MoogLadder --
            s.moogMode = 0;
            layer.updateParameters(s);
            expect(layer.spineModel() != nullptr, "no spine model after selecting Moog");
            auto* mgLP = dynamic_cast<const MoogLadder*>(layer.spineModel());
            expect(mgLP != nullptr, "active model is not MoogLadder");

            // Helper: compute RMS of a 100 Hz sine processed through the given model
            // using a fresh state.  Uses enough samples to let the filter settle.
            auto computeRms = [&](const MoogLadder* mg) -> float {
                const int warmup = testN / 2;   // discard first half (transient)
                std::vector<float> L(testN), R(testN);
                for (int i = 0; i < testN; ++i) {
                    float x = std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * i / testSr);
                    L[i] = x; R[i] = x;
                }
                std::unique_ptr<FilterModel::State> st(mg->makeState());
                mg->reset(*st);
                mg->processStereo(*st, L.data(), R.data(), testN);
                float sum = 0.0f;
                for (int i = warmup; i < testN; ++i)
                    sum += L[i] * L[i];
                return std::sqrt(sum / float(testN - warmup));
            };

            // -- HP mode: 100 Hz (well below 1 kHz cutoff) should be strongly attenuated --
            s.moogMode = 2;              // HP
            layer.updateParameters(s);
            auto* mgHP = dynamic_cast<const MoogLadder*>(layer.spineModel());
            expect(mgHP != nullptr, "Moog not present after HP mode update");
            const float rmsHP = computeRms(mgHP);

            // -- LP mode: 100 Hz (well below 1 kHz cutoff) should pass through --
            s.moogMode = 0;              // LP
            layer.updateParameters(s);
            auto* mgLP2 = dynamic_cast<const MoogLadder*>(layer.spineModel());
            expect(mgLP2 != nullptr, "Moog not present after LP mode update");
            const float rmsLP = computeRms(mgLP2);

            // LP should pass the low tone; HP should reject it.
            // If the dispatch block is removed, setMode() is never called and both
            // runs produce identical output -> rmsHP == rmsLP -> assertion fails.
            expect(rmsLP > 1e-4f, "LP mode must pass 100 Hz tone (rmsLP=" + juce::String(rmsLP) + ")");
            expect(rmsHP < rmsLP * 0.5f,
                "HP must attenuate 100 Hz vs LP: rmsHP=" + juce::String(rmsHP)
                + " rmsLP=" + juce::String(rmsLP));
        }

        beginTest("Lowering the cutoff drops high-note energy");
        {
            // Same Layer config save the cutoff; a high note through a low LP
            // cutoff should carry substantially less energy than through a high one.
            ParamSnapshot s {};
            s.oscWaveform = 3;
            s.svfResonance = 0.0f;
            s.wsDrive = 0.0f; s.wsMix = 0.0f;
            s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
            s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;

            Layer layer;
            layer.prepare(sr, N);

            s.svfCutoffHz = 100.0f;
            layer.updateParameters(s);
            Voice vLow; vLow.setLayer(&layer); vLow.prepare(sr, N); vLow.noteOn(108, 1.0f);
            std::vector<float> outLowL(N, 0.0f), outLowR(N, 0.0f);
            vLow.render(outLowL.data(), outLowR.data(), N);

            s.svfCutoffHz = 20000.0f;
            layer.updateParameters(s);
            Voice vHigh; vHigh.setLayer(&layer); vHigh.prepare(sr, N); vHigh.noteOn(108, 1.0f);
            std::vector<float> outHighL(N, 0.0f), outHighR(N, 0.0f);
            vHigh.render(outHighL.data(), outHighR.data(), N);

            float eLow = 0.0f, eHigh = 0.0f;
            for (int i = 0; i < N; ++i) {
                eLow  += outLowL[i]  * outLowL[i];
                eHigh += outHighL[i] * outHighL[i];
            }
            expectGreaterThan(eHigh, eLow * 2.0f,
                "High cutoff should yield substantially more energy than low cutoff for a high note");
        }
    }
};

static LayerTests layerTestsInstance;
