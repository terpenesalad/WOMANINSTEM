#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Rig/RigProcessor.h"
#include "StemPlayer.h"
#include "Recorder.h"
#include "ScopeFeed.h"

namespace wis
{

/** The real-time heart of the app: one device callback that reads your instrument input,
    runs it through the rig, mixes it with the stems, records, meters and protects your ears. */
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine (RigProcessor& rig, StemPlayer& player, Recorder& recorder);

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    /** Input selection: a device input channel number (0-based), or stereoSum for "1 + 2". */
    static constexpr int stereoSum = 1000;
    static constexpr int noInput = -1;
    std::atomic<int> selectedInput { 0 };

    std::atomic<bool> monitorOn { true };
    std::atomic<float> rigVolumeDb { 0.0f };
    std::atomic<float> masterVolumeDb { 0.0f };

    float readOutputPeakL() { return outPeakL.exchange (0.0f); }
    float readOutputPeakR() { return outPeakR.exchange (0.0f); }
    bool readLimiterActive() { return limiterHit.exchange (false); }

    double getSampleRate() const { return sampleRate.load(); }
    int getBlockSize() const { return blockSize.load(); }
    /** Estimated round-trip latency in ms (driver-reported input + output + one buffer). */
    double getRoundTripLatencyMs() const { return latencyMs.load(); }
    juce::StringArray getActiveInputNames() const;
    juce::Array<int> getActiveInputChannels() const;

    /** The oscilloscope window's tap (nullptr = none). */
    std::atomic<ScopeFeed*> scope { nullptr };

    std::function<void (double newRate)> onSampleRateChanged;   // called on the audio thread start - message thread must rebuild song

private:
    RigProcessor& rig;
    StemPlayer& player;
    Recorder& recorder;

    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> blockSize { 0 };
    std::atomic<double> latencyMs { 0.0 };

    // map device channel number -> index in the callback's input array
    std::array<int, 128> inputIndexForChannel {};
    juce::CriticalSection namesLock;
    juce::StringArray activeInputNames;
    juce::Array<int> activeInputChannels;
    std::atomic<int> firstInput { -1 }, secondInput { -1 };

    std::vector<float> inMono, rigL, rigR, mixL, mixR;
    juce::SmoothedValue<float> rigGain, masterGain;

    std::atomic<float> outPeakL { 0 }, outPeakR { 0 };
    std::atomic<bool> limiterHit { false };
};

} // namespace wis
