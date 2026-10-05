#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"

struct tsf;

namespace wis::daw
{

/** Loads .sf2 files once and hands out lightweight copies (samples are shared). Message thread only. */
class SoundFontCache
{
public:
    struct PresetInfo { int bank = 0, program = 0; juce::String name; };

    static SoundFontCache& get();

    /** The SoundFont shipped with the app (GeneralUser GS), or an empty file if it isn't installed. */
    static juce::File defaultSoundFont();

    /** Returns a new tsf instance sharing the cached samples (caller owns it - close with tsf_close), or null. */
    tsf* createInstance (const juce::File& sf2, juce::String& error);
    juce::Array<PresetInfo> presetsFor (const juce::File& sf2);

    /** Starts loading the default SoundFont on a background thread so the first instrument opens instantly. */
    void preloadDefault();

private:
    tsf* master (const juce::File& f, juce::String& error);
    std::mutex lock;
    std::map<juce::String, tsf*> loaded;
};

/** "Sound Library": a General MIDI sample player built on TinySoundFont. Ships with GeneralUser GS
    (pianos, keys, organs, guitars, basses, strings, brass, winds, synths, drum kits...) and can load any .sf2. */
class SoundFontInstrument : public BuiltinProcessor
{
public:
    SoundFontInstrument();
    ~SoundFontInstrument() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 3.0; }

    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (SoundFontInstrument&)> editorFactory;

    /** Message thread. */
    juce::String loadSoundFont (const juce::File& sf2);   // empty file = default
    void setPreset (int bank, int program);
    int getBank() const     { return bank.load(); }
    int getProgram() const  { return program.load(); }
    juce::String getPresetName() const;
    juce::File getSoundFontFile() const { return sfFile; }
    bool isLoaded() const   { return synth != nullptr; }
    juce::Array<SoundFontCache::PresetInfo> getPresets() const { return presets; }

private:
    void applyPreset (tsf* f);   // audio thread / under lock

    tsf* synth = nullptr;
    juce::SpinLock synthLock;
    juce::File sfFile;
    juce::Array<SoundFontCache::PresetInfo> presets;
    std::atomic<int> bank { 0 }, program { 0 };
    std::atomic<bool> presetDirty { true };
    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::vector<float> interleaved;
};

} // namespace wis::daw
