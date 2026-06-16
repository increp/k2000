#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/PluginProcessor.h"
#include "../src/params/Parameters.h"
#include "../src/gui/ParamBinder.h"

class ParamBinderTests : public juce::UnitTest {
public:
    ParamBinderTests() : juce::UnitTest("ParamBinder") {}

    void runTest() override {
        beginTest("rebinding a slider leaves the previously-bound param untouched");
        {
            K2000AudioProcessor p;
            auto& apvts = p.apvts();
            const juce::String idX = params::layerIds(0).filterCutoff;
            const juce::String idY = params::layerIds(1).filterCutoff;

            // Distinct, in-range values (cutoff range is 20..20000 Hz).
            apvts.getParameter(idX)->setValueNotifyingHost(
                apvts.getParameter(idX)->convertTo0to1(1000.0f));
            apvts.getParameter(idY)->setValueNotifyingHost(
                apvts.getParameter(idY)->convertTo0to1(8000.0f));
            const float xBefore = apvts.getRawParameterValue(idX)->load();

            juce::Slider s;
            ParamBinder binder(apvts);
            binder.bind(s, idX);   // slider syncs to X (~1000)
            binder.bind(s, idY);   // slider jumps to Y (~8000) — must NOT write into X

            expectWithinAbsoluteError(apvts.getRawParameterValue(idX)->load(), xBefore, 1.0f);
            expectWithinAbsoluteError(apvts.getRawParameterValue(idY)->load(), 8000.0f, 5.0f);
        }

        beginTest("clear() detaches: moving the control no longer drives the param");
        {
            K2000AudioProcessor p;
            auto& apvts = p.apvts();
            const juce::String idX = params::layerIds(0).filterCutoff;
            apvts.getParameter(idX)->setValueNotifyingHost(
                apvts.getParameter(idX)->convertTo0to1(1000.0f));

            juce::Slider s;
            ParamBinder binder(apvts);
            binder.bind(s, idX);
            binder.clear();

            s.setValue(5000.0, juce::sendNotificationSync);  // detached: should be ignored
            expectWithinAbsoluteError(apvts.getRawParameterValue(idX)->load(), 1000.0f, 1.0f);
        }
    }
};

static ParamBinderTests paramBinderTestsInstance;
