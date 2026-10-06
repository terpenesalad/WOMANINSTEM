#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

/** An audio file in memory for the samplers, plus where its transients are. Immutable once made. */
struct SampleData
{
    juce::AudioBuffer<float> buffer;     // stereo
    double sampleRate = 48000.0;
    juce::String ref;                    // how the song refers to the file
    juce::String name;
    std::vector<std::pair<int, float>> onsets;   // sample position, strength 0..1 (strongest = 1)
    std::vector<float> peaks;            // overview for drawing (max |x| per 512 samples)

    static std::unique_ptr<SampleData> fromFile (const juce::File& f, double maxSeconds, juce::String& error);
    static std::unique_ptr<SampleData> fromBuffer (juce::AudioBuffer<float> b, double sr, const juce::String& name);
    void analyse();
    int length() const { return buffer.getNumSamples(); }
};

/** Holds the sample the audio thread plays, swapping safely when a new one is loaded. */
class SampleSlot
{
public:
    void set (std::unique_ptr<SampleData> d);
    SampleData* get() const { return current.load(); }   // audio thread: valid for the current block
private:
    std::atomic<SampleData*> current { nullptr };
    std::vector<std::unique_ptr<SampleData>> history;    // old ones live on briefly (the audio thread may still be reading)
};

/** "Sampler": play any audio file across the keyboard (with loop), as a one-shot, or chopped into slices. */
class Sampler : public BuiltinProcessor
{
public:
    enum Mode { classic, oneShot, slice };
    Sampler();
    ~Sampler() override;

    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 3.0; }
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (Sampler&)> editorFactory;

    /** Message thread. Returns an error message, or empty. */
    juce::String loadFile (const juce::File& f);
    void loadBuffer (juce::AudioBuffer<float> b, double sr, const juce::String& name);
    SampleData* getSample() const { return sample.get(); }

    /** The slice boundaries (sample positions) the current settings give. */
    std::vector<int> currentSlices() const;
    static constexpr int firstSliceNote = 36;

    std::atomic<float> playPosition { -1.0f };   // 0..1 of the most recent voice, for the editor

private:
    struct Voice;
    std::vector<int> slicesFor (const SampleData&) const;
    SampleSlot sample;
    std::unique_ptr<std::array<Voice, 32>> voices;
    double sr = 48000.0;
    int lastNote = -1;
    float glideFrom = 0.0f;
};

/** "Drum Pads": 16 pads (notes 36-51, the General MIDI drum notes) each with its own sample, tuning, decay,
    filter, pan and choke group. Starts with an analogue kit; drop your own samples on the pads. */
class DrumPads : public BuiltinProcessor
{
public:
    static constexpr int numPads = 16, firstNote = 36;
    DrumPads();
    ~DrumPads() override;

    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 3.0; }
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int index) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (DrumPads&)> editorFactory;

    juce::String loadPadFile (int pad, const juce::File& f);
    void loadSynthKit (int kit);
    juce::String padName (int pad) const;
    SampleData* padSample (int pad) const { return pads[(size_t) pad].get(); }
    void previewPad (int pad) { previewRequest = pad; }
    std::atomic<int> lastHit { -1 };
    static juce::String padParam (int pad, const char* name) { return "p" + juce::String (pad) + "_" + name; }

private:
    struct Voice;
    std::array<SampleSlot, numPads> pads;
    std::array<juce::String, numPads> padSources;   // "synth:<voice>:<kit>" or a file reference
    std::unique_ptr<std::array<Voice, 32>> voices;
    double sr = 48000.0;
    std::atomic<int> previewRequest { -1 };
    int synthKit = 0;
};

} // namespace wis::daw
