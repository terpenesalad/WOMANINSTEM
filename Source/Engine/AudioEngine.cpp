#include "AudioEngine.h"

namespace wis
{

AudioEngine::AudioEngine (RigProcessor& r, StemPlayer& p, Recorder& rec)
    : rig (r), player (p), recorder (rec)
{
    inputIndexForChannel.fill (-1);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    const int bs = device->getCurrentBufferSizeSamples();

    sampleRate = sr;
    blockSize = bs;
    latencyMs = 1000.0 * (double) (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + bs) / sr;

    const int maxBlock = juce::jmax (bs, 512) * 2;   // headroom: some drivers vary the block size
    inMono.assign ((size_t) maxBlock, 0.0f);
    rigL.assign ((size_t) maxBlock, 0.0f);
    rigR.assign ((size_t) maxBlock, 0.0f);
    mixL.assign ((size_t) maxBlock, 0.0f);
    mixR.assign ((size_t) maxBlock, 0.0f);

    rig.setRateAndBufferSizeDetails (sr, maxBlock);
    rig.prepareToPlay (sr, maxBlock);
    player.prepare (sr, maxBlock);

    rigGain.reset (sr, 0.03);
    masterGain.reset (sr, 0.03);

    // Work out which callback input index each device channel ends up at.
    inputIndexForChannel.fill (-1);
    auto active = device->getActiveInputChannels();
    auto names = device->getInputChannelNames();
    juce::StringArray activeNames;
    juce::Array<int> activeChannels;
    int idx = 0;
    for (int ch = 0; ch < juce::jmin (128, active.getHighestBit() + 1); ++ch)
    {
        if (active[ch])
        {
            inputIndexForChannel[(size_t) ch] = idx++;
            activeChannels.add (ch);
            activeNames.add (names[ch].isNotEmpty() ? names[ch] : "Input " + juce::String (ch + 1));
        }
    }
    {
        const juce::ScopedLock sl (namesLock);
        activeInputNames = activeNames;
        activeInputChannels = activeChannels;
    }
    firstInput  = activeChannels.size() > 0 ? activeChannels[0] : -1;
    secondInput = activeChannels.size() > 1 ? activeChannels[1] : -1;

    if (onSampleRateChanged)
        onSampleRateChanged (sr);
}

void AudioEngine::audioDeviceStopped()
{
    recorder.stop();
}

juce::StringArray AudioEngine::getActiveInputNames() const
{
    const juce::ScopedLock sl (namesLock);
    return activeInputNames;
}

juce::Array<int> AudioEngine::getActiveInputChannels() const
{
    const juce::ScopedLock sl (namesLock);
    return activeInputChannels;
}

/** Transparent below -1 dBFS, then a smooth knee that can never exceed 0 dBFS. Protects ears and speakers
    from feedback squeals or a cranked fuzz. */
static inline float safetyLimit (float x, bool& hit) noexcept
{
    const float a = std::abs (x);
    constexpr float knee = 0.891f;   // -1 dBFS
    if (a <= knee) return x;
    hit = true;
    const float over = (a - knee) / (1.0f - knee);
    const float y = knee + (1.0f - knee) * std::tanh (over);
    return x < 0.0f ? -y : y;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs, int n,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    if (n > (int) inMono.size())
    {
        // Shouldn't happen (we allocate headroom), but never write past our buffers.
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], n);
        return;
    }

    // ---- input ----
    auto* in = inMono.data();
    std::fill (in, in + n, 0.0f);

    const int sel = selectedInput.load();
    auto readChannel = [&] (int deviceChannel, float gain)
    {
        if (deviceChannel < 0 || deviceChannel >= 128) return;
        const int idx = inputIndexForChannel[(size_t) deviceChannel];
        if (idx >= 0 && idx < numInputs && inputs[idx] != nullptr)
            juce::FloatVectorOperations::addWithMultiply (in, inputs[idx], gain, n);
    };

    if (sel == stereoSum)
    {
        const int a = firstInput.load(), b = secondInput.load();
        if (a >= 0 && b >= 0) { readChannel (a, 0.5f); readChannel (b, 0.5f); }
        else if (a >= 0)      readChannel (a, 1.0f);
    }
    else if (sel != noInput)
    {
        readChannel (sel, 1.0f);
    }

    // ---- rig ----
    rig.processMonoToStereo (in, rigL.data(), rigR.data(), n);

    rigGain.setTargetValue (monitorOn.load() ? juce::Decibels::decibelsToGain (rigVolumeDb.load(), -60.0f) : 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float g = rigGain.getNextValue();
        rigL[(size_t) i] *= g;
        rigR[(size_t) i] *= g;
    }

    // ---- song ----
    std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + n, 0.0f);
    player.process (mixL.data(), mixR.data(), n);

    // ---- sum, master, limiter ----
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolumeDb.load(), -60.0f));
    bool hit = false;
    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        float l = (mixL[(size_t) i] + rigL[(size_t) i]) * g;
        float r = (mixR[(size_t) i] + rigR[(size_t) i]) * g;
        l = safetyLimit (l, hit);
        r = safetyLimit (r, hit);
        mixL[(size_t) i] = l;
        mixR[(size_t) i] = r;
        pkL = juce::jmax (pkL, std::abs (l));
        pkR = juce::jmax (pkR, std::abs (r));
    }

    if (hit) limiterHit = true;
    if (pkL > outPeakL.load()) outPeakL = pkL;
    if (pkR > outPeakR.load()) outPeakR = pkR;

    recorder.write (mixL.data(), mixR.data(), rigL.data(), rigR.data(), in, n);

    // ---- to device ----
    for (int ch = 0; ch < numOutputs; ++ch)
    {
        if (outputs[ch] == nullptr) continue;
        if (ch == 0)      std::copy (mixL.begin(), mixL.begin() + n, outputs[ch]);
        else if (ch == 1) std::copy (mixR.begin(), mixR.begin() + n, outputs[ch]);
        else              juce::FloatVectorOperations::clear (outputs[ch], n);
    }

    // mono output device: fold down
    if (numOutputs == 1 && outputs[0] != nullptr)
        for (int i = 0; i < n; ++i)
            outputs[0][i] = 0.5f * (mixL[(size_t) i] + mixR[(size_t) i]);
}

} // namespace wis
