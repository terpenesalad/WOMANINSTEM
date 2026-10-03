#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <atomic>

namespace nam { class DSP; }

namespace wis
{

/** Hosts a Neural Amp Modeler (.nam) capture. Models are loaded on the calling (non-audio) thread
    and swapped in without blocking the audio thread. */
class NamAmp
{
public:
    NamAmp();
    ~NamAmp();

    /** Call when the audio device (re)starts; audio must not be running concurrently. */
    void prepare (double sampleRate, int maxBlock);

    /** Loads a .nam file (blocking, call from the message or a background thread).
        Returns an empty string on success, else an error. */
    juce::String load (const juce::File& file);
    void unload();

    /** Mono in-place processing. inputGainDb trims the level into the model. Returns false if no model. */
    bool process (float* data, int numSamples, float inputGainDb);

    bool isLoaded() const noexcept            { return loaded.load(); }
    juce::File getFile() const                { return file; }
    juce::String getModelName() const         { return file.getFileNameWithoutExtension(); }
    double getModelSampleRate() const noexcept{ return modelRate.load(); }
    bool sampleRateMismatch() const noexcept;

private:
    std::unique_ptr<nam::DSP> model;          // guarded by lock
    juce::SpinLock lock;
    std::atomic<bool> loaded { false };
    std::atomic<double> modelRate { 48000.0 };
    std::atomic<float> loudnessGain { 1.0f };
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    juce::File file;
    std::vector<float> scratchIn, scratchOut;
};

} // namespace wis
