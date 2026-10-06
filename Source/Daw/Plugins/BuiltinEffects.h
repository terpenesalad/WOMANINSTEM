#pragma once

#include "BuiltinProcessor.h"
#include "Rig/Effects.h"
#include "Rig/RigProcessor.h"

namespace wis::daw
{

using AC = juce::dsp::IIR::ArrayCoefficients<float>;

// ---- Channel EQ --------------------------------------------------------------------------------------
class ChannelEq : public BuiltinProcessor
{
public:
    ChannelEq();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;

    /** Total magnitude response in dB at a frequency (for the curve display).
        Call refreshDisplay() first (message thread) to pick up parameter changes. */
    float responseDb (double hz) const;
    void refreshDisplay() const;

private:
    void update();
    void designBands (double sr, std::array<std::array<float, 6>, 6>& c, std::array<bool, 6>& on) const;
    double sampleRate = 48000.0;
    static constexpr int numBands = 6;
    std::array<std::array<juce::dsp::IIR::Filter<float>, numBands>, 2> filters;
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, numBands> coeffs;
    std::array<bool, numBands> enabled {};
    mutable std::array<bool, numBands> displayEnabled {};
    std::array<float, 16> cache {};
    juce::SmoothedValue<float> outGain;
};

// ---- Compressor -----------------------------------------------------------------------------------------
class Compressor : public BuiltinProcessor
{
public:
    Compressor();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    bool wantsSidechain() const override { return true; }
    std::atomic<float> gainReductionDb { 0.0f };

private:
    double sampleRate = 48000.0;
    float envDb = 0.0f;
};

// ---- Reverb ------------------------------------------------------------------------------------------------
class ReverbFx : public BuiltinProcessor
{
public:
    ReverbFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    double getTailLengthSeconds() const override { return 6.0; }

private:
    juce::dsp::Reverb reverb;
    PreDelay preDelay;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowCut, highCut;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> wet;
};

// ---- Delay -------------------------------------------------------------------------------------------------
class DelayFx : public BuiltinProcessor
{
public:
    DelayFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    double getTailLengthSeconds() const override { return 6.0; }
    static juce::StringArray divisionNames();
    static double divisionBeats (int index);

private:
    StereoDelay delay;
    juce::AudioBuffer<float> dry;
};

// ---- Chorus / Phaser / Tremolo ----------------------------------------------------------------------------
class ChorusFx : public BuiltinProcessor
{
public:
    ChorusFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    juce::dsp::Chorus<float> chorus;
};

class PhaserFx : public BuiltinProcessor
{
public:
    PhaserFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    juce::dsp::Phaser<float> phaser;
};

class TremoloFx : public BuiltinProcessor
{
public:
    TremoloFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    double sampleRate = 48000.0, phase = 0.0;
};

// ---- Saturator ------------------------------------------------------------------------------------------------
class Saturator : public BuiltinProcessor
{
public:
    Saturator();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    std::array<juce::dsp::IIR::Filter<float>, 2> tone;
    float lastTone = -1.0f;
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> dry;
    std::array<float, 2> holdValue {};
    int holdCounter = 0;
};

// ---- Limiter / Gate -------------------------------------------------------------------------------------------
class LimiterFx : public BuiltinProcessor
{
public:
    LimiterFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    juce::dsp::Limiter<float> limiter;
};

class GateFx : public BuiltinProcessor
{
public:
    GateFx();
    bool wantsSidechain() const override { return true; }
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    double sampleRate = 48000.0;
    float env = 0.0f, gain = 0.0f;
    int holdCounter = 0;
    bool isOpen = false;
};

// ---- Amp & Pedals (the whole play-along rig as an insert) --------------------------------------------------
class AmpRigFx : public BuiltinProcessor
{
public:
    AmpRigFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::StringArray getProgramNames() override { return RigProcessor::factoryPresetNames(); }
    void loadProgram (int i) override { rig.loadFactoryPreset (i); }
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    double getTailLengthSeconds() const override { return 3.0; }

    RigProcessor rig;
    static std::function<juce::AudioProcessorEditor* (AmpRigFx&)> editorFactory;

private:
    std::vector<float> mono;
    int maxBlock = 512;
};

} // namespace wis::daw
