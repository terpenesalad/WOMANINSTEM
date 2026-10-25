// rigtest - automated checks for the real-time DSP (amps, drives, cabs, presets, tuner, vocal split, player).
// Exit code 0 = all passed. Prints levels so models can be loudness-matched.

#include <juce_core/juce_core.h>
#include "Rig/RigProcessor.h"
#include "Rig/AmpModels.h"
#include "Rig/CabSim.h"
#include "Rig/Tuner.h"
#include "Separation/VocalSplitter.h"
#include "Engine/StemPlayer.h"
#include "Engine/AudioEngine.h"
#include "Daw/Plugins/BuiltinProcessor.h"
#include "Engine/Recorder.h"
#include <iostream>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace wis;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static float rmsDb (const float* d, int n)
{
    double s = 0; for (int i = 0; i < n; ++i) s += (double) d[i] * d[i];
    return (float) juce::Decibels::gainToDecibels (std::sqrt (s / juce::jmax (1, n)), -150.0);
}

static bool allFinite (const float* d, int n)
{
    for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) return false;
    return true;
}

/** A plucked string-ish test signal: decaying harmonics, re-plucked every half second. */
static std::vector<float> pluck (double sr, int n, float freq, float levelDb)
{
    std::vector<float> v ((size_t) n);
    const float a = juce::Decibels::decibelsToGain (levelDb);
    for (int i = 0; i < n; ++i)
    {
        const double t = std::fmod (i / sr, 0.5);
        float s = 0;
        for (int h = 1; h <= 8; ++h)
            s += (float) (std::sin (2 * juce::MathConstants<double>::pi * freq * h * t) * std::exp (-t * (2.0 + h)) / h);
        v[(size_t) i] = s * a;
    }
    return v;
}

/** Loudness with the K-weighting curve of ITU BS.1770 (what streaming services use), in dB. */
static float loudnessDb (const float* d, int n, double sr)
{
    using AC = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::IIR::Filter<float> shelf (AC::makeHighShelf (sr, 1681.0, 0.71f, juce::Decibels::decibelsToGain (4.0f)));
    juce::dsp::IIR::Filter<float> hp (AC::makeHighPass (sr, 38.0, 0.5f));
    double s = 0;
    for (int i = 0; i < n; ++i) { const float x = hp.processSample (shelf.processSample (d[i])); s += (double) x * x; }
    return (float) juce::Decibels::gainToDecibels (std::sqrt (s / juce::jmax (1, n)), -150.0);
}

/** Spectral centroid (Hz) of a signal: a single number for "how bright is it". */
static float centroidHz (const float* d, int n, double sr)
{
    constexpr int order = 14, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    double num = 0, den = 0;
    for (int start = 0; start + N <= n; start += N)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = d[start + i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k) { const double f = k * sr / N; num += f * buf[(size_t) k]; den += buf[(size_t) k]; }
    }
    return den > 0 ? (float) (num / den) : 0.0f;
}

/** A plucked bass DI (Karplus-Strong string, roundwound brightness) playing a simple line. */
static std::vector<float> bassLine (double sr, int n, bool guitar)
{
    std::vector<float> out ((size_t) n, 0.0f);
    const float notes[] = { 41.2f, 41.2f, 55.0f, 61.74f, 73.42f, 55.0f, 49.0f, 61.74f };
    juce::Random rng (7);
    const int noteLen = (int) (sr * 0.5);
    for (int k = 0; k * noteLen < n; ++k)
    {
        const float f = notes[k % 8] * (guitar ? 4.0f : 1.0f);
        const int L = juce::jmax (2, (int) (sr / f));
        std::vector<float> line ((size_t) L);
        float lp = 0;
        for (auto& v : line) { const float w = rng.nextFloat() * 2 - 1; lp += (w - lp) * 0.6f; v = lp * 0.5f; }   // pick attack
        int idx = 0;
        for (int i = 0; i < noteLen && k * noteLen + i < n; ++i)
        {
            const int nx = (idx + 1) % L;
            const float y = line[(size_t) idx];
            line[(size_t) idx] = 0.996f * (0.7f * line[(size_t) idx] + 0.3f * line[(size_t) nx]);   // bright, long sustain
            out[(size_t) (k * noteLen + i)] += y * (i > noteLen - 400 ? (noteLen - i) / 400.0f : 1.0f);
            idx = nx;
        }
    }
    return out;
}

/** Strummed chords (Karplus-Strong strings with a pick, the strings struck one after another) for guitar, or a picked
    bass line: closer to real playing than a single pluck, so loudness matching doesn't hinge on one note. */
static std::vector<float> strum (double sr, int n, bool bass, float levelDb)
{
    if (bass)
    {
        // fingerstyle: a soft, dark excitation and strings that lose their top quickly
        std::vector<float> v ((size_t) n, 0.0f);
        const float notes[] = { 41.2f, 41.2f, 55.0f, 61.74f, 73.42f, 55.0f, 49.0f, 61.74f };
        juce::Random rng (5);
        const int noteLen = (int) (sr * 0.5);
        for (int k = 0; k * noteLen < n; ++k)
        {
            const int L = juce::jmax (2, (int) (sr / notes[k % 8]));
            std::vector<float> line ((size_t) L);
            float lp = 0, lp2 = 0;
            for (auto& x : line) { const float w = rng.nextFloat() * 2 - 1; lp += (w - lp) * 0.25f; lp2 += (lp - lp2) * 0.25f; x = lp2; }
            int idx = 0;
            for (int i = 0; i < noteLen && k * noteLen + i < n; ++i)
            {
                const int nx = (idx + 1) % L;
                const float y = line[(size_t) idx];
                line[(size_t) idx] = 0.998f * (0.5f * line[(size_t) idx] + 0.5f * line[(size_t) nx]);
                v[(size_t) (k * noteLen + i)] += y * (i > noteLen - 400 ? (noteLen - i) / 400.0f : 1.0f);
                idx = nx;
            }
        }
        float pk = 0; for (auto x : v) pk = juce::jmax (pk, std::abs (x));
        for (auto& x : v) x *= juce::Decibels::decibelsToGain (levelDb) / juce::jmax (1.0e-6f, pk);
        return v;
    }
    std::vector<float> out ((size_t) n, 0.0f);
    static const float chords[4][6] = { { 82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f },     // E
                                        { 110.0f, 164.81f, 220.0f, 277.18f, 329.63f, 440.0f },        // A
                                        { 146.83f, 220.0f, 293.66f, 369.99f, 440.0f, 587.33f },       // D (5 strings + octave)
                                        { 98.0f, 123.47f, 146.83f, 196.0f, 246.94f, 392.0f } };       // G
    juce::Random rng (11);
    const int strumLen = (int) (sr * 0.75);
    for (int k = 0; k * strumLen < n; ++k)
    {
        const bool down = (k % 2) == 0;
        for (int s = 0; s < 6; ++s)
        {
            const int string = down ? s : 5 - s;
            const float f = chords[(k / 2) % 4][string];
            const int start = k * strumLen + (int) (sr * 0.012 * s);
            const int L = juce::jmax (2, (int) (sr / f));
            std::vector<float> line ((size_t) L);
            float lp = 0;
            for (auto& v : line) { const float w = rng.nextFloat() * 2 - 1; lp += (w - lp) * 0.75f; v = lp * (down ? 0.5f : 0.35f); }
            int idx = 0;
            for (int i = 0; i < strumLen && start + i < n; ++i)
            {
                const int nx = (idx + 1) % L;
                const float y = line[(size_t) idx];
                line[(size_t) idx] = 0.9965f * (0.62f * line[(size_t) idx] + 0.38f * line[(size_t) nx]);
                out[(size_t) (start + i)] += y * (i > strumLen - 600 ? (strumLen - i) / 600.0f : 1.0f);
                idx = nx;
            }
        }
    }
    float pk = 0; for (auto x : out) pk = juce::jmax (pk, std::abs (x));
    for (auto& x : out) x *= juce::Decibels::decibelsToGain (levelDb) / juce::jmax (1.0e-6f, pk);
    return out;
}

static void writeWav (const juce::File& f, const float* d, int n, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24);
    if (auto w = juce::WavAudioFormat().createWriterFor (os, opts))
    {
        const float* chans[] = { d };
        w->writeFromFloatArrays (chans, 1, n);
    }
}

static void writeWavStereo (const juce::File& f, const float* l, const float* r, int n, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    if (auto w = juce::WavAudioFormat().createWriterFor (os, opts))
    {
        const float* chans[] = { l, r };
        w->writeFromFloatArrays (chans, 2, n);
    }
}

/** rigtest --render-file <in.wav> <folder> [preset numbers, e.g. 1,2,10] : a real DI recording through factory presets
    (stereo, 24-bit, at the file's sample rate). WIS_RENDER_SET="param=value,..." overrides parameters after the preset. */
static int renderFile (const juce::File& inFile, const juce::File& dir, const juce::String& which)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inFile));
    if (reader == nullptr) { std::cout << "Can't read " << inFile.getFullPathName() << std::endl; return 1; }
    const double sr = reader->sampleRate;
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> in (1, n);
    reader->read (&in, 0, n, 0, true, false);
    dir.createDirectory();
    auto names = RigProcessor::factoryPresetNames();
    juce::Array<int> list;
    for (auto& t : juce::StringArray::fromTokens (which, ",", "")) if (t.isNotEmpty()) list.add (t.getIntValue());
    if (list.isEmpty()) for (int i = 0; i < names.size(); ++i) list.add (i);
    for (int pi : list)
    {
        RigProcessor rig;
        rig.loadFactoryPreset (pi);
        for (auto& kv : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("WIS_RENDER_SET", {}), ",", ""))
            if (kv.contains ("=")) rig.setParam (kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        rig.prepareToPlay (sr, 256);
        rig.waitForBackgroundWork();
        std::vector<float> L ((size_t) n), R ((size_t) n);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < n; pos += 256)
            rig.processMonoToStereo (in.getReadPointer (0) + pos, L.data() + pos, R.data() + pos, juce::jmin (256, n - pos));
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        writeWavStereo (dir.getChildFile (juce::String (pi).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (names[pi]) + ".wav"), L.data(), R.data(), n, sr);
        std::cout << pi << " " << names[pi] << "  peak " << juce::Decibels::gainToDecibels (juce::jmax (juce::FloatVectorOperations::findMaximum (L.data(), n), -juce::FloatVectorOperations::findMinimum (L.data(), n)))
                  << " dB  loudness " << loudnessDb (L.data(), n, sr) << " dB  " << juce::String (100.0 * ms / (1000.0 * n / sr), 1) << "% of real time" << std::endl;
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--cab-irs")
    {
        // rigtest --cab-irs <folder> [sampleRate]: every built-in cabinet's close-mic IR at a few mic settings, timed
        juce::File dir { juce::String (juce::CharPointer_UTF8 (argv[2])) };
        dir.createDirectory();
        const double sr = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 48000.0;
        auto names = cabTypeNames();
        for (int t = 0; t < (int) CabType::impulseResponse; ++t)
            for (auto [mic, pos, dist] : { std::tuple<int, float, float> { 0, 0.3f, 0.1f }, { 0, 0.0f, 0.1f }, { 0, 0.8f, 0.1f }, { 0, 0.3f, 0.6f }, { 1, 0.3f, 0.1f }, { 2, 0.3f, 0.1f }, { 3, 0.3f, 0.1f } })
            {
                const auto start = juce::Time::getMillisecondCounterHiRes();
                auto ir = CabSim::designImpulseResponse ((CabType) t, (MicType) mic, pos, dist, sr);
                const double ms = juce::Time::getMillisecondCounterHiRes() - start;
                const auto file = dir.getChildFile (juce::String (t).paddedLeft ('0', 2) + "_m" + juce::String (mic) + "_p" + juce::String (pos, 1) + "_d" + juce::String (dist, 1) + ".wav");
                writeWav (file, ir.getReadPointer (0), ir.getNumSamples(), sr);
                std::cout << names[t] << " mic " << mic << " pos " << pos << " dist " << dist << ": " << ir.getNumSamples() << " taps, " << juce::String (ms, 0) << " ms" << std::endl;
            }
        auto rooms = AmpRoom::roomNames();
        for (int rm = 0; rm < rooms.size(); ++rm)
            for (auto cab : { CabType::brit4x12, CabType::american2x12, CabType::bass8x10 })
            {
                const auto start = juce::Time::getMillisecondCounterHiRes();
                auto ir = AmpRoom::designImpulseResponse (rm, cab, sr);
                const double ms = juce::Time::getMillisecondCounterHiRes() - start;
                writeWavStereo (dir.getChildFile ("room" + juce::String (rm) + "_cab" + juce::String ((int) cab) + ".wav"), ir.getReadPointer (0), ir.getReadPointer (1), ir.getNumSamples(), sr);
                std::cout << rooms[rm] << " / " << names[(int) cab] << ": " << ir.getNumSamples() << " taps, " << juce::String (ms, 0) << " ms" << std::endl;
            }
        return 0;
    }
    if (argc > 3 && juce::String (argv[1]) == "--render-file")
        return renderFile (juce::File (juce::String (juce::CharPointer_UTF8 (argv[2]))), juce::File (juce::String (juce::CharPointer_UTF8 (argv[3]))),
                           argc > 4 ? juce::String (argv[4]) : juce::String());
    const bool calibrate = argc > 1 && juce::String (argv[1]) == "--calibrate";
    if (argc > 2 && juce::String (argv[1]) == "--render")
    {
        // rigtest --render <folder>: every factory preset playing a plucked line, for listening / spectrum checks
        const double sr = 48000.0;
        const int n = (int) (sr * 6);
        juce::File dir { juce::String (juce::CharPointer_UTF8 (argv[2])) };
        dir.createDirectory();
        auto names = RigProcessor::factoryPresetNames();
        for (bool guitar : { false, true })
            writeWav (dir.getChildFile (guitar ? "_input_guitar.wav" : "_input_bass.wav"), bassLine (sr, n, guitar).data(), n, sr);
        for (int pi = 0; pi < names.size(); ++pi)
        {
            RigProcessor rig;
            rig.loadFactoryPreset (pi);
            // WIS_RENDER_OFF=amp_on,cab_on,... switches blocks off (to see what each contributes)
            for (auto& id : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("WIS_RENDER_OFF", {}), ",", ""))
                if (id.isNotEmpty()) rig.setParam (id, id == "char_type" ? 0.0f : 0.0f);
            rig.prepareToPlay (sr, 256);
            auto in = bassLine (sr, n, ! names[pi].startsWith ("Bass"));
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int pos = 0; pos < n; pos += 256)
                rig.processMonoToStereo (in.data() + pos, L.data() + pos, R.data() + pos, juce::jmin (256, n - pos));
            writeWav (dir.getChildFile (juce::String (pi).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (names[pi]) + ".wav"), L.data(), n, sr);
        }
        std::cout << "Rendered " << names.size() << " presets to " << dir.getFullPathName() << std::endl;
        return 0;
    }
    const double sr = 48000.0;
    const int block = 128;
    const int seconds = 3;
    const int n = (int) sr * seconds;

    // ---------------------------------------------------------------------------------------------
    std::cout << "Amp models (pluck at -18 dBFS peak-ish, E2):" << std::endl;
    auto input = pluck (sr, n, 82.41f, -12.0f);
    std::cout << "  input RMS " << rmsDb (input.data(), n) << " dB" << std::endl;

    std::cout << "Tone stack:" << std::endl;
    {
        bool passive = true, scoop = true;
        juce::Random rng (3);
        for (auto comps : { ToneStack::tweed(), ToneStack::blackface(), ToneStack::british(), ToneStack::chime(), ToneStack::modern() })
        {
            for (int trial = 0; trial < 30; ++trial)
            {
                ToneStack ts;
                ts.design (192000.0, comps, rng.nextDouble(), rng.nextDouble(), rng.nextDouble());
                for (double f = 20; f < 20000; f *= 1.1) passive = passive && ts.magnitudeAt (f, 192000.0) < 1.02;
            }
            ToneStack noon;
            noon.design (192000.0, comps, 0.5, 0.5, 0.5);
            scoop = scoop && noon.magnitudeAt (600, 192000.0) < noon.magnitudeAt (80, 192000.0) && noon.magnitudeAt (600, 192000.0) < noon.magnitudeAt (5000, 192000.0);
        }
        check (passive, "The passive tone stack never boosts (any knob settings)");
        check (scoop, "...and has the classic mid scoop at noon");
        ToneStack lo, hi;
        lo.design (192000.0, ToneStack::british(), 0.5, 0.5, 0.0);
        hi.design (192000.0, ToneStack::british(), 0.5, 0.5, 1.0);
        check (hi.magnitudeAt (80, 192000.0) > lo.magnitudeAt (80, 192000.0) * 1.5, "Bass knob works (" + juce::String (juce::Decibels::gainToDecibels (hi.magnitudeAt (80, 192000.0) / lo.magnitudeAt (80, 192000.0)), 1) + " dB at 80 Hz)");
    }

    for (int m = 0; m < (int) AmpType::count; ++m)
    {
        if (m == (int) AmpType::flatDi || m == (int) AmpType::namCapture) continue;
        if (calibrate)
        {
            BuiltInAmp amp;
            amp.prepare (sr, block);
            const bool bassModel = ampTypeIsBass ((AmpType) m);
            auto in = pluck (sr, n, bassModel ? 41.2f : 82.41f, -12.0f);
            amp.setParameters ((AmpType) m, 5, 5, 5, 5, 5, 7);
            for (int pos = 0; pos < n; pos += block) amp.process (in.data() + pos, juce::jmin (block, n - pos));
            std::cout << "  CAL " << ampTypeNames()[m] << " " << rmsDb (in.data() + n / 3, n - n / 3) << std::endl;
        }
        BuiltInAmp amp;
        amp.prepare (sr, block);
        auto buf = input;
        for (float gain : { 2.0f, 5.0f, 9.0f })
        {
            buf = input;
            amp.reset();
            amp.setParameters ((AmpType) m, gain, 5, 5, 5, 5, 6);
            for (int pos = 0; pos < n; pos += block)
                amp.process (buf.data() + pos, juce::jmin (block, n - pos));
            const float lvl = rmsDb (buf.data() + n / 3, n - n / 3);
            check (allFinite (buf.data(), n) && lvl > -45.0f && lvl < 3.0f,
                   ampTypeNames()[m] + " gain " + juce::String (gain, 0) + ": " + juce::String (lvl, 1) + " dB RMS");
        }
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Drive pedals:" << std::endl;
    for (int t = 0; t < (int) DriveType::count; ++t)
    {
        DrivePedal d;
        d.prepare (sr, block);
        d.setParameters ((DriveType) t, 6, 5, 5);
        auto buf = pluck (sr, n, 110.0f, -18.0f);
        for (int pos = 0; pos < n; pos += block)
            d.process (buf.data() + pos, juce::jmin (block, n - pos));
        const float lvl = rmsDb (buf.data(), n);
        check (allFinite (buf.data(), n) && lvl > -40 && lvl < 3, driveTypeNames()[t] + ": " + juce::String (lvl, 1) + " dB RMS");
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Cabinets:" << std::endl;
    for (int c = 0; c < (int) CabType::impulseResponse; ++c)
    {
        CabSim cab;
        cab.prepare (sr, block);
        cab.setParameters ((CabType) c, 20, 20000);
        cab.prepare (sr, block);
        std::vector<float> buf ((size_t) n);
        juce::Random rng (1);
        for (auto& s : buf) s = (rng.nextFloat() * 2 - 1) * 0.25f;
        for (int pos = 0; pos < n; pos += block)
            cab.process (buf.data() + pos, juce::jmin (block, n - pos));
        const float lvl = rmsDb (buf.data(), n);
        check (allFinite (buf.data(), n) && lvl > -40 && lvl < 0, cabTypeNames()[c] + ": " + juce::String (lvl, 1) + " dB RMS, centroid "
               + juce::String ((int) centroidHz (buf.data(), n, sr)) + " Hz (white noise in)");
    }
    {
        auto irC = CabSim::designImpulseResponse (CabType::brit4x12, MicType::dynamic, 0.0f, 0.0f, sr);
        auto irE = CabSim::designImpulseResponse (CabType::brit4x12, MicType::dynamic, 1.0f, 0.0f, sr);
        auto irB = CabSim::designImpulseResponse (CabType::bass1x15Vintage, MicType::dynamic, 0.3f, 0.0f, sr);
        auto irR = CabSim::designImpulseResponse (CabType::brit4x12, MicType::dynamic, 0.3f, 1.0f, sr);
        auto cen = [&] (const juce::AudioBuffer<float>& ir)
        {
            std::vector<float> v ((size_t) 32768, 0.0f);
            for (int i = 0; i < ir.getNumSamples(); ++i) v[(size_t) i] = ir.getSample (0, i);
            return centroidHz (v.data(), 32768, sr);
        };
        bool finite = true;
        for (auto* b : { &irC, &irE, &irB, &irR }) finite = finite && allFinite (b->getReadPointer (0), b->getNumSamples());
        check (finite && cen (irE) < cen (irC) * 0.8f, "Mic at the cone edge is darker than at the centre (" + juce::String ((int) cen (irC)) + " -> " + juce::String ((int) cen (irE)) + " Hz)");
        check (cen (irB) < cen (irC) * 0.6f, "A vintage 1x15 bass cab is much darker than a 4x12 (" + juce::String ((int) cen (irB)) + " Hz)");
        {
            // a mic further back hears more of the floor and the room: more of its energy arrives after the direct sound
            auto lateShare = [] (const juce::AudioBuffer<float>& ir)
            {
                const float* d = ir.getReadPointer (0);
                int pk = 0;
                for (int i = 0; i < ir.getNumSamples(); ++i) if (std::abs (d[i]) > std::abs (d[pk])) pk = i;
                double early = 0, late = 0;
                for (int i = 0; i < ir.getNumSamples(); ++i) (i < pk + 72 ? early : late) += (double) d[i] * d[i];
                return late / juce::jmax (1.0e-12, early + late);
            };
            check (lateShare (irR) > lateShare (irC) * 1.4, "A mic 1 m back hears more reflections (" + juce::String (lateShare (irC), 3) + " -> " + juce::String (lateShare (irR), 3) + ")");
        }
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Factory presets through the full rig:" << std::endl;
    {
        RigProcessor rig;
        rig.prepareToPlay (sr, block);
        auto names = RigProcessor::factoryPresetNames();
        std::vector<float> L ((size_t) n), R ((size_t) n);

        for (int pi = 0; pi < names.size(); ++pi)
        {
            rig.loadFactoryPreset (pi);
            rig.prepareToPlay (sr, block);   // builds the preset's cabinet right away
            check (rig.waitForBackgroundWork(), names[pi] + ": cabinet, room and reverb built");
            const bool bass = names[pi].startsWith ("Bass");
            auto in = names[pi].startsWith ("Vocal") ? pluck (sr, n, 164.8f, -14.0f) : strum (sr, n, bass, -8.0f);

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos < n; pos += block)
            {
                const int k = juce::jmin (block, n - pos);
                rig.processMonoToStereo (in.data() + pos, L.data() + pos, R.data() + pos, k);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const float lvl = rmsDb (L.data() + n / 3, n - n / 3);
            const double cpu = 100.0 * ms / (1000.0 * seconds);
            const float loud = loudnessDb (L.data() + n / 3, n - n / 3, sr);
            if (calibrate) std::cout << "  CALP " << pi << " " << names[pi] << " loudness " << loud
                                     << " outLevel " << rig.getState().getRawParameterValue (pid::outLevel)->load() << std::endl;
            // Guitar presets are levelled on real recorded playing (rigtest --render-file, -18 LU, peaks under -1 dBFS) and the
            // rest on these test signals (-14 LU), so each preset is checked against the loudness it was levelled at.
            // Re-run rigtest --calibrate after changing the DSP or a preset and update the table.
            static const float reference[] = { -10.2f, -11.5f, -13.3f, -18.2f, -12.7f, -13.7f, -14.1f, -15.5f, -16.9f, -13.2f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.9f, -15.4f, -17.8f, -15.1f, -14.0f, -15.4f, -15.1f, -15.2f, -12.1f, -15.1f, -14.1f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -14.0f, -13.7f, -14.0f, -14.0f };
            if (pi < (int) std::size (reference) && ! names[pi].contains ("DI"))
                check (std::abs (loud - reference[pi]) < 1.5f, names[pi] + " keeps its level (" + juce::String (loud, 1) + " LU, levelled at " + juce::String (reference[pi], 1) + ")");
            check (allFinite (L.data(), n) && allFinite (R.data(), n) && lvl > -40 && lvl < 0,
                   names[pi] + ": " + juce::String (lvl, 1) + " dB RMS, CPU " + juce::String (cpu, 2) + "% of one core");
        }

        // preset save/load round trip
        rig.loadFactoryPreset (3);
        auto saved = rig.createPresetState();
        rig.loadFactoryPreset (0);
        rig.restorePresetState (saved);
        check ((int) rig.getState().getRawParameterValue (pid::ampModel)->load() == (int) AmpType::tweedBreakup, "Preset state round trip");

        // a rig saved by an older version (amp "Hot Lead" = 2, cab "4x10 Bass" = 3) comes back as the same sounds
        auto old = rig.getState().copyState();
        for (auto child : old)
        {
            if (child.getProperty ("id").toString() == pid::ampModel) child.setProperty ("value", 2, nullptr);
            if (child.getProperty ("id").toString() == pid::cabType) child.setProperty ("value", 3, nullptr);
        }
        old.removeProperty ("rigVersion", nullptr);
        rig.restorePresetState (old);
        check ((int) rig.getState().getRawParameterValue (pid::ampModel)->load() == (int) AmpType::modernHighGain
               && (int) rig.getState().getRawParameterValue (pid::cabType)->load() == (int) CabType::bass4x10Horn,
               "Presets and songs from older versions keep their amp and cabinet");
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Tone character (centroid = brightness):" << std::endl;
    {
        auto bassIn = pluck (sr, n, 55.0f, -14.0f);
        // brighten the test pluck so the string models have something to remove
        {
            auto orig = bassIn;
            for (int i = 1; i < n; ++i) bassIn[(size_t) i] = orig[(size_t) i] + 2.4f * (orig[(size_t) i] - orig[(size_t) i - 1]);
        }
        auto run = [&] (int preset)
        {
            RigProcessor rig;
            rig.loadFactoryPreset (preset);
            rig.prepareToPlay (sr, block);
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int pos = 0; pos < n; pos += block)
                rig.processMonoToStereo (bassIn.data() + pos, L.data() + pos, R.data() + pos, juce::jmin (block, n - pos));
            return centroidHz (L.data(), n, sr);
        };
        const float sixties = run (10), motown = run (12), hifi = run (15), growl = run (14);
        check (sixties < hifi * 0.7f && motown < hifi * 0.7f, "60s / Motown bass is much rounder than modern hi-fi (centroid "
               + juce::String ((int) sixties) + " / " + juce::String ((int) motown) + " vs " + juce::String ((int) hifi) + " Hz)");
        check (growl > sixties, "Modern growl is brighter than the 60s bass (" + juce::String ((int) growl) + " Hz)");

        InstrumentCharacter c;
        c.prepare (sr);
        c.setParameters (CharacterType::violinBass, 10);
        auto v = bassIn;
        for (int pos = 0; pos < n; pos += block) c.process (v.data() + pos, juce::jmin (block, n - pos));
        check (allFinite (v.data(), n) && centroidHz (v.data(), n, sr) < centroidHz (bassIn.data(), n, sr) * 0.8f,
               "Violin-bass strings darken the tone (" + juce::String ((int) centroidHz (bassIn.data(), n, sr)) + " -> " + juce::String ((int) centroidHz (v.data(), n, sr)) + " Hz)");
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Tuner:" << std::endl;
    for (float f : { 30.87f, 41.20f, 55.0f, 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 440.0f, 659.26f })
    {
        Tuner tuner;
        tuner.prepare (sr);
        auto sig = pluck (sr, (int) (sr * 0.45), f, -20.0f);
        for (int pos = 0; pos + block <= (int) sig.size(); pos += block)
            tuner.push (sig.data() + pos, block);
        Tuner::Reading r;
        for (int i = 0; i < 4; ++i) r = tuner.analyse();
        const float expectedMidi = 69.0f + 12.0f * std::log2 (f / 440.0f);
        const bool ok = r.valid && std::abs (r.midiNote + r.cents / 100.0f - expectedMidi) < 0.05f;
        check (ok, juce::String (f, 2) + " Hz -> " + (r.valid ? r.noteName + juce::String (r.octave) + " " + juce::String (r.cents, 1) + " cents" : juce::String ("no reading")));
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Lead / backing vocal split:" << std::endl;
    {
        const int len = (int) (44100 * 6);
        juce::AudioBuffer<float> vox (2, len), lead, backing;
        for (int i = 0; i < len; ++i)
        {
            const double t = i / 44100.0;
            const float centre = (float) std::sin (2 * juce::MathConstants<double>::pi * 300 * t) * 0.3f;
            const float wideL  = (float) std::sin (2 * juce::MathConstants<double>::pi * 1200 * t) * 0.2f;
            const float wideR  = (float) std::sin (2 * juce::MathConstants<double>::pi * 1500 * t) * 0.2f;
            vox.setSample (0, i, centre + wideL);
            vox.setSample (1, i, centre + wideR);
        }
        VocalSplitter::split (vox, lead, backing);

        // centre tone should be in lead, wide tones in backing: measure via correlation with references
        double leadCentre = 0, backWide = 0, recon = 0, total = 0;
        for (int i = 4096; i < len - 4096; ++i)
        {
            const double t = i / 44100.0;
            const double c = std::sin (2 * juce::MathConstants<double>::pi * 300 * t);
            const double wl = std::sin (2 * juce::MathConstants<double>::pi * 1200 * t);
            leadCentre += lead.getSample (0, i) * c;
            backWide   += backing.getSample (0, i) * wl;
            const double d = lead.getSample (0, i) + backing.getSample (0, i) - vox.getSample (0, i);
            recon += d * d; total += (double) vox.getSample (0, i) * vox.getSample (0, i);
        }
        const double cnt = len - 8192;
        check (leadCentre / cnt > 0.12, "Centre voice lands in Lead (" + juce::String (leadCentre / cnt * 2, 3) + " of 0.3)");
        check (backWide / cnt > 0.07, "Wide voices land in Backing (" + juce::String (backWide / cnt * 2, 3) + " of 0.2)");
        check (10 * std::log10 (recon / total + 1e-30) < -80, "Lead + Backing == original");
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Stem player:" << std::endl;
    {
        StemPlayer player;
        player.prepare (sr, 256);
        PlayableSong::Ptr song = new PlayableSong();
        song->sampleRate = sr;
        song->length = (int) sr * 4;
        for (int s = 0; s < numStemIds; ++s)
        {
            song->stems[(size_t) s].setSize (2, song->length);
            song->stems[(size_t) s].clear();
            song->present[(size_t) s] = s < 3;
            if (s < 3)
                for (int i = 0; i < song->length; ++i)
                {
                    const float v = (float) std::sin (2 * juce::MathConstants<double>::pi * (200 + 100 * s) * i / sr) * 0.2f;
                    song->stems[(size_t) s].setSample (0, i, v);
                    song->stems[(size_t) s].setSample (1, i, v);
                }
        }
        song->buildOverviews();
        player.setSong (song);
        player.play();

        std::vector<float> L (256), R (256);
        auto render = [&] (int blocks)
        {
            float pk = 0;
            for (int b = 0; b < blocks; ++b)
            {
                std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
                player.process (L.data(), R.data(), 256);
                for (auto v : L) pk = juce::jmax (pk, std::abs (v));
                if (! allFinite (L.data(), 256)) return -1.0f;
            }
            return pk;
        };

        check (render (40) > 0.3f, "Plays all stems");
        for (int s = 0; s < numStemIds; ++s) player.control ((StemId) s).mute = true;
        render (4);
        check (render (20) < 1.0e-4f, "Mute all = silence");
        for (int s = 0; s < numStemIds; ++s) player.control ((StemId) s).mute = false;
        player.control (StemId::bass).solo = true;
        render (4);
        const float soloPk = render (20);
        check (soloPk > 0.15f && soloPk < 0.25f, "Solo one stem (" + juce::String (soloPk, 3) + ")");
        player.control (StemId::bass).solo = false;

        player.seekSeconds (1.0);
        render (1);
        check (std::abs (player.getPositionSeconds() - (1.0 + 256 / sr)) < 0.01, "Seek");

        player.setLoop (true, 1.0, 1.5);
        player.seekSeconds (1.4);
        render ((int) (sr * 0.3 / 256));
        const double p = player.getPositionSeconds();
        check (p >= 1.0 && p < 1.5, "Loop wraps (pos " + juce::String (p, 3) + ")");
        player.setLoop (false, 0, 0);

        player.seekSeconds (0.0);
        player.setSpeed (0.75f);
        player.setTransposeSemitones (-2.0f);
        render (2);
        const double before = player.getPositionSeconds();
        const float pk = render ((int) (sr / 256));   // ~1 s of output
        const double advanced = player.getPositionSeconds() - before;
        check (pk > 0.05f, "Time-stretch produces audio (" + juce::String (pk, 3) + ")");
        check (std::abs (advanced - 0.75) < 0.03, "75% speed advances 0.75 s per second (" + juce::String (advanced, 3) + ")");

        // benchmark: 7 full stems with stretch
        for (int s = 0; s < numStemIds; ++s) song->present[(size_t) s] = true;
        for (int s = 3; s < numStemIds; ++s) song->stems[(size_t) s].makeCopyOf (song->stems[0]);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render ((int) (sr * 2 / 256));
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  7 stems + stretch: " << juce::String (100.0 * ms / 2000.0, 2) << "% CPU of one core" << std::endl;
        player.setSpeed (1.0f);
        player.setTransposeSemitones (0.0f);
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Pedalboard:" << std::endl;
    {
        const double psr = 48000.0;
        const int pn = (int) (psr * 1.5), pblock = 256;
        auto renderRig = [&] (RigProcessor& r, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R)
        {
            r.waitForBackgroundWork();   // rooms and reverbs are built in the background: compare like with like
            L.assign ((size_t) pn, 0.0f); R.assign ((size_t) pn, 0.0f);
            for (int pos = 0; pos < pn; pos += pblock)
                r.processMonoToStereo (in.data() + pos, L.data() + pos, R.data() + pos, juce::jmin (pblock, pn - pos));
        };
        auto keysOf = [] (RigProcessor& r) { juce::StringArray k; for (auto& it : r.getBoard()) k.add (it.isPedal ? it.builtinId : it.key); return k.joinIntoString (" "); };
        const auto guitar = bassLine (psr, pn, true);

        {
            RigProcessor r;
            check (keysOf (r) == "gate comp drive ampcab eq tape chorus delay reverb", "Standard order: " + keysOf (r));
            const auto ids = RigProcessor::pedalIds();
            check (ids.contains ("wah") && ids.contains ("octaver") && ids.contains ("stompdrive") && ids.contains ("phaser")
                   && ! ids.contains ("amprig") && ! ids.contains ("synth") && ! ids.contains ("arp"),
                   juce::String (ids.size()) + " pedals to choose from (no instruments, MIDI effects or the rig itself)");
        }

        // every pedal, in front of the amp, on a clean guitar preset
        {
            juce::StringArray bad;
            const auto ids = RigProcessor::pedalIds();
            for (auto& id : ids)
            {
                RigProcessor r;
                r.loadFactoryPreset (1);
                r.prepareToPlay (psr, pblock);
                const auto uid = r.addPedal (id);
                std::vector<float> L, R;
                renderRig (r, guitar, L, R);
                const float lvl = rmsDb (L.data(), pn);
                if (uid.isEmpty() || ! allFinite (L.data(), pn) || ! allFinite (R.data(), pn) || lvl < -60.0f || lvl > 6.0f)
                    bad.add (id + " (" + juce::String (lvl, 1) + " dB)");
            }
            check (bad.isEmpty(), juce::String (ids.size() - bad.size()) + " pedals play with sane output" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
        }

        // re-ordering changes the sound; bypass = not there
        {
            RigProcessor a, b;
            for (auto* r : { &a, &b }) { r->loadFactoryPreset (5); r->prepareToPlay (psr, pblock); }   // high gain
            a.setParam (pid::delayOn, 1); b.setParam (pid::delayOn, 1);
            // b: delay moved in front of the drive (echoes get distorted)
            auto board = b.getBoard();
            int delayAt = -1;
            for (int i = 0; i < (int) board.size(); ++i) if (board[(size_t) i].key == "delay") delayAt = i;
            b.moveBoardItem (delayAt, 0);
            check (keysOf (b).startsWith ("delay gate"), "Move the delay to the front: " + keysOf (b));
            std::vector<float> aL, aR, bL, bR;
            renderRig (a, guitar, aL, aR);
            renderRig (b, guitar, bL, bR);
            double diff = 0;
            for (int i = 0; i < pn; ++i) diff += std::abs (aL[(size_t) i] - bL[(size_t) i]);
            check (diff / pn > 1.0e-3 && allFinite (bL.data(), pn), "Delay before the amp sounds different from the effects loop (" + juce::String (diff / pn, 5) + ")");

            RigProcessor c, d;
            for (auto* r : { &c, &d }) { r->loadFactoryPreset (1); r->prepareToPlay (psr, pblock); }
            const auto wah = c.addPedal ("wah");
            c.setBoardItemOn (wah, false);
            std::vector<float> cL, cR, dL, dR;
            renderRig (c, guitar, cL, cR);
            renderRig (d, guitar, dL, dR);
            double bypassDiff = 0;
            for (int i = 0; i < pn; ++i) bypassDiff += std::abs (cL[(size_t) i] - dL[(size_t) i]);
            check (bypassDiff < 1.0e-6, "A switched-off pedal is completely out of the signal");
            c.removePedal (wah);
            check (keysOf (c) == keysOf (d), "Remove a pedal");
        }

        // presets keep the board: order, pedals, their knobs and on / off
        {
            RigProcessor r;
            r.loadFactoryPreset (0);
            const auto oct = r.addPedal ("octaver", 0);
            r.addPedal ("phaser");
            r.moveBoardItem (10, 8);   // reverb before the delay
            if (auto* p = dynamic_cast<daw::BuiltinProcessor*> (r.getPedalProcessor (oct))) p->setParam ("sub2", 0.42f);
            r.setBoardItemOn (oct, false);
            const auto saved = r.createPresetState();
            RigProcessor r2;
            r2.restorePresetState (saved);
            check (keysOf (r2) == keysOf (r), "Preset round trip keeps the order: " + keysOf (r2));
            auto b2 = r2.getBoard();
            auto* p2 = b2.empty() ? nullptr : dynamic_cast<daw::BuiltinProcessor*> (r2.getPedalProcessor (b2[0].key));
            check (p2 != nullptr && std::abs (p2->param ("sub2") - 0.42f) < 1.0e-3f && ! b2[0].on, "...and each pedal's knobs and on / off");

            auto old = saved.createCopy();
            old.removeChild (old.getChildWithName ("PEDALBOARD"), nullptr);
            r2.restorePresetState (old);
            check (keysOf (r2) == "gate comp drive ampcab eq tape chorus delay reverb", "Presets from before 3.3 load with the standard order");
            r.loadFactoryPreset (3);
            check (keysOf (r) == "gate comp drive ampcab eq tape chorus delay reverb", "Factory presets reset the board");
        }

        // the new stomp boxes do what they say
        {
            auto sine = [&] (float hz, float amp) { std::vector<float> v ((size_t) pn); for (int i = 0; i < pn; ++i) v[(size_t) i] = amp * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) psr); return v; };
            auto runFx = [&] (daw::BuiltinProcessor& fx, const std::vector<float>& in) {
                fx.setPlayConfigDetails (2, 2, psr, pblock);
                fx.prepareToPlay (psr, pblock);
                juce::AudioBuffer<float> buf (2, pn);
                for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in.data(), pn);
                juce::MidiBuffer midi;
                for (int pos = 0; pos < pn; pos += pblock)
                {
                    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, juce::jmin (pblock, pn - pos));
                    fx.processBlock (view, midi);
                }
                return std::vector<float> (buf.getReadPointer (0), buf.getReadPointer (0) + pn);
            };
            auto energyAt = [&] (const std::vector<float>& v, float hz) {
                double re = 0, im = 0;
                for (int i = pn / 2; i < pn; ++i) { const double ph = juce::MathConstants<double>::twoPi * hz * i / psr; re += v[(size_t) i] * std::cos (ph); im += v[(size_t) i] * std::sin (ph); }
                return std::sqrt (re * re + im * im) / (pn / 2);
            };

            auto oct = daw::createBuiltin ("octaver");
            oct->setParam ("dry", 0.0f); oct->setParam ("sub1", 1.0f);
            const auto o = runFx (*oct, sine (110.0f, 0.3f));
            check (allFinite (o.data(), pn) && energyAt (o, 55.0f) > 0.03 && energyAt (o, 55.0f) > 3.0 * energyAt (o, 110.0f),
                   "Octaver plays an octave down (55 Hz: " + juce::String (energyAt (o, 55.0f), 3) + ", 110 Hz: " + juce::String (energyAt (o, 110.0f), 3) + ")");

            auto wahFx = daw::createBuiltin ("wah");
            wahFx->setParam ("position", 0.0f);
            const auto heel = runFx (*wahFx, guitar);
            wahFx->setParam ("position", 1.0f);
            const auto toe = runFx (*wahFx, guitar);
            check (centroidHz (toe.data(), pn, psr) > centroidHz (heel.data(), pn, psr) * 1.5f,
                   "Wah: toe down is brighter than heel down (" + juce::String ((int) centroidHz (heel.data(), pn, psr)) + " -> " + juce::String ((int) centroidHz (toe.data(), pn, psr)) + " Hz)");

            auto boost = daw::createBuiltin ("boost");
            const auto quiet = sine (440.0f, 0.05f);
            const auto boosted = runFx (*boost, quiet);
            check (rmsDb (boosted.data(), pn) > rmsDb (quiet.data(), pn) + 6.0f, "Boost boosts (" + juce::String (rmsDb (boosted.data(), pn) - rmsDb (quiet.data(), pn), 1) + " dB)");

            auto swell = daw::createBuiltin ("swell");
            auto note = sine (220.0f, 0.3f);
            for (int i = 0; i < (int) (psr * 0.2); ++i) note[(size_t) i] = 0.0f;   // silence, then a note
            const auto sw = runFx (*swell, note);
            const int at = (int) (psr * 0.2);
            check (rmsDb (sw.data() + at, (int) (psr * 0.05)) < rmsDb (sw.data() + at + (int) (psr * 0.8), (int) (psr * 0.05)) - 12.0f,
                   "Volume Swell fades the note in");
        }
    }


    // ---------------------------------------------------------------------------------------------
    std::cout << "Audio engine (simulated 2-in/2-out interface, 128-sample buffer):" << std::endl;
    {
        struct FakeDevice : public juce::AudioIODevice
        {
            FakeDevice() : AudioIODevice ("Fake Interface", "Test") {}
            juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
            juce::StringArray getInputChannelNames() override  { return { "In 1", "In 2" }; }
            juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
            juce::Array<int> getAvailableBufferSizes() override { return { 128 }; }
            int getDefaultBufferSize() override { return 128; }
            juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
            void close() override {}
            bool isOpen() override { return true; }
            void start (juce::AudioIODeviceCallback*) override {}
            void stop() override {}
            bool isPlaying() override { return true; }
            juce::String getLastError() override { return {}; }
            int getCurrentBufferSizeSamples() override { return 128; }
            double getCurrentSampleRate() override { return 48000.0; }
            int getCurrentBitDepth() override { return 24; }
            juce::BigInteger getActiveOutputChannels() const override { return 3; }
            juce::BigInteger getActiveInputChannels() const override { return 3; }
            int getOutputLatencyInSamples() override { return 64; }
            int getInputLatencyInSamples() override { return 64; }
        };

        RigProcessor rig;
        rig.loadFactoryPreset (13);  // classic rock 8x10 bass
        StemPlayer player;
        Recorder recorder;
        AudioEngine engine (rig, player, recorder);
        FakeDevice device;
        engine.selectedInput = 1;    // instrument plugged into input 2
        engine.audioDeviceAboutToStart (&device);
        check (engine.getActiveInputNames().size() == 2, "Input channels discovered");
        check (std::abs (engine.getRoundTripLatencyMs() - 5.33) < 0.1, "Latency estimate " + juce::String (engine.getRoundTripLatencyMs(), 2) + " ms");

        PlayableSong::Ptr song = new PlayableSong();
        song->sampleRate = 48000.0;
        song->length = 48000 * 3;
        for (int s = 0; s < numStemIds; ++s)
        {
            song->stems[(size_t) s].setSize (2, song->length);
            song->stems[(size_t) s].clear();
            song->present[(size_t) s] = true;
            for (int i = 0; i < song->length; ++i)
                song->stems[(size_t) s].setSample (0, i, (float) std::sin (i * 0.01 * (s + 1)) * 0.3f),
                song->stems[(size_t) s].setSample (1, i, (float) std::sin (i * 0.01 * (s + 1)) * 0.3f);
        }
        player.setSong (song);
        player.play();

        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wis_rec_test");
        tmp.deleteRecursively();
        check (recorder.start (tmp, "test", 48000.0).isEmpty(), "Recorder starts");

        auto bassIn = pluck (48000.0, 48000 * 2, 41.2f, -14.0f);
        std::vector<float> in1 (128, 0.0f), in2 (128), out1 (128), out2 (128);
        const float* ins[] = { in1.data(), in2.data() };
        float* outs[] = { out1.data(), out2.data() };
        float maxOut = 0.0f; bool finite = true;
        for (int pos = 0; pos + 128 <= (int) bassIn.size(); pos += 128)
        {
            std::copy (bassIn.begin() + pos, bassIn.begin() + pos + 128, in2.begin());
            engine.audioDeviceIOCallbackWithContext (ins, 2, outs, 2, 128, {});
            for (int i = 0; i < 128; ++i)
            {
                maxOut = juce::jmax (maxOut, std::abs (out1[(size_t) i]), std::abs (out2[(size_t) i]));
                finite = finite && std::isfinite (out1[(size_t) i]) && std::isfinite (out2[(size_t) i]);
            }
        }
        recorder.stop();
        check (finite && maxOut > 0.1f, "Song + rig reach the outputs (peak " + juce::String (maxOut, 3) + ")");
        check (maxOut <= 1.0f, "Safety limiter keeps output <= 0 dBFS (7 loud stems + bass)");
        check (engine.readLimiterActive(), "Limiter engaged on the deliberately loud mix");

        auto files = recorder.getLastFiles();
        bool allWritten = files.size() == 3;
        for (auto& f : files) allWritten = allWritten && juce::File (f).getSize() > 48000 * 3;
        check (allWritten, "Recorder wrote Mix, Rig and DI files");
        tmp.deleteRecursively();

        // ---- oscilloscope taps ----
        ScopeFeed feed;
        engine.scope = &feed;
        auto runBlocks = [&] (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                const int pos = (b * 128) % (int) (bassIn.size() - 128);
                std::copy (bassIn.begin() + pos, bassIn.begin() + pos + 128, in2.begin());
                engine.audioDeviceIOCallbackWithContext (ins, 2, outs, 2, 128, {});
            }
        };
        auto peakOf = [] (const std::vector<float>& v) { float p = 0; for (auto x : v) p = juce::jmax (p, std::abs (x)); return p; };
        std::vector<float> sa, sb;
        std::uint64_t cursor = feed.getWritePosition();

        runBlocks (10);
        check (feed.read (cursor, sa, sb, 1 << 15) == 0, "Scope off: nothing is captured");

        feed.tap = ScopeFeed::duet;
        engine.monitorOn = false;   // direct monitoring on the interface: the scope must still see the player
        player.seekSeconds (0.0);
        player.play();
        runBlocks (40);
        const int got = feed.read (cursor, sa, sb, 1 << 15);
        check (got == 40 * 128 && peakOf (sa) > 0.01f && peakOf (sb) > 0.01f,
               "Scope duet: you on X (" + juce::String (peakOf (sa), 3) + "), the song on Y (" + juce::String (peakOf (sb), 3) + ")");

        feed.tap = ScopeFeed::instrument;
        player.pause();
        runBlocks (40);
        feed.read (cursor, sa, sb, 1 << 15);
        check (peakOf (sa) > 0.01f && peakOf (sb) > 0.01f, "Scope instrument tap works with monitoring off");
        engine.monitorOn = true;

        feed.tap = ScopeFeed::everything;
        runBlocks (1000);   // more than the ring holds: the reader skips to the newest frames
        const int kept = feed.read (cursor, sa, sb, 4096);
        check (kept == 4096 && cursor == feed.getWritePosition(), "Scope reader keeps up after an overflow");

        std::vector<float> nanIn (16, std::numeric_limits<float>::quiet_NaN());
        feed.push (nanIn.data(), nanIn.data(), 16, 48000.0);
        feed.read (cursor, sa, sb, 64);
        check (std::all_of (sa.begin(), sa.end(), [] (float x) { return x == 0.0f; }), "Scope feed drops NaNs");
        engine.scope = nullptr;
    }

    // ---------------------------------------------------------------------------------------------
    std::cout << "Real amp in a real room (3.12):" << std::endl;
    {
        const double rsr = 48000.0;
        // ---- the speaker model's impulse responses ----
        {
            bool ok = true;
            float worstDc = 0.0f, worstEarly = 0.0f;
            for (int t = 0; t < (int) CabType::impulseResponse; ++t)
                for (int m = 0; m < (int) MicType::count; ++m)
                {
                    auto ir = CabSim::designImpulseResponse ((CabType) t, (MicType) m, 0.3f, 0.1f, rsr);
                    const float* d = ir.getReadPointer (0);
                    const int len = ir.getNumSamples();
                    ok = ok && allFinite (d, len);
                    double sum = 0, total = 0, before = 0;
                    int pk = 0;
                    for (int i = 0; i < len; ++i) { sum += d[i]; total += (double) d[i] * d[i]; if (std::abs (d[i]) > std::abs (d[pk])) pk = i; }
                    for (int i = 0; i < 8; ++i) before += (double) d[i] * d[i];   // anything here wrapped round from "before time zero"
                    worstDc = juce::jmax (worstDc, (float) std::abs (sum));
                    worstEarly = juce::jmax (worstEarly, (float) (before / juce::jmax (1.0e-12, total)));
                    ok = ok && pk < (int) (rsr * 0.002);   // arrives within 2 ms (low latency)
                }
            check (ok, "Every cabinet x mic designs a finite IR that arrives within 2 ms");
            check (worstDc < 1.0e-3f, "Speakers pass no DC (worst " + juce::String (worstDc, 5) + ")");
            check (worstEarly < 0.005f, "Nothing wraps round from before the sound (worst " + juce::String (worstEarly * 100.0f, 2) + "% of the energy)");

            // a figure-8 ribbon jammed against the grille of a 1x12 is boomier than one 30 cm back (proximity effect; on a 4x12
            // the mic further back also hears the other three speakers adding up in the lows)
            auto lowShare = [&] (const juce::AudioBuffer<float>& ir)
            {
                std::vector<float> v ((size_t) 32768, 0.0f);
                for (int i = 0; i < ir.getNumSamples(); ++i) v[(size_t) i] = ir.getSample (0, i);
                juce::dsp::IIR::Filter<float> lp (juce::dsp::IIR::Coefficients<float>::makeLowPass (rsr, 200.0));
                double lo = 0, all = 0;
                for (auto x : v) { const float y = lp.processSample (x); lo += (double) y * y; all += (double) x * x; }
                return lo / juce::jmax (1.0e-12, all);
            };
            const auto nearR = CabSim::designImpulseResponse (CabType::openBack1x12, MicType::ribbon, 0.3f, 0.0f, rsr);
            const auto farR = CabSim::designImpulseResponse (CabType::openBack1x12, MicType::ribbon, 0.3f, 0.7f, rsr);
            check (lowShare (nearR) > lowShare (farR) * 1.5, "A ribbon close up is boomier than 30 cm back (proximity effect)");
            // the same settings design the same IR (cached or not)
            const auto again = CabSim::designImpulseResponse (CabType::openBack1x12, MicType::ribbon, 0.3f, 0.0f, rsr);
            bool same = again.getNumSamples() == nearR.getNumSamples();
            for (int i = 0; same && i < again.getNumSamples(); ++i) same = again.getSample (0, i) == nearR.getSample (0, i);
            check (same, "Cabinet designs are repeatable");
        }

        // ---- the rooms ----
        {
            static const float targetRt[] = { 0.55f, 0.38f, 1.0f, 1.35f, 1.75f };
            static const float micDist[] = { 2.2f, 1.4f, 3.6f, 2.0f, 6.0f };
            for (int rm = 0; rm < AmpRoom::roomNames().size(); ++rm)
            {
                auto ir = AmpRoom::designImpulseResponse (rm, CabType::brit4x12, rsr);
                const float* l = ir.getReadPointer (0);
                const float* r = ir.getReadPointer (1);
                const int len = ir.getNumSamples();
                // Schroeder decay: -5 to -25 dB, times 3
                std::vector<double> e ((size_t) len + 1, 0.0);
                for (int i = len; --i >= 0;) e[(size_t) i] = e[(size_t) i + 1] + (double) l[i] * l[i];
                int i5 = 0, i25 = 0;
                for (int i = 0; i < len; ++i) { const double db = 10.0 * std::log10 (e[(size_t) i] / e[0] + 1e-15); if (! i5 && db < -5) i5 = i; if (! i25 && db < -25) { i25 = i; break; } }
                const float rt = 3.0f * (float) (i25 - i5) / (float) rsr;
                int pk = 0;
                for (int i = 0; i < len; ++i) if (std::abs (l[i]) > std::abs (l[pk])) pk = i;
                double lr = 0, ll = 0, rr = 0;
                for (int i = pk + (int) (0.03 * rsr); i < len; ++i) { lr += (double) l[i] * r[i]; ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i]; }
                const float corr = (float) (lr / std::sqrt (ll * rr + 1e-20));
                const float expectMs = 1000.0f * std::sqrt (micDist[rm] * micDist[rm] + 0.3f * 0.3f + 1.0f) / 343.0f;
                const float arrivalMs = 1000.0f * (float) pk / (float) rsr;
                check (allFinite (l, len) && allFinite (r, len) && std::abs (rt - targetRt[rm]) < 0.25f * targetRt[rm]
                       && std::abs (corr) < 0.4f && std::abs (arrivalMs - expectMs) < 2.5f,
                       AmpRoom::roomNames()[rm] + ": RT " + juce::String (rt, 2) + " s, direct sound at " + juce::String (arrivalMs, 1)
                           + " ms, left/right correlation " + juce::String (corr, 2));
            }
        }

        // ---- a room that's still loading is silent: the raw amp (before the cab) must never leak into the room mics ----
        {
            AmpRoom room;
            room.prepare (rsr, 256);
            room.setRoom (0, CabType::brit4x12);
            for (int i = 0; i < 500 && ! room.isDesigned(); ++i) juce::Thread::sleep (10);
            std::vector<float> in ((size_t) 256, 0.5f), l ((size_t) 256), r ((size_t) 256);
            room.process (in.data(), l.data(), r.data(), 256);   // hands the room over: JUCE swaps it in later, with a crossfade
            float mx = 0.0f;
            for (int i = 0; i < 256; ++i) mx = juce::jmax (mx, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
            check (room.isDesigned() && mx < 1.0e-6f, "A room that's still loading is silent (no raw amp through the room mics)");
        }

        // ---- the room mic in the rig: stereo, and no louder or quieter ----
        {
            const int n = (int) rsr * 4;
            auto in = strum (rsr, n, false, -8.0f);
            auto render = [&] (float roomMix, std::vector<float>& L, std::vector<float>& R)
            {
                RigProcessor rig;
                rig.loadFactoryPreset (2);
                rig.setParam (pid::reverbOn, 0.0f);
                rig.setParam (pid::cabRoomMic, roomMix);
                rig.prepareToPlay (rsr, 256);
                rig.waitForBackgroundWork();
                L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
                for (int pos = 0; pos < n; pos += 256)
                    rig.processMonoToStereo (in.data() + pos, L.data() + pos, R.data() + pos, juce::jmin (256, n - pos));
            };
            std::vector<float> l0, r0, l1, r1;
            render (0.0f, l0, r0);
            render (0.5f, l1, r1);
            double diff0 = 0, diff1 = 0;
            for (int i = 0; i < n; ++i) { diff0 += std::abs (l0[(size_t) i] - r0[(size_t) i]); diff1 += std::abs (l1[(size_t) i] - r1[(size_t) i]); }
            check (diff0 == 0.0 && diff1 / n > 1.0e-4, "Room mics make the amp stereo (only the close mic: mono)");
            const float a = loudnessDb (l0.data() + n / 4, n - n / 4, rsr), b = loudnessDb (l1.data() + n / 4, n - n / 4, rsr);
            check (allFinite (l1.data(), n) && allFinite (r1.data(), n) && std::abs (a - b) < 1.5f,
                   "Room mic at half keeps the level (" + juce::String (a, 1) + " -> " + juce::String (b, 1) + " LU)");
        }

        // ---- the guitar cable ----
        {
            GuitarCable c;
            c.prepare (rsr);
            c.setLength (0.0f);
            std::vector<float> x ((size_t) 512);
            juce::Random rng (3);
            for (auto& v : x) v = rng.nextFloat() - 0.5f;
            auto y = x;
            c.process (y.data(), (int) y.size());
            check (y == x, "Cable 0 m: the signal as recorded, bit for bit");
            c.setLength (6.0f);
            const double at200 = c.magnitudeAt (200.0), at3k = c.magnitudeAt (3000.0), at8k = c.magnitudeAt (8000.0);
            check (std::abs (at200 - 1.0) < 0.02 && at3k > 1.1 && at8k < 0.6,
                   "A 6 m cable: lows untouched, the pickup peak moves down (3 kHz x" + juce::String (at3k, 2) + "), the top rolls off (8 kHz x" + juce::String (at8k, 2) + ")");
        }

        // ---- the reverb spaces ----
        {
            bool ok = true;
            juce::String info;
            for (int t = 1; t <= 4; ++t)
            {
                auto ir = SpaceReverb::designImpulseResponse (t, 0.5f, 0.5f, 10.0f, rsr);
                ok = ok && allFinite (ir.getReadPointer (0), ir.getNumSamples()) && ir.getNumChannels() == 2 && ir.getNumSamples() > (int) (0.2 * rsr);
                info << SpaceReverb::typeNames()[t] << " " << juce::String (ir.getNumSamples() / rsr, 1) << " s  ";
            }
            check (ok, "Reverb spaces: " + info.trim());

            const int n = (int) rsr * 3;
            std::vector<float> in ((size_t) n, 0.0f);
            for (int i = 0; i < (int) rsr / 4; ++i) in[(size_t) i] = 0.3f * std::sin (0.05f * (float) i);
            RigProcessor rig;
            rig.loadFactoryPreset (1);
            rig.setParam (pid::reverbType, 3.0f);   // spring
            rig.setParam (pid::reverbMix, 0.5f);
            rig.prepareToPlay (rsr, 256);
            rig.waitForBackgroundWork();
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int pos = 0; pos < n; pos += 256)
                rig.processMonoToStereo (in.data() + pos, L.data() + pos, R.data() + pos, juce::jmin (256, n - pos));
            const float tail = rmsDb (L.data() + (int) (rsr * 0.6), (int) (rsr * 0.4));
            check (allFinite (L.data(), n) && tail > -80.0f, "The spring reverb rings on after the note (" + juce::String (tail, 1) + " dB)");
        }

        // ---- the output transformer: low notes saturate it, quiet ones pass ----
        {
            BuiltInAmp a;
            a.prepare (rsr, 512);
            a.setParameters (AmpType::tweedBreakup, 2.0f, 5, 5, 5, 5, 7);
            auto gainAt = [&] (float level)
            {
                const int n = (int) rsr;
                std::vector<float> s ((size_t) n);
                for (int i = 0; i < n; ++i) s[(size_t) i] = level * std::sin (juce::MathConstants<float>::twoPi * 55.0f * (float) i / (float) rsr);
                for (int pos = 0; pos < n; pos += 512) a.process (s.data() + pos, juce::jmin (512, n - pos));
                return rmsDb (s.data() + n / 2, n / 2) - juce::Decibels::gainToDecibels (level / std::sqrt (2.0f));
            };
            a.reset();
            const float quiet = gainAt (0.01f);
            a.reset();
            const float loud = gainAt (0.8f);
            check (loud < quiet - 3.0f, "A small tweed amp squashes big low notes (" + juce::String (quiet - loud, 1) + " dB less gain)");
        }

        // ---- new settings survive a preset round trip ----
        {
            RigProcessor rig;
            rig.loadFactoryPreset (2);
            rig.setParam (pid::inputCable, 7.5f);
            rig.setParam (pid::cabRoomMic, 0.42f);
            rig.setParam (pid::cabRoomType, 3.0f);
            rig.setParam (pid::reverbType, 2.0f);
            auto st = rig.createPresetState();
            RigProcessor other;
            other.restorePresetState (st);
            auto v = [&] (const char* id) { return other.getState().getRawParameterValue (id)->load(); };
            check (std::abs (v (pid::inputCable) - 7.5f) < 0.06f && std::abs (v (pid::cabRoomMic) - 0.42f) < 0.002f
                   && (int) v (pid::cabRoomType) == 3 && (int) v (pid::reverbType) == 2, "Cable, room mic, room and reverb type are saved");
            // a rig saved before 3.12 (no such settings) comes back with them off: old songs keep their sound
            auto old = rig.getState().copyState();
            for (auto id : { pid::inputCable, pid::cabRoomMic, pid::cabRoomType, pid::reverbType })
                old.removeChild (old.getChildWithProperty ("id", juce::String (id)), nullptr);
            RigProcessor fresh;
            fresh.restorePresetState (old);
            auto f = [&] (const char* id) { return fresh.getState().getRawParameterValue (id)->load(); };
            check (f (pid::inputCable) == 0.0f && f (pid::cabRoomMic) == 0.0f && (int) f (pid::reverbType) == 0,
                   "A rig saved by an older version: no cable, no room mic, the classic reverb");
        }
    }

    std::cout << (failures == 0 ? "ALL RIG TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
