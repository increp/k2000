#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/Program.h"
#include "../src/VoiceManager.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

namespace {
ParamSnapshot dspBase() {
    ParamSnapshot s;
    s.oscWaveform = 3; s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
    s.wsDrive = 0.0f; s.wsMix = 0.0f;
    s.ampAttackS = 0.0001f; s.ampDecayS = 0.05f; s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;
    return s;
}
double energy(const std::vector<float>& b) { double e = 0; for (float x : b) e += x*x; return e; }
}  // namespace

class MultiLayerTests : public juce::UnitTest {
public:
    MultiLayerTests() : juce::UnitTest("MultiLayer") {}
    static constexpr double SR = 48000.0;
    static constexpr int N = 256;

    void runTest() override {
        beginTest("split: a note fires only the layer whose key range contains it");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(1).layer.updateParameters(dspBase());
            // layer0 = lower half (audible), layer1 = upper half (silent: level 0).
            // The silent upper layer proves routing: a high note that wrongly hit
            // layer0 would be audible; routed to layer1 it is silent.
            prog.slot(0).routing = LayerRouting{true, 0, 59, 1, 127, 0};
            prog.slot(1).routing = LayerRouting{true, 60, 127, 1, 127, 0};
            prog.slot(0).layer.setLevel(1.0f);
            prog.slot(1).layer.setLevel(0.0f);

            auto render = [&](int note) {
                VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
                std::vector<float> out(N, 0.0f);
                vm.renderBlock(out.data(), N, midi);
                return energy(out);
            };
            expect(render(48) > 1e-5, "low note routes to audible layer0");
            expectWithinAbsoluteError((float) render(70), 0.0f, 1e-9f);  // high → silent layer1 only
        }

        beginTest("stacking: a note matching both layers fires both (more energy)");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(1).layer.updateParameters(dspBase());
            prog.slot(0).layer.setLevel(1.0f); prog.slot(1).layer.setLevel(1.0f);

            auto render = [&](bool stackBoth) {
                prog.slot(0).routing = LayerRouting{true,      0, 127, 1, 127, 0};
                prog.slot(1).routing = LayerRouting{stackBoth, 0, 127, 1, 127, 0};
                VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
                std::vector<float> out(N, 0.0f);
                vm.renderBlock(out.data(), N, midi);
                return energy(out);
            };
            // Two identical, phase-coherent voices ≈ 2x amplitude ≈ 4x energy.
            expect(render(true) > render(false) * 2.0, "stacking both layers adds a voice");
        }

        beginTest("layer level scales contribution");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(0).routing = LayerRouting{true, 0, 127, 1, 127, 0};
            prog.slot(1).routing = LayerRouting{false, 0, 127, 1, 127, 0};

            auto render = [&](float lvl) {
                prog.slot(0).layer.setLevel(lvl);
                VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
                std::vector<float> out(N, 0.0f);
                vm.renderBlock(out.data(), N, midi);
                return energy(out);
            };
            expect(render(1.0f) > render(0.25f) * 2.0, "higher level → more energy");
        }

        beginTest("disabled layer produces no voices");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(0).routing = LayerRouting{false, 0, 127, 1, 127, 0};
            prog.slot(1).routing = LayerRouting{false, 0, 127, 1, 127, 0};
            VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
            std::vector<float> out(N, 0.0f);
            vm.renderBlock(out.data(), N, midi);
            expectWithinAbsoluteError((float) energy(out), 0.0f, 1e-9f);
        }
    }
};

static MultiLayerTests multiLayerTestsInstance;
