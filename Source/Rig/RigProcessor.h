#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpModels.h"
#include "CabSim.h"
#include "Effects.h"
#include "NamAmp.h"
#include "Tuner.h"
#include <unordered_map>

namespace wis
{

/** Parameter IDs (also used by the UI). */
namespace pid
{
    inline constexpr const char* inputGain   = "input_gain";
    inline constexpr const char* gateOn      = "gate_on";
    inline constexpr const char* gateThresh  = "gate_thresh";
    inline constexpr const char* gateRelease = "gate_release";
    inline constexpr const char* compOn      = "comp_on";
    inline constexpr const char* compThresh  = "comp_thresh";
    inline constexpr const char* compRatio   = "comp_ratio";
    inline constexpr const char* compAttack  = "comp_attack";
    inline constexpr const char* compRelease = "comp_release";
    inline constexpr const char* compLevel   = "comp_level";
    inline constexpr const char* driveOn     = "drive_on";
    inline constexpr const char* driveType   = "drive_type";
    inline constexpr const char* driveAmount = "drive_amount";
    inline constexpr const char* driveTone   = "drive_tone";
    inline constexpr const char* driveLevel  = "drive_level";
    inline constexpr const char* ampOn       = "amp_on";
    inline constexpr const char* ampModel    = "amp_model";
    inline constexpr const char* ampGain     = "amp_gain";
    inline constexpr const char* ampBass     = "amp_bass";
    inline constexpr const char* ampMid      = "amp_mid";
    inline constexpr const char* ampTreble   = "amp_treble";
    inline constexpr const char* ampPresence = "amp_presence";
    inline constexpr const char* ampMaster   = "amp_master";
    inline constexpr const char* cabOn       = "cab_on";
    inline constexpr const char* cabType     = "cab_type";
    inline constexpr const char* cabLowCut   = "cab_lowcut";
    inline constexpr const char* cabHighCut  = "cab_highcut";
    inline constexpr const char* eqOn        = "eq_on";
    inline constexpr const char* eqLow       = "eq_low";
    inline constexpr const char* eqLowMid    = "eq_lowmid";
    inline constexpr const char* eqHighMid   = "eq_highmid";
    inline constexpr const char* eqHigh      = "eq_high";
    inline constexpr const char* chorusOn    = "chorus_on";
    inline constexpr const char* chorusRate  = "chorus_rate";
    inline constexpr const char* chorusDepth = "chorus_depth";
    inline constexpr const char* chorusMix   = "chorus_mix";
    inline constexpr const char* delayOn     = "delay_on";
    inline constexpr const char* delayTime   = "delay_time";
    inline constexpr const char* delayFeedback = "delay_feedback";
    inline constexpr const char* delayTone   = "delay_tone";
    inline constexpr const char* delayMix    = "delay_mix";
    inline constexpr const char* delayPingPong = "delay_pingpong";
    inline constexpr const char* reverbOn    = "reverb_on";
    inline constexpr const char* reverbSize  = "reverb_size";
    inline constexpr const char* reverbDamp  = "reverb_damp";
    inline constexpr const char* reverbPreDelay = "reverb_predelay";
    inline constexpr const char* reverbMix   = "reverb_mix";
    inline constexpr const char* reverbWidth = "reverb_width";
    inline constexpr const char* outLevel    = "out_level";
}

/** The play-along rig: mono instrument/mic in -> stereo out.
    Gate -> Compressor -> Drive -> Amp (built-in or NAM) -> Cab (built-in or IR) -> EQ -> Chorus -> Delay -> Reverb.
    It's an AudioProcessor so we get a parameter tree with undo-free automation/smoothing, and presets for free. */
class RigProcessor : public juce::AudioProcessor
{
public:
    RigProcessor();
    ~RigProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "WOMANINSTEM Rig"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Processes a mono input block into stereo output. input may alias outL. */
    void processMonoToStereo (const float* input, float* outL, float* outR, int numSamples);

    juce::AudioProcessorValueTreeState& getState() { return apvts; }

    // ---- files ----
    juce::String loadNamModel (const juce::File& f);
    void unloadNamModel();
    NamAmp& getNam() { return nam; }

    bool loadImpulseResponse (const juce::File& f);
    juce::File getImpulseResponseFile() const { return cab.getImpulseResponseFile(); }

    // ---- presets ----
    static juce::StringArray factoryPresetNames();
    void loadFactoryPreset (int index);
    static juce::File userPresetDirectory();
    juce::String saveUserPreset (const juce::String& name);
    juce::String loadUserPreset (const juce::File& file);
    juce::ValueTree createPresetState();
    void restorePresetState (const juce::ValueTree& state);
    juce::String getPresetName() const { return presetName; }

    // ---- metering / tuner ----
    float getInputPeak() noexcept   { return inputPeak.exchange (0.0f); }
    float getOutputPeak() noexcept  { return outputPeak.exchange (0.0f); }
    bool isClipping() noexcept      { return inputClip.exchange (false); }
    Tuner& getTuner() { return tuner; }
    std::atomic<bool> tunerMute { false };

    void setParam (const juce::String& id, float value);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float p (const char* id) const { return params.at (id)->load(); }   // keyed by pointer: no allocation

    juce::AudioProcessorValueTreeState apvts;
    std::unordered_map<const char*, std::atomic<float>*> params;

    double sr = 48000.0;
    int maxBlock = 512;
    juce::String presetName;

    NoiseGate gate;
    juce::dsp::Compressor<float> compressor;
    DrivePedal drive;
    BuiltInAmp amp;
    NamAmp nam;
    CabSim cab;
    std::array<juce::dsp::IIR::Filter<float>, 4> eq;
    std::array<float, 4> eqCache { 99, 99, 99, 99 };
    juce::dsp::Chorus<float> chorus;
    StereoDelay delay;
    PreDelay preDelay;
    juce::dsp::Reverb reverb;
    Tuner tuner;

    juce::SmoothedValue<float> inGain, compMakeup, outGain, reverbMixSm;
    std::vector<float> mono, wetL, wetR;

    std::atomic<float> inputPeak { 0 }, outputPeak { 0 };
    std::atomic<bool> inputClip { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigProcessor)
};

} // namespace wis
