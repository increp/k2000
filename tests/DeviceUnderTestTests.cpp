#include <juce_core/juce_core.h>
#include "characterization/DeviceUnderTest.h"
#include "testdsp/SteppedSine.h"
#include <cmath>
#include <vector>

using namespace chz;

// Minimal unity-gain passthrough device. Proves the DeviceUnderTest contract and
// that an L0 ruler engine can measure a device through the ABSTRACT BASE (virtual
// dispatch), independent of any real DSP.
struct PassthroughDevice : public DeviceUnderTest {
    void reset() override {}
    void process(float* /*mono*/, int /*n*/) override {}   // unity: leave buffer unchanged
    juce::String name() const override { return "passthrough"; }
    DeviceKind kind() const override { return DeviceKind::TransferFunction; }
    Excitation excitation() const override { return Excitation::InputSweep; }
    bool supports(Mode) override { return true; }
    void setOperatingPoint(const OperatingPoint&) override {}
};

struct DeviceUnderTestTests : public juce::UnitTest {
    DeviceUnderTestTests() : juce::UnitTest("DeviceUnderTest") {}

    void runTest() override {
        beginTest("descriptor reports kind / excitation / name");
        PassthroughDevice dev;
        DeviceUnderTest& dut = dev;                 // measured via the abstract base
        expect(dut.kind() == DeviceKind::TransferFunction);
        expect(dut.excitation() == Excitation::InputSweep);
        expectEquals(dut.name(), juce::String("passthrough"));

        beginTest("L0 ruler measures a device through the base (unity ~0 dB)");
        std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
        auto r = testdsp::SteppedSine::transfer(dut, freqs, 48000.0, 0.25f);
        for (double m : r.magDb)
            expect(std::abs(m) < 0.01, "unity gain should read ~0 dB");
    }
};

static DeviceUnderTestTests deviceUnderTestTestsInstance;
