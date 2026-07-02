#pragma once
#include "OperatingPoint.h"
#include "DeviceUnderTest.h"
#include "../../src/dsp/spine/FilterModel.h"
#include "../../src/dsp/VoiceOversampler.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

namespace chz {

// One uniform socket to excite ANY FilterModel. Generic params (cutoff/res/drive)
// go through FilterModel::setCommon; model-specific mode/slope go through a
// Configurator bound at construction (this is where per-model knowledge lives).
// The OS axis is applied internally via VoiceOversampler: when osFactor > 1 the
// model runs at hostSampleRate*osFactor and the socket up/downsamples around it.
// Adapter contract for testdsp engines: reset(); process(float*, int).
class FilterUnderTest : public DeviceUnderTest {
public:
    // Returns false if the model does not support the requested Mode.
    using Configurator = std::function<bool(FilterModel&, Mode)>;

    FilterUnderTest(juce::String name, std::unique_ptr<FilterModel> model, Configurator cfg);

    juce::String name() const override { return name_; }
    DeviceKind   kind()       const override { return DeviceKind::TransferFunction; }
    Excitation   excitation() const override { return Excitation::InputSweep; }
    // NOT const: probing the configurator sets the model's mode/slope. setOperatingPoint()
    // always re-applies mode before measuring, so a prior supports() probe cannot leak into a
    // measurement — but never call supports() mid-sweep.
    bool supports(Mode m) override;

    void setOperatingPoint(const OperatingPoint& op) override;
    void reset() override;
    void process(float* mono, int n) override;   // base-rate in/out; OS applied internally

private:
    juce::String name_;
    std::unique_ptr<FilterModel> model_;
    Configurator cfg_;
    std::unique_ptr<FilterModel::State> state_;
    OperatingPoint op_;
    VoiceOversampler os_;
    std::vector<float> upL_, upR_, dnL_, dnR_;   // OS scratch (heap; MSVC stack-safe)
    static constexpr int kBlock = 1024;           // base-rate processing block
};

std::unique_ptr<FilterUnderTest> makeMoogFut();
std::unique_ptr<FilterUnderTest> makeHuggettFut();

} // namespace chz
