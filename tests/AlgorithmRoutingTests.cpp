#include <juce_core/juce_core.h>
#include <vector>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/AlgorithmLibrary.h"
#include "../src/dsp/ParamSnapshot.h"

class AlgorithmRoutingTests : public juce::UnitTest {
public:
    AlgorithmRoutingTests() : juce::UnitTest("AlgorithmRouting") {}

    static constexpr double SR = 48000.0;
    static constexpr int N = 512;

    static std::vector<float> renderOnce(const ParamSnapshot& s) {
        Layer layer; layer.prepare(SR, N); layer.updateParameters(s);
        Voice v; v.setLayer(&layer); v.prepare(SR, N); v.noteOn(60, 1.0f);
        std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
        v.render(outL.data(), outR.data(), N);
        return outL;
    }

    static ParamSnapshot base() {
        ParamSnapshot s;
        s.oscWaveform = 0;                 // saw — harmonically rich
        s.svfCutoffHz = 800.0f; s.svfResonance = 0.2f;
        s.wsDrive = 0.9f; s.wsMix = 1.0f;  // strong shaping so order matters
        s.ampAttackS = 0.0001f; s.ampDecayS = 0.05f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;
        return s;
    }

    void runTest() override {
        beginTest("shaper: waveshaper drive changes the output (the shaper block is in the graph)");
        // The trimmed 'shaper' algorithm carries a Waveshaper block, so changing its
        // drive must change the rendered output.
        ParamSnapshot a = base(); a.algorithmId = (int) AlgorithmLibrary::indexOfId("shaper");
        a.wsDrive = 0.0f; auto a0 = renderOnce(a);
        a.wsDrive = 1.0f; auto a1 = renderOnce(a);
        double da = 0.0; for (int i = 0; i < N; ++i) da += std::abs(a0[i] - a1[i]);
        expect(da > 1e-3f, "shaper drive should change the output (diff " + juce::String(da, 6) + ")");

        beginTest("thru: waveshaper drive has no effect (no shaper block in the graph)");
        ParamSnapshot t = base(); t.algorithmId = (int) AlgorithmLibrary::indexOfId("thru");
        t.wsDrive = 0.0f; auto t0 = renderOnce(t);
        t.wsDrive = 1.0f; auto t1 = renderOnce(t);
        double dt = 0.0; for (int i = 0; i < N; ++i) dt += std::abs(t0[i] - t1[i]);
        expectWithinAbsoluteError((float) dt, 0.0f, 1e-5f);
    }
};

static AlgorithmRoutingTests algorithmRoutingTestsInstance;
