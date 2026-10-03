#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/Stems.h"
#include <array>
#include <atomic>
#include <memory>

namespace signalsmith { namespace stretch { template <typename, class> struct SignalsmithStretch; } }

namespace wis
{

/** A song's stems, already converted to the audio device's sample rate. Immutable once built. */
struct PlayableSong : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<PlayableSong>;

    std::array<juce::AudioBuffer<float>, numStemIds> stems;
    std::array<bool, numStemIds> present {};
    int length = 0;              // samples at `sampleRate`
    double sampleRate = 0.0;
    juce::String songId;

    /** Peak overview for drawing waveforms: per stem, min/max pairs per bucket. */
    static constexpr int overviewBuckets = 2048;
    std::array<std::vector<float>, numStemIds> overview;   // abs-peak per bucket
    std::vector<float> mixOverview;

    void buildOverviews();
};

/** Real-time multi-stem player: gain, balance, mute, solo per stem; looping; practice speed
    and transpose via a high quality phase-vocoder (Signalsmith Stretch). Lock-free on the audio side. */
class StemPlayer
{
public:
    StemPlayer();
    ~StemPlayer();

    // ---- audio thread ----
    void prepare (double sampleRate, int maxBlock);
    /** Adds the stem mix into L/R. */
    void process (float* left, float* right, int numSamples) noexcept;

    // ---- message thread ----
    void setSong (PlayableSong::Ptr song);
    PlayableSong::Ptr getSong() const;
    void releaseOldSongs();               // call periodically from the message thread

    void play()  { playing = true; }
    void pause() { playing = false; }
    void togglePlay() { playing = ! playing.load(); }
    bool isPlaying() const { return playing.load(); }

    void seekSeconds (double seconds);
    double getPositionSeconds() const;
    double getLengthSeconds() const;

    void setLoop (bool enabled, double startSeconds, double endSeconds);
    bool isLooping() const { return loopEnabled.load(); }
    double getLoopStartSeconds() const;
    double getLoopEndSeconds() const;

    void setSpeed (float ratio)          { speed = juce::jlimit (0.25f, 1.5f, ratio); }
    float getSpeed() const               { return speed.load(); }
    void setTransposeSemitones (float s) { transpose = juce::jlimit (-12.0f, 12.0f, s); }
    float getTranspose() const           { return transpose.load(); }

    struct StemControl
    {
        std::atomic<float> gainDb { 0.0f };
        std::atomic<float> balance { 0.0f };   // -1 left .. +1 right
        std::atomic<bool> mute { false };
        std::atomic<bool> solo { false };
        std::atomic<float> meter { 0.0f };     // peak since last read
    };

    StemControl& control (StemId id) { return controls[(size_t) id]; }
    std::atomic<float> masterGainDb { 0.0f };

    float readMeter (StemId id) { return controls[(size_t) id].meter.exchange (0.0f); }

    /** When the device rate changes, the song must be rebuilt at the new rate. */
    double getDeviceSampleRate() const { return deviceRate.load(); }

private:
    void computeGains (std::array<float, numStemIds>& gl, std::array<float, numStemIds>& gr) const noexcept;
    int mixInto (const PlayableSong& song, double& pos, float* L, float* R, int numSamples, bool& reachedEnd,
                 const std::array<float, numStemIds>& gl0, const std::array<float, numStemIds>& gr0,
                 const std::array<float, numStemIds>& gl1, const std::array<float, numStemIds>& gr1,
                 int rampOffset, int rampTotal) noexcept;

    std::array<StemControl, numStemIds> controls;

    mutable juce::SpinLock songLock;
    PlayableSong::Ptr song;
    juce::ReferenceCountedArray<PlayableSong> graveyard;

    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };          // in samples at song rate
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };   // seconds
    std::atomic<float> speed { 1.0f }, transpose { 0.0f };
    std::atomic<double> deviceRate { 48000.0 };

    int maxBlockSize = 512;
    std::array<float, numStemIds> lastGainL {}, lastGainR {};
    float fadeIn = 1.0f;                           // de-click ramp after seeks / loop jumps
    float fadeStep = 1.0f / 256.0f;

    // time stretch
    using Stretcher = signalsmith::stretch::SignalsmithStretch<float, void>;
    std::unique_ptr<Stretcher> stretcher;
    bool stretchActive = false;
    float lastTranspose = 0.0f;
    double inputAccumulator = 0.0;
    std::vector<float> stretchInL, stretchInR, tmpL, tmpR;
};

} // namespace wis
