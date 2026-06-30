#pragma once
#include "Sweep.h"
#include "TransferFunction.h"
#include <vector>

// testdsp::EssResponse — calibrated Farina ESS measurement of an adapter.
//
// Drives the adapter with an exponential sine sweep, deconvolves to the system impulse
// response, and REFERENCES it against the identity (raw-sweep) deconvolution so the result
// lands in the same unity-gain (0 dB) frame as testdsp::SteppedSine.
//
// Why the reference is needed: a bare deconvolution magnitude carries the sweep amplitude and
// the band-limited-sinc deconvolution gain (~+6 dB for a full-band sweep), so it is offset by
// a constant from the true |H(f)|. In the frequency domain the deconvolved IRs are
//   IR_sys(f) = System(f) * Sweep(f) * Inv(f),   IR_ref(f) = Sweep(f) * Inv(f),
// so IR_sys(f) / IR_ref(f) = System(f) EXACTLY. Subtracting the reference (in dB for
// magnitude, directly for phase, and for the constant deconvolution latency in group delay)
// removes the amplitude/sinc gain and any measurement-chain coloration.
//
// Adapter contract: void reset(); void process(float* buf, int n);  (same as SteppedSine).
namespace testdsp {

struct EssResponse {
    template <typename Adapter>
    static TransferFunction::Result measure(Adapter& a, double f0, double f1, double durSec,
                                            double sr, float amp,
                                            const std::vector<double>& freqs) {
        const auto sweep = Sweep::ess(f0, f1, durSec, sr, amp);
        const auto inv   = Sweep::inverseFilter(f0, f1, durSec, sr);

        // Reference (identity) deconvolution -> the calibration baseline.
        const auto irRef = Sweep::impulseResponse(sweep, inv);
        const auto ref   = TransferFunction::fromImpulse(irRef, sr, freqs);

        // System under test: the same sweep through the adapter.
        std::vector<float> out = sweep;
        a.reset();
        a.process(out.data(), (int) out.size());
        const auto irSys = Sweep::impulseResponse(out, inv);
        auto sys = TransferFunction::fromImpulse(irSys, sr, freqs);

        // Calibrate against the reference so an identity system reads 0 dB / 0 phase.
        const size_t n = sys.magDb.size();
        for (size_t i = 0; i < n; ++i) {
            sys.magDb[i]         -= ref.magDb[i];
            sys.phaseRad[i]      -= ref.phaseRad[i];
            sys.groupDelaySec[i] -= ref.groupDelaySec[i];
        }
        return sys;
    }
};

} // namespace testdsp
