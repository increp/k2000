#pragma once
#include "FilterModel.h"
#include "NlSvfCell.h"
#include "AsymSaturator.h"
#include "DcBlocker.h"

// Nonlinear Huggett: two NlSvfCells + asymmetric pre/post drive shapers (ADAA) +
// a self-limiting resonance saturator + an output DC blocker. setCommon's `drive`
// is the pre-filter input drive; post-drive is a Huggett-bank param.
class HuggettFilter : public FilterModel {
public:
    enum class Mode  { LP, BP, HP };
    enum class Slope { db12, db24 };

    struct VoiceState : public FilterModel::State {
        NlSvfCell a, b;
        DcBlocker dc;
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    State* makeState() const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setMode(Mode m) noexcept       { mode_ = m; }
    void setSlope(Slope s) noexcept     { slope_ = s; }
    void setSeparation(float oct) noexcept { separationOct_ = oct; }
    void setPostDrive(float drive01) noexcept { postDrive_ = drive01; postSat_.setDrive(drive01, kPostBias, kPostDriveDb); }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    static int tapForMode(Mode m) noexcept {
        switch (m) { case Mode::BP: return NlSvfCell::BP;
                     case Mode::HP: return NlSvfCell::HP;
                     case Mode::LP: default: return NlSvfCell::LP; }
    }
    static constexpr float kPreDriveDb  = 30.0f;   // CALIB
    static constexpr float kPostDriveDb = 24.0f;   // CALIB
    static constexpr float kPreBias  = 0.25f;      // CALIB (pre is the "dirty" end)
    static constexpr float kPostBias = 0.15f;      // CALIB

    AsymSaturator preSat_, postSat_;

    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, separationOct_ = 0.0f;
    float  preDrive_ = 0.0f, postDrive_ = 0.0f;
    Mode   mode_  = Mode::LP;
    Slope  slope_ = Slope::db24;
};
