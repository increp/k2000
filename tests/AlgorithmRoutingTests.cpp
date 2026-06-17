#include <juce_core/juce_core.h>
#include <vector>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/AlgorithmLibrary.h"
#include "../src/params/ParamSnapshot.h"

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
        s.svfType = 0; s.svfCutoffHz = 800.0f; s.svfResonance = 0.2f;
        s.wsDrive = 0.9f; s.wsMix = 1.0f;  // strong shaping so order matters
        s.ampAttackS = 0.0001f; s.ampDecayS = 0.05f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;
        return s;
    }

    void runTest() override {
        beginTest("filter_then_shaper and shaper_then_filter both shaper-only in v5 (filter in spine)");
        // In v5 the graph SvfFilter was retired to the always-on spine; both
        // legacy algorithm ids now contain only a Waveshaper block, so their
        // outputs are identical.
        ParamSnapshot a = base(); a.algorithmId = (int) AlgorithmLibrary::indexOfId("filter_then_shaper");
        ParamSnapshot b = base(); b.algorithmId = (int) AlgorithmLibrary::indexOfId("shaper_then_filter");
        auto oa = renderOnce(a), ob = renderOnce(b);
        double diff = 0.0;
        for (int i = 0; i < N; ++i) diff += std::abs(oa[i] - ob[i]);
        expectWithinAbsoluteError((float) diff, 0.0f, 1e-5f);

        beginTest("filter_only: shaper drive has no effect");
        ParamSnapshot c = base(); c.algorithmId = (int) AlgorithmLibrary::indexOfId("filter_only");
        c.wsDrive = 0.0f; auto c0 = renderOnce(c);
        c.wsDrive = 1.0f; auto c1 = renderOnce(c);
        double d = 0.0; for (int i = 0; i < N; ++i) d += std::abs(c0[i] - c1[i]);
        expectWithinAbsoluteError((float) d, 0.0f, 1e-5f);

        beginTest("thru: waveshaper drive has no effect (shaper block not in graph)");
        ParamSnapshot t = base(); t.algorithmId = (int) AlgorithmLibrary::indexOfId("thru");
        t.wsDrive = 0.0f; auto t0 = renderOnce(t);
        t.wsDrive = 1.0f; auto t1 = renderOnce(t);
        double dt = 0.0; for (int i = 0; i < N; ++i) dt += std::abs(t0[i] - t1[i]);
        expectWithinAbsoluteError((float) dt, 0.0f, 1e-5f);
    }
};

static AlgorithmRoutingTests algorithmRoutingTestsInstance;
