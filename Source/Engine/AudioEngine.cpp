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
    youL.assign ((size_t) maxBlock, 0.0f);
    youR.assign ((size_t) maxBlock, 0.0f);
    {
        const juce::ScopedLock sl (keysLock);
        keysBuf.setSize (2, maxBlock);
        keysMidi.ensureSize (2048);
        if (keys != nullptr)
        {
            keys->setPlayConfigDetails (0, 2, sr, maxBlock);
            keys->prepareToPlay (sr, maxBlock);
        }
    }
    midiCollector.reset (sr);
    keysGain.reset (sr, 0.03);

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

std::unique_ptr<juce::AudioProcessor> AudioEngine::setKeysInstrument (std::unique_ptr<juce::AudioProcessor> p)
{
    const double sr = sampleRate.load();
    if (p != nullptr && sr > 0.0)
    {
        p->setPlayConfigDetails (0, 2, sr, keysBuf.getNumSamples());
        p->prepareToPlay (sr, juce::jmax (16, keysBuf.getNumSamples()));
    }
    const juce::ScopedLock sl (keysLock);   // waits for the audio thread to finish with the old one
    std::swap (keys, p);
    return p;
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

    // ---- keys: MIDI keyboards, the on-screen keyboard and the computer keys play the keys instrument ----
    keysMidi.clear();
    midiCollector.removeNextBlockOfMessages (keysMidi, n);   // always drained, so nothing piles up
    keyboardState.processNextMidiBuffer (keysMidi, 0, n, true);
    bool keysPlayed = false;
    {
        const juce::ScopedTryLock sl (keysLock);
        if (sl.isLocked() && keys != nullptr && n <= keysBuf.getNumSamples())
        {
            for (const auto meta : keysMidi)
                if (meta.getMessage().isNoteOn()) { midiNoteSeen = true; break; }
            juce::AudioBuffer<float> view (keysBuf.getArrayOfWritePointers(), 2, n);
            view.clear();
            keys->processBlock (view, keysMidi);
            keysPlayed = true;
        }
        else
        {
            for (const auto meta : keysMidi)
                if (meta.getMessage().isNoteOn()) { midiNoteSeen = true; break; }
        }
    }
    if (keysPlayed)
    {
        keysGain.setTargetValue (juce::Decibels::decibelsToGain (keysVolumeDb.load(), -60.0f));
        float pk = 0.0f;
        auto* kl = keysBuf.getWritePointer (0);
        auto* kr = keysBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = keysGain.getNextValue();
            kl[i] *= g; kr[i] *= g;
            pk = juce::jmax (pk, std::abs (kl[i]), std::abs (kr[i]));
        }
        if (pk > keysPeak.load()) keysPeak = pk;
    }

    auto* scopeFeed = scope.load();
    const double sr = sampleRate.load();
    // "you" for the scope = your instrument through the rig plus the keys
    const float* youLp = rigL.data();
    const float* youRp = rigR.data();
    if (scopeFeed != nullptr && keysPlayed && (scopeFeed->wants (ScopeFeed::instrument) || scopeFeed->wants (ScopeFeed::duet)))
    {
        for (int i = 0; i < n; ++i)
        {
            youL[(size_t) i] = rigL[(size_t) i] + keysBuf.getSample (0, i);
            youR[(size_t) i] = rigR[(size_t) i] + keysBuf.getSample (1, i);
        }
        youLp = youL.data();
        youRp = youR.data();
    }
    if (scopeFeed != nullptr && scopeFeed->wants (ScopeFeed::instrument))
        scopeFeed->push (youLp, youRp, n, sr);

    // ---- song ----
    std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + n, 0.0f);
    player.process (mixL.data(), mixR.data(), n);

    if (scopeFeed != nullptr)
    {
        if (scopeFeed->wants (ScopeFeed::song))
            scopeFeed->push (mixL.data(), mixR.data(), n, sr);
        else if (scopeFeed->wants (ScopeFeed::duet))
            scopeFeed->pushPair (youLp, youRp, mixL.data(), mixR.data(), n, sr);
    }

    // ---- monitor volume (after the scope taps: the scope reacts even with monitoring off) ----
    rigGain.setTargetValue (monitorOn.load() ? juce::Decibels::decibelsToGain (rigVolumeDb.load(), -60.0f) : 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float g = rigGain.getNextValue();
        rigL[(size_t) i] *= g;
        rigR[(size_t) i] *= g;
    }

    // the keys join "you" after the monitor volume (they can't be monitored through the interface)
    if (keysPlayed)
    {
        juce::FloatVectorOperations::add (rigL.data(), keysBuf.getReadPointer (0), n);
        juce::FloatVectorOperations::add (rigR.data(), keysBuf.getReadPointer (1), n);
    }

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

    if (scopeFeed != nullptr && scopeFeed->wants (ScopeFeed::everything))
        scopeFeed->push (mixL.data(), mixR.data(), n, sr);

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
