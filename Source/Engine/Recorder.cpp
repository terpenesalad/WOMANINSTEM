#include "Recorder.h"

namespace wis
{

Recorder::Recorder()  { writerThread.startThread (juce::Thread::Priority::high); }
Recorder::~Recorder() { stop(); writerThread.stopThread (2000); }

juce::File Recorder::defaultFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("WOMANINSTEM Recordings");
    dir.createDirectory();
    return dir;
}

static std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> makeWriter (const juce::File& file, double sr, int channels,
                                                                           juce::TimeSliceThread& thread)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return {};

    auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (channels).withBitsPerSample (24);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    if (writer == nullptr)
        return {};

    return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), thread, 65536);
}

juce::String Recorder::start (const juce::File& folder, const juce::String& baseName, double sampleRate)
{
    stop();

    folder.createDirectory();
    rate = sampleRate;
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    const auto name = juce::File::createLegalFileName ((baseName.isNotEmpty() ? baseName + " " : juce::String()) + stamp);

    auto fMix = folder.getChildFile (name + " - Mix.wav");
    auto fRig = folder.getChildFile (name + " - Rig.wav");
    auto fDi  = folder.getChildFile (name + " - DI.wav");

    auto m = makeWriter (fMix, sampleRate, 2, writerThread);
    auto r = makeWriter (fRig, sampleRate, 2, writerThread);
    auto d = makeWriter (fDi,  sampleRate, 1, writerThread);

    if (m == nullptr || r == nullptr || d == nullptr)
        return "Couldn't create recording files in " + folder.getFullPathName();

    {
        const juce::ScopedLock sl (writerLock);
        mix = std::move (m); rig = std::move (r); dry = std::move (d);
    }

    lastFolder = folder;
    lastFiles = { fMix.getFullPathName(), fRig.getFullPathName(), fDi.getFullPathName() };
    samplesWritten = 0;
    active = true;
    return {};
}

void Recorder::stop()
{
    active = false;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> m, r, d;
    {
        const juce::ScopedLock sl (writerLock);
        std::swap (m, mix); std::swap (r, rig); std::swap (d, dry);
    }
    // destructors flush the files
}

double Recorder::getSecondsRecorded() const noexcept
{
    return (double) samplesWritten.load() / rate;
}

void Recorder::write (const float* mixL, const float* mixR, const float* rigL, const float* rigR, const float* di, int n) noexcept
{
    if (! active.load())
        return;

    const juce::ScopedTryLock sl (writerLock);
    if (! sl.isLocked() || mix == nullptr)
        return;

    const float* m[] = { mixL, mixR };
    const float* r[] = { rigL, rigR };
    const float* d[] = { di };
    mix->write (m, n);
    rig->write (r, n);
    dry->write (d, n);
    samplesWritten += n;
}

} // namespace wis
