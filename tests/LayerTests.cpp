#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/params/ParamSnapshot.h"
#include "../src/dsp/spine/HuggettHpStage.h"

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
            s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
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
            ParamSnapshot s; s.hpEnable = 1; s.hpCutoffHz = 2000.0f; s.hpResonance = 0.0f;
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

        beginTest("Lowering the cutoff drops high-note energy");
        {
            // Same Layer config save the cutoff; a high note through a low LP
            // cutoff should carry substantially less energy than through a high one.
            ParamSnapshot s {};
            s.oscWaveform = 3;
            s.svfType = 0; s.svfResonance = 0.0f;
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
