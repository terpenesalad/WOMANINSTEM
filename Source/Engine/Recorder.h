#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace wis
{

/** Records three 24-bit WAV files at once, written on a background thread:
      - "<name> - Mix.wav"   : exactly what you hear (song + you)
      - "<name> - Rig.wav"   : your processed instrument only
      - "<name> - DI.wav"    : your dry, unprocessed input (re-amp it later with any plugin)  */
class Recorder
{
public:
    Recorder();
    ~Recorder();

    static juce::File defaultFolder();

    /** Message thread. Returns empty string on success. */
    juce::String start (const juce::File& folder, const juce::String& baseName, double sampleRate);
    void stop();
    bool isRecording() const noexcept { return active.load(); }
    double getSecondsRecorded() const noexcept;
    juce::File getLastFolder() const { return lastFolder; }
    juce::StringArray getLastFiles() const { return lastFiles; }

    /** Audio thread. */
    void write (const float* mixL, const float* mixR, const float* rigL, const float* rigR, const float* di, int n) noexcept;

private:
    juce::TimeSliceThread writerThread { "Recorder" };
    juce::CriticalSection writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> mix, rig, dry;
    std::atomic<bool> active { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    double rate = 48000.0;
    juce::File lastFolder;
    juce::StringArray lastFiles;
};

} // namespace wis
