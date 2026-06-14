#include "VoiceManager.h"

void VoiceManager::setLayer(Layer* layer) {
    layer_ = layer;
    for (auto& v : voices_) v.setLayer(layer);
}

void VoiceManager::prepare(double sr, int maxBlock) {
    for (auto& v : voices_) {
        v.setLayer(layer_);  // ensure binding before per-slot state is sized
        v.prepare(sr, maxBlock);
    }
    voiceAge_.fill(0);
    ageCounter_ = 0;
}

void VoiceManager::allNotesOff() {
    for (auto& v : voices_) v.noteOff();
}

int VoiceManager::pickVoiceFor(int) {
    // Inactive voice first
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices_[i].isActive()) return i;
    // Otherwise steal the oldest (smallest age)
    int oldest = 0;
    for (int i = 1; i < kNumVoices; ++i)
        if (voiceAge_[i] < voiceAge_[oldest]) oldest = i;
    return oldest;
}

void VoiceManager::noteOn(int midiNote, float velocity) {
    int v = pickVoiceFor(midiNote);
    voices_[v].noteOn(midiNote, velocity);
    voiceAge_[v] = ++ageCounter_;
}

void VoiceManager::noteOff(int midiNote) {
    for (int i = 0; i < kNumVoices; ++i)
        if (voices_[i].isActive() && voices_[i].currentNote() == midiNote)
            voices_[i].noteOff();
}

void VoiceManager::renderBlock(float* out, int numSamples,
                               const juce::MidiBuffer& midi) {
    int cursor = 0;
    auto renderRange = [&](int from, int to) {
        if (to <= from) return;
        const int len = to - from;
        for (auto& v : voices_) v.render(out + from, len);
    };

    for (const auto meta : midi) {
        const int pos = meta.samplePosition;
        renderRange(cursor, pos);
        cursor = pos;

        const auto& m = meta.getMessage();
        if (m.isNoteOn())  noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff()) allNotesOff();
    }
    renderRange(cursor, numSamples);
}
