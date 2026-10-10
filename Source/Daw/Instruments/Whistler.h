#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

/** "Whistler": a person whistling. One voice that slides between the notes you play (play legato and it glides
    from one to the next; every new phrase scoops up into its first note), breath and air, a delayed vibrato,
    a wobbly human pitch, a slapback echo and a reverb. Pitch bend bends it; the mod wheel adds vibrato. */
class Whistler : public BuiltinProcessor
{
public:
    Whistler();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 5.0; }
    juce::StringArray getProgramNames() override { return presetNames(); }
    static juce::StringArray presetNames();
    void loadProgram (int) override;

    /** For tests: the pitch the whistle is sounding right now (Hz, 0 when silent). */
    float getCurrentHz() const { return currentHz.load(); }

private:
    void noteOn (int note, float velocity);
    void noteOff (int note);

    double sampleRate = 48000.0;
    std::vector<int> held;           // keys down, oldest first
    int targetNote = -1;
    float velocity = 0.8f;

    float pitch = 72.0f;             // the sung pitch now (MIDI note, fractional)
    float glideFrom = 72.0f, glideTo = 72.0f;
    int glideAge = 0, glideLen = 1;
    float scoopFrom = 0.0f;          // semitones below that the note starts from (decays to 0)
    int scoopAge = 0, scoopLen = 1;
    bool sounding = false;
    float env = 0.0f, fall = 0.0f;
    int noteAge = 0;
    int releasedFor = 1 << 30;      // samples since the last key came up

    double phase = 0.0;
    float vibPhase = 0.0f, drift = 0.0f, driftVel = 0.0f, bend = 0.0f, modWheel = 0.0f;
    float puff = 0.0f;
    struct Svf { float g = 0.1f, k = 0.2f, ic1 = 0, ic2 = 0; } breathBp;
    float airHp = 0.0f, toneLp = 0.0f;
    juce::Random rnd;

    std::vector<float> echo[2];
    int echoPos = 0;
    juce::Reverb reverb;
    std::atomic<float> currentHz { 0.0f };
};

} // namespace wis::daw
