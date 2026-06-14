#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"

// Verifies the v1→v2 preset migration shim (ADR 0007).
//
// A real v1 preset is a binary blob produced by AudioProcessor::copyXmlToBinary
// (JUCE magic header + gzipped XML), not plain text — a hand-written .xml file
// would fail getXmlFromBinary and silently no-op. So rather than committing a
// fragile binary fixture, this test reconstructs the exact v1 on-disk format
// in-memory using the same encoder a v1.0.0 build used: a <K2000Root> (with no
// `v` attribute) wrapping <Slots> and <Params><PARAMS> carrying the old flat
// param IDs. If migration regresses, this test fails.
class PresetMigrationTests : public juce::UnitTest {
public:
    PresetMigrationTests() : juce::UnitTest("PresetMigration") {}

    // Builds a v1-format state blob with the given old-id/value pairs.
    static juce::MemoryBlock makeV1Preset(
        const std::vector<std::pair<juce::String, double>>& params) {
        juce::XmlElement root("K2000Root");  // note: no "v" attribute (v1)

        auto* slots = root.createNewChildElement("Slots");
        auto* s0 = slots->createNewChildElement("Slot");
        s0->setAttribute("index", 0);
        s0->setAttribute("type", "svf_filter");
        auto* s1 = slots->createNewChildElement("Slot");
        s1->setAttribute("index", 1);
        s1->setAttribute("type", "waveshaper");

        auto* wrapper = root.createNewChildElement("Params");
        auto* paramsRoot = wrapper->createNewChildElement("PARAMS");
        for (const auto& kv : params) {
            auto* p = paramsRoot->createNewChildElement("PARAM");
            p->setAttribute("id", kv.first);
            p->setAttribute("value", kv.second);
        }

        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary(root, mb);
        return mb;
    }

    void runTest() override {
        beginTest("Loading a v1 preset rewrites IDs and applies values");
        {
            K2000AudioProcessor proc;
            proc.prepareToPlay(48000.0, 256);

            const auto v1 = makeV1Preset({
                {"osc.coarse",   0.0},
                {"slot0.cutoff", 1000.0},
                {"amp.attack",   0.01},
                {"master.gain",  0.0},
            });
            proc.setStateInformation(v1.getData(), (int) v1.getSize());

            auto& tree = proc.apvts();
            auto* cutoff = tree.getRawParameterValue("layer.slot0.cutoff");
            expect(cutoff != nullptr, "migrated cutoff id should resolve");
            expectWithinAbsoluteError(cutoff->load(), 1000.0f, 0.5f);

            auto* attack = tree.getRawParameterValue("layer.amp.attack");
            expect(attack != nullptr, "migrated attack id should resolve");
            expectWithinAbsoluteError(attack->load(), 0.01f, 0.001f);
        }

        beginTest("Saving from v2 sets v=2 attribute and round-trips");
        {
            K2000AudioProcessor proc;
            proc.prepareToPlay(48000.0, 256);

            juce::MemoryBlock saved;
            proc.getStateInformation(saved);

            std::unique_ptr<juce::XmlElement> xml(
                juce::AudioProcessor::getXmlFromBinary(saved.getData(),
                                                       (int) saved.getSize()));
            expect(xml != nullptr, "saved state should decode");
            expectEquals(xml->getIntAttribute("v", 1), 2);

            // A v2 blob skips the shim and still loads cleanly.
            K2000AudioProcessor proc2;
            proc2.prepareToPlay(48000.0, 256);
            proc2.setStateInformation(saved.getData(), (int) saved.getSize());
        }
    }
};

static PresetMigrationTests presetMigrationTestsInstance;
