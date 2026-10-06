#pragma once

#include "BuiltinProcessor.h"

namespace wis::daw
{

/** "Vocal Tune": automatic pitch correction (the "Auto-Tune" effect). Detects the pitch of a single voice
    and moves it to the nearest note of a key and scale, from gentle and natural (slow retune) to the
    hard, robotic sound (retune speed 0). Pitch shifting is pitch-synchronous (PSOLA), so the voice's
    formants stay natural. Latency is reported to the Studio, which compensates for it on playback. */
class VocalTune : public BuiltinProcessor
{
public:
    VocalTune();
    ~VocalTune() override;
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.1; }
    juce::StringArray getProgramNames() override;
    void loadProgram (int index) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (VocalTune&)> editorFactory;

    // for the editor's pitch display
    std::atomic<float> detectedNote { -1.0f };   // MIDI note (fractional), -1 = no pitch
    std::atomic<float> targetNote { -1.0f };
    std::atomic<float> correction { 0.0f };      // semitones applied

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int currentRange = -1;
    double sampleRate = 48000.0;
};

} // namespace wis::daw
