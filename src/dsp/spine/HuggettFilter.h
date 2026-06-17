#pragma once
#include "FilterModel.h"
#include "TptSvfCell.h"

// Linear Huggett model: two TPT cells. Mode selects the tap; Slope runs one
// (12 dB) or both (24 dB) cells in series; Separation offsets cell B's cutoff.
// Nonlinear stages (Plan 2) and hot-swap (Plan 3) are out of scope here.
class HuggettFilter : public FilterModel {
public:
    enum class Mode  { LP, BP, HP };
    enum class Slope { db12, db24 };

    struct VoiceState : public FilterModel::State {
        TptSvfCell a, b;
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    State* makeState() const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setMode(Mode m) noexcept       { mode_ = m; }
    void setSlope(Slope s) noexcept     { slope_ = s; }
    void setSeparation(float oct) noexcept { separationOct_ = oct; }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    static int tapForMode(Mode m) noexcept {
        switch (m) { case Mode::BP: return TptSvfCell::BP;
                     case Mode::HP: return TptSvfCell::HP;
                     case Mode::LP: default: return TptSvfCell::LP; }
    }
    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, separationOct_ = 0.0f;
    Mode   mode_  = Mode::LP;
    Slope  slope_ = Slope::db24;
};
