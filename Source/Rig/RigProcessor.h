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
    // 3.1
    inline constexpr const char* charType    = "char_type";
    inline constexpr const char* charAmount  = "char_amount";
    inline constexpr const char* cabMic      = "cab_mic";
    inline constexpr const char* cabMicPos   = "cab_micpos";
    inline constexpr const char* cabRoom     = "cab_room";
    inline constexpr const char* cabDiBlend  = "cab_di";
    inline constexpr const char* tapeOn      = "tape_on";
    inline constexpr const char* tapeDrive   = "tape_drive";
}

/** The reorderable blocks of the rig's pedalboard (the rig's own effects; added pedals sit between them). */
enum class RigBlock : int { gate = 0, comp, drive, ampCab, eq, tape, chorus, delay, reverb, count };
const char* rigBlockKey (RigBlock);              // stable key saved in presets ("gate", "ampcab" ...)
juce::String rigBlockName (RigBlock);            // shown on the pedalboard
const char* rigBlockPowerParam (RigBlock);       // its on / off parameter (ampCab: the amp's)

/** A pedal added to the board: one of the built-in effects, with its own knobs. */
struct RigPedal : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<RigPedal>;
    juce::String uid, builtinId, name;
    std::unique_ptr<juce::AudioProcessor> proc;
    std::atomic<bool> bypass { false };
};

/** The order of the board: an immutable list, swapped into the audio thread when it changes. */
struct RigChain : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<RigChain>;
    struct Entry { int block = -1; RigPedal::Ptr pedal; };   // block >= 0: one of RigBlock, else a pedal
    std::vector<Entry> entries;
};

/** The play-along rig: mono instrument/mic in -> stereo out.
    Strings & Pickups -> Gate -> Compressor -> Drive -> Amp (built-in or NAM) -> Cab (built-in or IR, + DI blend) -> EQ
    -> Console & Tape -> Chorus -> Delay -> Reverb.
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

    // ---- pedalboard (message thread) ----
    /** One block on the board, in signal order. key = a RigBlock key, or the pedal's uid. */
    struct BoardItem { juce::String key, name, builtinId; bool isPedal = false; bool on = false; int block = -1; };
    std::vector<BoardItem> getBoard() const;
    /** Moves the item at index `from` so it ends up at index `to` (indices into getBoard()). */
    void moveBoardItem (int from, int to);
    /** Adds one of the built-in effects as a pedal (at `insertAt`, or just before the amp when -1). Returns its uid, or empty. */
    juce::String addPedal (const juce::String& builtinId, int insertAt = -1, const juce::String& stateBase64 = {});
    void removePedal (const juce::String& uid);
    juce::AudioProcessor* getPedalProcessor (const juce::String& uid) const;
    void setBoardItemOn (const juce::String& key, bool on);
    bool isBoardItemOn (const juce::String& key) const;
    /** Back to the standard order with no added pedals. */
    void resetBoard();
    /** Built-in effect ids that can be added as pedals. */
    static juce::StringArray pedalIds();
    static constexpr int maxPedals = 16;
    /** Sends a change message whenever the board's layout changes (add / remove / reorder / preset). */
    juce::ChangeBroadcaster boardChanged;

    /** Told synchronously, on the message thread, just before a pedal leaves the board (close its editor now). */
    struct BoardListener
    {
        virtual ~BoardListener() = default;
        virtual void pedalRemoved (const juce::String& uid) = 0;
    };
    void addBoardListener (BoardListener* l)    { boardListeners.add (l); }
    void removeBoardListener (BoardListener* l) { boardListeners.remove (l); }

private:
    void publishChain (std::vector<RigChain::Entry> entries);
    std::vector<RigChain::Entry> copyEntries() const;
    void preparePedal (juce::AudioProcessor& p);
    juce::ValueTree saveBoard() const;
    void loadBoard (const juce::ValueTree& board);
    /** Gate / compressor / EQ / tape on one (mono) or two (stereo) channels. */
    void processCoreBlock (RigBlock b, float* const* chans, int numChannels, int n);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float p (const char* id) const { return params.at (id)->load(); }   // keyed by pointer: no allocation

    juce::AudioProcessorValueTreeState apvts;
    std::unordered_map<const char*, std::atomic<float>*> params;

    double sr = 48000.0;
    int maxBlock = 512;
    juce::String presetName;

    mutable juce::SpinLock chainLock;
    RigChain::Ptr chain, audioChain;               // chain: latest (message thread); audioChain: what the audio thread runs
    juce::ReferenceCountedArray<RigChain> chainGraveyard;
    juce::MidiBuffer pedalMidi;
    juce::ListenerList<BoardListener> boardListeners;
    int pedalCounter = 0;

    InstrumentCharacter character;
    ConsoleTape tape, tapeR;
    std::vector<float> diCopy, diLine;
    int diWrite = 0;
    NoiseGate gate, gateR;
    juce::dsp::Compressor<float> compressor;
    DrivePedal drive;
    BuiltInAmp amp;
    NamAmp nam;
    CabSim cab;
    std::array<juce::dsp::IIR::Filter<float>, 4> eq, eqR;
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
