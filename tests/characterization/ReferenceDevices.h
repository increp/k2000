#pragma once
#include "DeviceUnderTest.h"
#include <cmath>

// chz -- synthetic reference devices with answers known by construction. Used to
// prove the ruler + level/THD extractors read true before trusting them on real DSP.
namespace chz {

// Analytic RBJ low-pass biquad. Doubles as the first non-Huggett/Moog device through
// the DeviceUnderTest contract (extensibility proof). trueMagDb(f) is the EXACT
// z-domain magnitude, the ground truth the ruler must recover.
class AnalyticBiquad : public DeviceUnderTest {
public:
    void reset() override { z1_ = z2_ = 0.0f; }

    void process(float* mono, int n) override {
        for (int i = 0; i < n; ++i) {
            const float x = mono[i];
            const float y = b0_ * x + z1_;
            z1_ = b1_ * x - a1_ * y + z2_;
            z2_ = b2_ * x - a2_ * y;
            mono[i] = y;
        }
    }

    juce::String name()       const override { return "ref_biquad"; }
    DeviceKind   kind()       const override { return DeviceKind::TransferFunction; }
    Excitation   excitation() const override { return Excitation::InputSweep; }
    bool         supports(Mode) override { return true; }

    void setOperatingPoint(const OperatingPoint& op) override {
        sr_ = op.hostSampleRate;
        const double Q  = 0.5 + op.resonance * 9.5;     // res in [0,1] -> Q in [0.5, 10]
        const double w0 = 2.0 * juce::MathConstants<double>::pi * op.cutoffHz / sr_;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double b0 = (1.0 - cw) * 0.5, b1 = 1.0 - cw, b2 = (1.0 - cw) * 0.5;
        const double a0 = 1.0 + alpha,      a1 = -2.0 * cw, a2 = 1.0 - alpha;
        b0_ = float(b0 / a0); b1_ = float(b1 / a0); b2_ = float(b2 / a0);
        a1_ = float(a1 / a0); a2_ = float(a2 / a0);
        reset();
    }

    // Exact |H(e^{jw})| in dB, w = 2*pi*f/sr. H = (b0+b1 z^-1+b2 z^-2)/(1+a1 z^-1+a2 z^-2).
    // Evaluated from the SAME quantized float coefficients the process() loop uses (b0_..a2_),
    // by design — so the trust-gate tolerance bounds RULER error alone, not a coefficient
    // mismatch. Do not 'optimize' this to recompute in double precision: that would silently
    // loosen the gate's meaning.
    double trueMagDb(double f) const {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr_;
        const double c1 = std::cos(w),  s1 = std::sin(w);
        const double c2 = std::cos(2*w), s2 = std::sin(2*w);
        const double nre = b0_ + b1_ * c1 + b2_ * c2;   // e^{-jw}: real=cos, imag=-sin
        const double nim = -(b1_ * s1 + b2_ * s2);
        const double dre = 1.0 + a1_ * c1 + a2_ * c2;
        const double dim = -(a1_ * s1 + a2_ * s2);
        const double num = std::sqrt(nre*nre + nim*nim);
        const double den = std::sqrt(dre*dre + dim*dim);
        return 20.0 * std::log10(std::max(num / std::max(den, 1e-30), 1e-30));
    }

private:
    double sr_ = 48000.0;
    float  b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float  z1_ = 0.0f, z2_ = 0.0f;
};

// Memoryless cubic: y = x + c3*x^3. For a sine A*sin(wt), x^3 = A^3*(3/4 sin - 1/4 sin3),
// so the output is a fundamental of amplitude (A + 3/4 c3 A^3) plus a single 3rd harmonic
// of amplitude (1/4 c3 A^3) -- a THD known in closed form. Plain reset()/process() adapter.
class CubicNonlinearity {
public:
    float c3 = 0.0f;

    void reset() {}
    void process(float* mono, int n) {
        for (int i = 0; i < n; ++i) {
            const float x = mono[i];
            mono[i] = x + c3 * x * x * x;
        }
    }

    // Closed-form THD (3rd / fundamental amplitude), dB.
    // THD of a memoryless cubic is LEVEL-DEPENDENT: pass the SAME amplitude used to excite
    // the device (the amp given to Harmonics::thdDb). A different amp yields a silently wrong
    // 'truth'.
    double trueThdDb(float amp) const {
        const double A = amp;
        const double fund = A + 0.75 * (double) c3 * A * A * A;
        const double h3   = 0.25 * (double) c3 * A * A * A;
        return 20.0 * std::log10(std::abs(h3) / std::abs(fund));
    }
};

// Engineered aliasing case: ADDS a fixed inharmonic tone (aliasHz, aliasAmp) to
// whatever passes through — a synthetic stand-in for aliasing foldback whose
// frequency and level are known by construction. Plain reset()/process() adapter.
class EngineeredAliaser {
public:
    double sr       = 48000.0;
    double aliasHz  = 300.0;
    float  aliasAmp = 0.01f;

    void reset() { phase_ = 0.0; }
    void process(float* mono, int n) {
        const double inc = 2.0 * juce::MathConstants<double>::pi * aliasHz / sr;
        for (int i = 0; i < n; ++i) {
            mono[i] += aliasAmp * (float) std::sin(phase_);
            phase_ += inc;
            if (phase_ > 2.0 * juce::MathConstants<double>::pi)
                phase_ -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

private:
    double phase_ = 0.0;
};

// Calibrated-tone Generator: emits a sine of exactly known amplitude — the
// absolute-level trust anchor (spec §5.1 "calibrated tone") and the first
// Generator/Trigger device through the DeviceUnderTest contract. M4 Generator
// convention: OperatingPoint::cutoffHz is the tone frequency, hostSampleRate the
// rate (generalized OperatingPoint lands in SP-C).
class CalibratedToneRef : public DeviceUnderTest {
public:
    void setToneDbfs(double dbfs) { amp_ = (float) std::pow(10.0, dbfs / 20.0); }

    void reset() override { phase_ = 0.0; }

    // Generator: input contents ignored, buffer OVERWRITTEN with the emission.
    void process(float* mono, int n) override {
        const double inc = 2.0 * juce::MathConstants<double>::pi * freqHz_ / sr_;
        for (int i = 0; i < n; ++i) {
            mono[i] = amp_ * (float) std::sin(phase_);
            phase_ += inc;
            if (phase_ > 2.0 * juce::MathConstants<double>::pi)
                phase_ -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

    juce::String name()       const override { return "ref_tone"; }
    DeviceKind   kind()       const override { return DeviceKind::Generator; }
    Excitation   excitation() const override { return Excitation::Trigger; }
    bool         supports(Mode) override { return true; }

    void setOperatingPoint(const OperatingPoint& op) override {
        freqHz_ = op.cutoffHz;
        sr_     = op.hostSampleRate;
        reset();
    }

private:
    double sr_ = 48000.0, freqHz_ = 1000.0, phase_ = 0.0;
    float  amp_ = 0.12589254f;   // -18 dBFS default (the documented calibration tone)
};

} // namespace chz
