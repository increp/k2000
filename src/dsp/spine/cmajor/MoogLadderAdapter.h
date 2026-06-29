#pragma once
#include <cstddef>

// In-place (no-heap) wrapper for the generated Moog processor. Holds the generated
// class by VALUE (the generated state is embeddable — proven by NlSvfDriveLeanAdapter).
// One adapter per mono lane; MoogLadder::VoiceState holds two. The generated header is
// included only in the .cpp; this header exposes a fixed-size aligned buffer so callers
// don't pull in the 30 KB generated header.
class MoogLadderAdapter {
public:
    MoogLadderAdapter() noexcept;
    ~MoogLadderAdapter();
    MoogLadderAdapter(const MoogLadderAdapter&) = delete;
    MoogLadderAdapter& operator=(const MoogLadderAdapter&) = delete;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    double getMaxFrequency() const noexcept;  // exposes codegen cap; used by tests to verify oversampling headroom
    void setCutoff(float hz) noexcept;
    void setParams(float cutoffHz, float resonance, float drive, int slope, int mode = 0) noexcept;
    // Task 6: sub-osc bass voice. setBass forwards amount/wave/octave; setFundamental
    // forwards the played-note Hz; noteReset fires the phase reset (note-on seam).
    void setBass(float amount, int wave, int octave) noexcept;
    void setFundamental(float hz) noexcept;
    void noteReset() noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    // Storage for the generated processor, placement-constructed in the .cpp.
    // kGenBytes/kGenAlign are validated against sizeof/alignof(Generated) by static_assert in the .cpp.
    static constexpr std::size_t kGenBytes = 512;    // measured sizeof(MoogLadder) = 336 (maxFramesPerBlock=32); 512 gives headroom
    static constexpr std::size_t kGenAlign = 16;
    alignas(kGenAlign) unsigned char storage_[kGenBytes];
};
