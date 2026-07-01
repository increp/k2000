#pragma once
#include "OperatingPoint.h"
#include <juce_core/juce_core.h>

// chz -- the device-agnostic characterization socket. Any measurable device
// (filter, oscillator, or hardware capture) implements DeviceUnderTest. The L0
// ruler engines (SteppedSine / ESS) only need reset()+process(); the capability
// descriptor (kind / excitation / supports) tells the runner HOW to drive it.
namespace chz {

// What kind of device -- determines how it is excited and what a battery measures.
enum class DeviceKind {
    TransferFunction,   // input -> output (filters): drive a signal through it
    Generator,          // produces output on its own (oscillators): trigger & record
    Captured            // pre-recorded / hardware capture (SP-D): replay captured audio
};

// How the runner excites a device. One driver per mode (only InputSweep is live in M1).
enum class Excitation {
    InputSweep,         // feed a probe / sweep through it (filters)
    Trigger,            // trigger a note / impulse and record the emission (oscillators)
    MidiCapture         // send MIDI to real hardware and capture (SP-D)
};

struct DeviceUnderTest {
    virtual ~DeviceUnderTest() = default;

    // L0 adapter contract -- any ruler engine can measure it.
    // For a Generator the input buffer is ignored and overwritten with output.
    virtual void reset()                     = 0;
    virtual void process(float* mono, int n) = 0;

    // Capability descriptor.
    virtual juce::String name()       const  = 0;
    virtual DeviceKind   kind()       const  = 0;
    virtual Excitation   excitation() const  = 0;
    virtual bool         supports(Mode m)    = 0;   // non-const: may set the model's mode
    virtual void         setOperatingPoint(const OperatingPoint& op) = 0;
};

} // namespace chz
