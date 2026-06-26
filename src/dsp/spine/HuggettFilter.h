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
    enum class Routing {
        LP = 0, BP = 1, HP = 2, Notch = 3,            // single modes (slope decides 12/24)
        SeriesLPHP = 4, SeriesLPBP = 5, SeriesHPBP = 6,
        ParLPHP    = 7, ParLPBP    = 8, ParHPBP    = 9,
        ParLPLP    = 10, ParBPBP   = 11, ParHPHP   = 12
    };

    struct VoiceState : public FilterModel::State {
        NlSvfCell a, b;
        DcBlocker dc;
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setMode(Mode m) noexcept {
        switch (m) { case Mode::BP: routing_ = Routing::BP; break;
                     case Mode::HP: routing_ = Routing::HP; break;
                     case Mode::LP: default: routing_ = Routing::LP; break; }
    }
    void setSlope(Slope s) noexcept     { slope_ = s; }
    void setRouting(Routing r) noexcept { routing_ = r; }
    void setSeparation(float oct) noexcept { separationOct_ = oct; }
    void setPostDrive(float drive01) noexcept { postDrive_ = drive01; postSat_.setDrive(drive01, kPostBias, kPostDriveDb); }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    struct Resolved { int tapA; int tapB; bool series; bool single; float parGain; };
    Resolved resolve() const noexcept;   // maps (routing_, slope_, separationOct_) -> triple

    static constexpr float kPreDriveDb  = 30.0f;   // CALIB
    static constexpr float kPostDriveDb = 24.0f;   // CALIB
    static constexpr float kPreBias  = 0.25f;      // CALIB (pre is the "dirty" end)
    static constexpr float kPostBias = 0.15f;      // CALIB

    AsymSaturator preSat_, postSat_;

    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, separationOct_ = 0.0f;
    float  preDrive_ = 0.0f, postDrive_ = 0.0f;
    Routing routing_ = Routing::LP;
    Slope   slope_   = Slope::db24;
};
