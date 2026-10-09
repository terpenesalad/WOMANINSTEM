#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include "DrumSynth.h"
#include "VintageRhythms.h"

namespace wis::daw
{

/** "HomeKeys 20": a 1980s-style portable home keyboard. Eighteen preset voices (including two early-80s digital
    organs), amp drive, a vibrato "wobble" and a big reverb, preset rhythms on an analogue
    rhythm section, Auto accompaniment (single-finger or fingered chords in the left hand), ensemble, vibrato,
    sustain and a "Vintage" knob for the worn-cassette sound. Rhythm follows the song when it's playing,
    or runs on its own (START / SYNC START) when it isn't. */
class HomeKeys : public BuiltinProcessor
{
public:
    HomeKeys();
    ~HomeKeys() override;

    static juce::StringArray toneNames();
    static juce::StringArray rhythmNames();
    /** Rhythm Sound choices: the shared drum kits plus "Portable '81" (choice index DrumSynth::numKits). */
    static juce::StringArray kitNames();
    /** The selected rhythm as shown on the display ("Rock II" in the Portable '81 bank). */
    juce::String currentRhythmName() const;

    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 3.0; }
    juce::StringArray getProgramNames() override;
    static juce::StringArray presetNames();
    void loadProgram (int index) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (HomeKeys&)> editorFactory;

    // panel buttons (message thread)
    void startStop();
    void requestFill()              { fillRequested = true; }
    bool isRhythmRunning() const    { return internalRunning.load() || hostPlaying.load(); }

    // for the panel's displays
    std::atomic<int> currentStep { -1 }, currentBar { 0 };
    std::atomic<int> chordRoot { -1 };      // pitch class, -1 = none
    std::atomic<int> chordType { 0 };       // 0 major 1 minor 2 seventh 3 minor seventh
    static juce::String chordName (int root, int type);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<bool> internalRunning { false }, hostPlaying { false }, fillRequested { false }, startRequested { false }, stopRequested { false };
};

} // namespace wis::daw
