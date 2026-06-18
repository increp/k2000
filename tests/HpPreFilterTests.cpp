#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettHpStage.h"
#include <cmath>
#include <memory>

class HpPreFilterTests : public juce::UnitTest {
public:
    HpPreFilterTests() : juce::UnitTest("HpPreFilter") {}
    void runTest() override {
        beginTest("HP stage passes highs, attenuates lows; 24 dB steeper than 12");
        {
            HuggettHpStage hp; hp.prepare(48000.0);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            auto mag = [&](double f, HuggettHpStage::Slope slope){
                hp.setParams(1000.0f, 0.0f, slope, 0.0f); hp.reset(*st);
                const int N=8192; float peak=0;
                for (int i=0;i<N;++i){ float x=std::sin(2.0*juce::MathConstants<double>::pi*f*i/48000.0);
                    float l=x,r=x; hp.processStereo(*st,&l,&r,1); if(i>N/2) peak=std::max(peak,std::abs(l)); }
                return peak;
            };
            expect(mag(8000.0, HuggettHpStage::Slope::db12) > 0.7f, "highs pass");
            float low12 = mag(200.0, HuggettHpStage::Slope::db12);
            float low24 = mag(200.0, HuggettHpStage::Slope::db24);
            expect(low12 < 0.1f, "lows cut (12dB): " + juce::String(low12));
            expect(low24 < low12, "24 dB steeper below corner");
            expect(low24 < 0.04f, "24 dB strongly cuts lows: " + juce::String(low24));
        }
        beginTest("HP self-oscillation bounded at max resonance");
        {
            HuggettHpStage hp; hp.prepare(48000.0);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            hp.setParams(1200.0f, 1.0f, HuggettHpStage::Slope::db24, 0.0f); hp.reset(*st);
            float l=1.0f,r=1.0f; hp.processStereo(*st,&l,&r,1);
            float peak=0; bool nan=false;
            for (int i=0;i<24000;++i){ float a=0,b=0; hp.processStereo(*st,&a,&b,1);
                if(!std::isfinite(a)) { nan=true; } if(i>2000) peak=std::max(peak,std::abs(a)); }
            expect(!nan && peak < 4.0f, "bounded: " + juce::String(peak));
        }
    }
};
static HpPreFilterTests hpPreFilterTestsInstance;
