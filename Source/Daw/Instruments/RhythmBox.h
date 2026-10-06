#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include "DrumSynth.h"

namespace wis::daw
{

/** "Rhythm Box": an analogue drum machine instrument (General MIDI drum map) with four vintage kits. */
class RhythmBox : public BuiltinProcessor
{
public:
    RhythmBox();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 2.0; }
    juce::StringArray getProgramNames() override { return DrumSynth::kitNames(); }
    void loadProgram (int index) override;

private:
    DrumSynth drums;
};

} // namespace wis::daw
