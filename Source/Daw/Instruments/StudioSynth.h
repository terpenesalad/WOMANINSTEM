#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

/** "Studio Synth": a polyphonic virtual-analogue synth - 2 band-limited oscillators + sub + noise,
    multimode resonant filter with its own envelope, amp envelope, LFO, glide and mono/legato mode. */
class StudioSynth : public BuiltinProcessor
{
public:
    StudioSynth();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    juce::StringArray getProgramNames() override;
    void loadProgram (int index) override;

    /** Snapshot of the parameters, taken once per block for the voices. */
    struct Params
    {
        int wave1 = 0, wave2 = 0, filterType = 0;
        float semi2 = 0, detune2 = 0, mix = 0.5f, sub = 0, noise = 0;
        float cutoff = 8000, reso = 0.2f, envAmt = 0, keyTrack = 0.5f, velToFilter = 0.3f;
        float fA = 0.01f, fD = 0.3f, fS = 0.5f, fR = 0.3f;
        float aA = 0.005f, aD = 0.2f, aS = 0.8f, aR = 0.3f;
        float lfoRate = 4, lfoPitch = 0, lfoFilter = 0, glide = 0, volume = 0;
        bool mono = false;
    };
    const Params& getParams() const { return snapshot; }
    double lastNoteFrequency = 0.0;

private:
    void updateSnapshot();
    juce::Synthesiser synth, monoSynth;   // 16-voice poly / 1-voice legato
    Params snapshot;
    int voiceCount = 16;
    bool lastMono = false;
};

} // namespace wis::daw
