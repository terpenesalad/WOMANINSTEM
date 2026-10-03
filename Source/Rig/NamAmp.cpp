#include "NamAmp.h"
#include <juce_audio_basics/juce_audio_basics.h>

#if defined(_MSC_VER)
 #pragma warning (push, 0)
#endif
#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#if defined(_MSC_VER)
 #pragma warning (pop)
#endif

namespace wis
{

NamAmp::NamAmp() = default;
NamAmp::~NamAmp() = default;

void NamAmp::prepare (double sr, int maxBlock)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = sr;
    maxBlockSize = maxBlock;
    scratchIn.assign ((size_t) maxBlock, 0.0f);
    scratchOut.assign ((size_t) maxBlock, 0.0f);

    if (model != nullptr)
        model->Reset (sampleRate, maxBlockSize);
}

bool NamAmp::sampleRateMismatch() const noexcept
{
    const auto r = modelRate.load();
    return loaded.load() && r > 0.0 && std::abs (r - sampleRate) > 1.0;
}

juce::String NamAmp::load (const juce::File& f)
{
    if (! f.existsAsFile())
        return "File not found: " + f.getFullPathName();

    std::unique_ptr<nam::DSP> newModel;

    try
    {
        newModel = nam::get_dsp (std::filesystem::path (f.getFullPathName().toWideCharPointer()));
    }
    catch (const std::exception& e)
    {
        return "Couldn't load \"" + f.getFileName() + "\": " + juce::String (e.what());
    }

    if (newModel == nullptr)
        return "Couldn't load \"" + f.getFileName() + "\" (unsupported model).";

    if (newModel->NumInputChannels() != 1 || newModel->NumOutputChannels() != 1)
        return "\"" + f.getFileName() + "\" isn't a mono amp capture. (Cabinet IR .wav files go in the Cab section.)";

    double sr;
    int block;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sr = sampleRate;
        block = maxBlockSize;
    }

    // Prepare (and prewarm) off the audio thread - this can take a little while for big models.
    newModel->Reset (sr, block);

    float gain = 1.0f;
    if (newModel->HasLoudness())
        gain = juce::Decibels::decibelsToGain ((float) (-18.0 - newModel->GetLoudness()));   // normalise to -18 dB
    const double expected = newModel->GetExpectedSampleRate();

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (model, newModel);
        loudnessGain = juce::jlimit (0.01f, 20.0f, gain);
        modelRate = expected > 0 ? expected : 48000.0;
        file = f;
        loaded = true;
    }
    // old model (now in newModel) is destroyed here, outside the lock

    return {};
}

void NamAmp::unload()
{
    std::unique_ptr<nam::DSP> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (old, model);
        loaded = false;
        file = juce::File();
    }
}

bool NamAmp::process (float* data, int numSamples, float inputGainDb)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || model == nullptr || numSamples > maxBlockSize)
        return false;

    const float inGain = juce::Decibels::decibelsToGain (inputGainDb);
    for (int i = 0; i < numSamples; ++i)
        scratchIn[(size_t) i] = data[i] * inGain;

    float* in[]  = { scratchIn.data() };
    float* out[] = { scratchOut.data() };
    model->process (in, out, numSamples);

    const float g = loudnessGain.load();
    for (int i = 0; i < numSamples; ++i)
    {
        const float y = scratchOut[(size_t) i] * g;
        data[i] = std::isfinite (y) ? y : 0.0f;
    }
    return true;
}

} // namespace wis
