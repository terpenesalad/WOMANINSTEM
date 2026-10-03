// stemsplit - command line stem separation using the same engine as the app.
//
//   stemsplit <song.mp3|flac|wav> <output folder> [--max] [--threads N] [--no-backing] [--download]
//
// Writes 24-bit WAV stems. Also used by CI as an end-to-end smoke test (--selftest).

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Separation/AudioFileLoader.h"
#include "Separation/StemSeparator.h"
#include "Separation/ModelManager.h"
#include "Separation/VocalSplitter.h"
#include "Library/SongLibrary.h"
#include <iostream>

using namespace wis;

static int fail (const juce::String& msg)
{
    std::cerr << "ERROR: " << msg << std::endl;
    return 1;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
    if (auto w = juce::WavAudioFormat().createWriterFor (stream, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

/** Synthesises a test "song": bass line + drums + a centre "voice" + wide "backing" tones. */
static juce::AudioBuffer<float> makeTestSong (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (42);
    const double bpm = 110.0, beat = 60.0 / bpm;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double inBeat = std::fmod (t, beat);
        // kick
        float s = (float) (std::sin (2 * juce::MathConstants<double>::pi * (50 + 80 * std::exp (-inBeat * 40)) * inBeat) * std::exp (-inBeat * 12)) * 0.6f;
        // hat on off-beats
        const double off = std::fmod (t + beat / 2, beat);
        s += (rng.nextFloat() * 2 - 1) * (float) std::exp (-off * 60) * 0.15f;
        // bass
        const double bassF = (std::fmod (t, beat * 4) < beat * 2) ? 55.0 : 73.4;
        s += (float) std::sin (2 * juce::MathConstants<double>::pi * bassF * t) * 0.3f;
        // centre "voice" with vibrato
        const float voice = (float) std::sin (2 * juce::MathConstants<double>::pi * (330 + 6 * std::sin (2 * juce::MathConstants<double>::pi * 5 * t)) * t) * 0.2f;
        // wide "backing" voices
        const float bvL = (float) std::sin (2 * juce::MathConstants<double>::pi * 440.0 * t) * 0.08f;
        const float bvR = (float) std::sin (2 * juce::MathConstants<double>::pi * 554.4 * t) * 0.08f;
        b.setSample (0, i, s + voice + bvL);
        b.setSample (1, i, s + voice + bvR);
    }
    return b;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;   // message manager for URL streams
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);

    if (args.isEmpty() || args.contains ("--help"))
    {
        std::cout << "stemsplit - WOMANINSTEM stem separation\n\n"
                     "  stemsplit <input audio> <output folder> [--max] [--threads N] [--no-backing] [--download]\n"
                     "  stemsplit --selftest <output folder>     (synthetic song, checks the whole pipeline)\n";
        return 0;
    }

    const bool selfTest = args.contains ("--selftest");
    SeparationSettings settings;
    settings.quality = args.contains ("--max") ? SeparationQuality::maximum : SeparationQuality::standard;
    settings.splitBackingVocals = ! args.contains ("--no-backing");
    if (auto t = args.indexOf ("--threads"); t >= 0 && t + 1 < args.size())
        settings.numThreads = args[t + 1].getIntValue();

    juce::StringArray positional;
    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--threads") { ++i; continue; }
        if (! args[i].startsWith ("--")) positional.add (args[i]);
    }

    if (positional.size() < (selfTest ? 1 : 2))
        return fail ("Missing arguments. Run with --help.");

    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (positional[selfTest ? 0 : 1]);
    outDir.createDirectory();

    // ---- models ----
    for (auto id : StemSeparator::missingModels (settings.quality))
    {
        if (! args.contains ("--download") && ! selfTest)
            return fail ("Model " + juce::String (modelSpec (id).fileName) + " not found. Put it in "
                         + ModelManager::userModelsDirectory().getFullPathName() + " or run with --download.");

        std::cout << "Downloading " << modelSpec (id).fileName << " ..." << std::endl;
        int lastPct = -1;
        auto err = ModelManager::download (id, [&] (juce::int64 done, juce::int64 total)
        {
            const int pct = (int) (100 * done / juce::jmax ((juce::int64) 1, total));
            if (pct / 10 != lastPct / 10) { std::cout << "  " << pct << "%" << std::endl; lastPct = pct; }
        }, {});
        if (err.isNotEmpty())
            return fail (err);
    }

    // ---- input ----
    juce::AudioBuffer<float> mix;
    juce::String title = "selftest";

    if (selfTest)
    {
        mix = makeTestSong (stemSampleRate, 40.0);
        writeWav (outDir.getChildFile ("input.wav"), mix, stemSampleRate);
    }
    else
    {
        const juce::File input = juce::File::getCurrentWorkingDirectory().getChildFile (positional[0]);
        std::cout << "Loading " << input.getFullPathName() << std::endl;
        auto loaded = loadAudioFile (input, stemSampleRate);
        if (! loaded.ok())
            return fail (loaded.error);
        mix = std::move (loaded.audio);
        title = loaded.title;
    }

    std::cout << "Separating " << juce::String (mix.getNumSamples() / stemSampleRate, 1) << " s of audio with "
              << (settings.numThreads > 0 ? settings.numThreads : StemSeparator::defaultThreadCount()) << " threads" << std::endl;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    StemSeparator separator;
    SeparatedStems stems;
    int lastShown = -1;
    auto err = separator.separate (mix, settings, [&] (float p, const juce::String& stage)
    {
        const int pct = (int) (p * 100);
        if (pct / 5 != lastShown / 5) { std::cout << "  [" << pct << "%] " << stage << std::endl; lastShown = pct; }
    }, {}, stems);

    if (err.isNotEmpty())
        return fail (err);

    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::cout << "Done in " << juce::String (secs, 1) << " s (" << juce::String ((mix.getNumSamples() / stemSampleRate) / secs, 2) << "x realtime)" << std::endl;

    // ---- write ----
    juce::AudioBuffer<float> sum (2, mix.getNumSamples());
    sum.clear();
    for (auto& s : allStems())
    {
        auto& b = stems.audio[(size_t) s.id];
        std::cout << "  " << juce::String (s.displayName).paddedRight (' ', 16) << (stems.present[(size_t) s.id] ? "present " : "silent  ")
                  << juce::String (stems.rmsDb[(size_t) s.id], 1) << " dB RMS" << std::endl;
        if (b.getNumSamples() == mix.getNumSamples())
            for (int ch = 0; ch < 2; ++ch)
                sum.addFrom (ch, 0, b, ch, 0, b.getNumSamples());
        if (stems.present[(size_t) s.id])
            writeWav (outDir.getChildFile (juce::File::createLegalFileName (title) + " - " + s.key + ".wav"), b, stemSampleRate);
    }

    // stems should add back up to (approximately) the original mix
    double errPow = 0, mixPow = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < mix.getNumSamples(); ++i)
        {
            const double d = sum.getSample (ch, i) - mix.getSample (ch, i);
            errPow += d * d;
            mixPow += (double) mix.getSample (ch, i) * mix.getSample (ch, i);
        }
    const double reconstructionDb = 10.0 * std::log10 ((errPow + 1e-20) / (mixPow + 1e-20));
    std::cout << "Reconstruction error: " << juce::String (reconstructionDb, 1) << " dB (lower is better)" << std::endl;

    if (selfTest)
    {
        // Library round trip
        SongInfo info;
        info.id = "selftest-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30));
        info.title = "Self Test";
        info.durationSeconds = mix.getNumSamples() / stemSampleRate;
        info.added = juce::Time::getCurrentTime();
        if (auto e = SongLibrary::saveSong (info, stems); e.isNotEmpty())
            return fail ("Library save failed: " + e);
        StemBuffers loaded;
        if (auto e = SongLibrary::loadStems (info, loaded); e.isNotEmpty())
            return fail ("Library load failed: " + e);
        SongLibrary::removeSong (info.id);

        if (! stems.present[(size_t) StemId::bass])  return fail ("Self test: no bass detected");
        if (! stems.present[(size_t) StemId::drums]) return fail ("Self test: no drums detected");
        if (reconstructionDb > -12.0)                return fail ("Self test: stems don't sum back to the mix");
        std::cout << "SELFTEST PASSED" << std::endl;
    }

    std::cout << "Stems written to " << outDir.getFullPathName() << std::endl;
    return 0;
}
