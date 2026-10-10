#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

// ---------------------------------------------------------------------------------------------------------
//  Junkyard Percussion: everything here is synthesized (modal resonators, plucked and bowed strings, filtered
//  noise), no samples. Two ways to play it:
//    * the Junk Kit: a different object on every key - body percussion (finger snaps, claps, slaps), wood
//      (crates, chairs, planks, log drums, bones), strings (col legno, pizzicato, a slap on the double bass's
//      body), metal (brake drums, pipes, an anvil, a pot lid, oil drums, a jail-door clang, chains, a spring)
//      and sound-space textures (bowed saw, bowed metal, bowed basses, wind, rumble, rain on tin, creaks,
//      cave drips, mains hum, thunder sheet, waterphone),
//    * or one source played chromatically across the keyboard (marimba, brake drum, pipes, bowed bass...).
//  All of it sits in a designed acoustic space (a concrete storeroom, a tin shed, a cave, a forest...).
// ---------------------------------------------------------------------------------------------------------
class Junkyard : public BuiltinProcessor, private juce::Timer
{
public:
    Junkyard();
    ~Junkyard() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 8.0; }
    juce::StringArray getProgramNames() override { return presetNames(); }
    static juce::StringArray presetNames();
    static juce::String presetDescription (int program);
    void loadProgram (int) override;

    /** "Junk Kit" followed by the chromatic sources. */
    static juce::StringArray sourceNames();
    /** The kit's key map: what each key plays (empty when the key is unused). Notes 36..84. */
    static juce::String kitKeyName (int note);
    static constexpr int kitLow = 36, kitHigh = 84;

    /** Kit keys, for the loops and sound spaces in the browser. */
    enum KitKey
    {
        kneeSlap = 36, snapTight, snapFat, handClap, snapBright, thighSlap, tongueClick, chestThump, snapSoft, groupClap, fingerClick, footStomp,
        crate = 48, chairKnock, plank, logLow, logHigh, woodBlock, bones, doorKnock, woodBox, colLegno, bassBodySlap, bassPizz,
        brakeDrum = 60, brakeMuted, pipe, anvil, bellPlate, tinCan, potLid, oilDrum, jailDoor, chains, springBoing, glassBottle,
        bowedSaw = 72, bowedMetal, bowedBass, bowedCello, wind, rumble, rain, creak, drips, hum, thunderSheet, waterphone, deepGong
    };

    /** Tests: block until the space's impulse response is loaded. */
    void waitUntilReady();
    int getActiveVoices() const { return activeVoices.load(); }

    static constexpr int maxVoices = 40;
    struct Voice;   // (public so the one-off loudness calibration can render one)

private:
    void timerCallback() override;
    void rebuildRoomIfNeeded (bool force);
    void startNote (int note, float velocity);
    void stopNote (int note);
    void allNotesOff();

    std::vector<std::unique_ptr<Voice>> voices;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    juce::uint32 noteCounter = 0;
    juce::Random random;
    std::atomic<int> activeVoices { 0 };

    float tiltLp[2] {};
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> wet;
    std::atomic<bool> roomReady { false };
    int roomKey[2] { -1, -1 };
};

} // namespace wis::daw
