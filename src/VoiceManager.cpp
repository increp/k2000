#include "VoiceManager.h"
#include "Program.h"

void VoiceManager::setProgram(Program* program) {
    program_ = program;
    // Voices can play any layer; bind each to slot 0's layer initially so
    // prepare() can size per-block VoiceState against the (shared) palette.
    if (program_)
        for (auto& v : voices_) v.setLayer(&program_->slot(0).layer);
}

void VoiceManager::prepare(double sr, int maxBlock) {
    if (program_)
        for (auto& v : voices_) { v.setLayer(&program_->slot(0).layer); v.prepare(sr, maxBlock); }
    voiceAge_.fill(0);
    voiceSlot_.fill(-1);
    ageCounter_ = 0;
}

void VoiceManager::allNotesOff() {
    for (auto& v : voices_) v.noteOff();
}

int VoiceManager::pickVoice() {
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices_[i].isActive()) return i;
    int oldest = 0;
    for (int i = 1; i < kNumVoices; ++i)
        if (voiceAge_[i] < voiceAge_[oldest]) oldest = i;
    return oldest;
}

void VoiceManager::noteOn(int note, float velocity, int channel) {
    if (!program_) return;
    const int vel = (int) (velocity * 127.0f + 0.5f);
    for (std::size_t s = 0; s < program_->numLayers(); ++s) {
        auto& slot = program_->slot(s);
        if (!slot.routing.matches(note, vel, channel)) continue;
        const int v = pickVoice();
        voices_[v].setLayer(&slot.layer);
        voices_[v].noteOn(note, velocity);
        voiceSlot_[v] = (int) s;
        voiceAge_[v] = ++ageCounter_;
    }
}

void VoiceManager::noteOff(int note, int channel) {
    if (!program_) return;
    for (int i = 0; i < kNumVoices; ++i) {
        if (!voices_[i].isActive() || voices_[i].currentNote() != note) continue;
        const int s = voiceSlot_[i];
        if (s < 0) continue;
        const int ch = program_->slot((std::size_t) s).routing.channel;
        if (ch == 0 || ch == channel) voices_[i].noteOff();
    }
}

void VoiceManager::renderBlock(float* outL, float* outR, int numSamples, const juce::MidiBuffer& midi) {
    int cursor = 0;
    auto renderRange = [&](int from, int to) {
        if (to <= from) return;
        const int len = to - from;
        for (auto& v : voices_) v.render(outL + from, outR + from, len);
    };

    for (const auto meta : midi) {
        const int pos = meta.samplePosition;
        renderRange(cursor, pos);
        cursor = pos;
        const auto& m = meta.getMessage();
        if (m.isNoteOn())       noteOn(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel());
        else if (m.isNoteOff()) noteOff(m.getNoteNumber(), m.getChannel());
        else if (m.isAllNotesOff()) allNotesOff();
    }
    renderRange(cursor, numSamples);
}
