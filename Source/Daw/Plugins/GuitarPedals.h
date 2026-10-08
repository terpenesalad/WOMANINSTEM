#pragma once

#include "BuiltinProcessor.h"
#include "Rig/AmpModels.h"

namespace wis::daw
{

/** Stomp boxes for guitar and bass: on the Play Along pedalboard (and in the Studio's effect list). */

// ---- Drive: overdrive / distortion / fuzz / bass drive (the rig's drive circuits as a pedal you can add anywhere) ----
class DriveStomp : public BuiltinProcessor
{
public:
    DriveStomp();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
private:
    DrivePedal drive[2];
};

// ---- Boost: clean, treble booster or mid boost ------------------------------------------------------------------
class BoostStomp : public BuiltinProcessor
{
public:
    BoostStomp();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
private:
    double sampleRate = 48000.0;
    std::array<juce::dsp::IIR::Filter<float>, 2> shape, lowCut;
    int lastType = -1;
    float lastTone = -1.0f;
    juce::SmoothedValue<float> gain;
};

// ---- Wah: rocker pedal, envelope (auto-wah) or LFO -------------------------------------------------------------
class WahStomp : public BuiltinProcessor
{
public:
    WahStomp();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
private:
    struct Svf { float ic1 = 0, ic2 = 0; };
    std::array<Svf, 2> svf;
    double sampleRate = 48000.0, lfoPhase = 0.0;
    float env = 0.0f, position = 0.0f;
};

// ---- Octaver: one and two octaves down, one up -----------------------------------------------------------------
class OctaverStomp : public BuiltinProcessor
{
public:
    OctaverStomp();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
private:
    double sampleRate = 48000.0;
    juce::dsp::IIR::Filter<float> trackLp1, trackLp2, subLp1, subLp2, upHp, upLp;
    float lastTone = -1.0f;
    float env = 0.0f, hyst = 0.0f;
    bool armed = true, ff1 = false, ff2 = false;
};

// ---- Swell: fades every new note in, like a violin bow --------------------------------------------------------
class SwellStomp : public BuiltinProcessor
{
public:
    SwellStomp();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    double sampleRate = 48000.0;
    float env = 0.0f, gainNow = 0.0f;
    bool open = false;
};

} // namespace wis::daw
