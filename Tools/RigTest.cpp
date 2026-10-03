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
#include "Engine/Recorder.h"
#include <iostream>

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

int main()
{
    const double sr = 48000.0;
    const int block = 128;
    const int seconds = 3;
    const int n = (int) sr * seconds;

    // ---------------------------------------------------------------------------------------------
    std::cout << "Amp models (pluck at -18 dBFS peak-ish, E2):" << std::endl;
    auto input = pluck (sr, n, 82.41f, -12.0f);
    std::cout << "  input RMS " << rmsDb (input.data(), n) << " dB" << std::endl;

    for (int m = 0; m < (int) AmpType::namCapture; ++m)
    {
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
        cab.setParameters ((CabType) c, 60, 10000);
        std::vector<float> buf ((size_t) n);
        juce::Random rng (1);
        for (auto& s : buf) s = (rng.nextFloat() * 2 - 1) * 0.25f;
        for (int pos = 0; pos < n; pos += block)
            cab.process (buf.data() + pos, juce::jmin (block, n - pos));
        const float lvl = rmsDb (buf.data(), n);
        check (allFinite (buf.data(), n) && lvl > -40 && lvl < 0, cabTypeNames()[c] + ": " + juce::String (lvl, 1) + " dB RMS (white noise in)");
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
            const bool bass = names[pi].startsWith ("Bass");
            auto in = pluck (sr, n, bass ? 41.2f : 164.8f, -14.0f);

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos < n; pos += block)
            {
                const int k = juce::jmin (block, n - pos);
                rig.processMonoToStereo (in.data() + pos, L.data() + pos, R.data() + pos, k);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const float lvl = rmsDb (L.data() + n / 3, n - n / 3);
            const double cpu = 100.0 * ms / (1000.0 * seconds);
            check (allFinite (L.data(), n) && allFinite (R.data(), n) && lvl > -40 && lvl < 0,
                   names[pi] + ": " + juce::String (lvl, 1) + " dB RMS, CPU " + juce::String (cpu, 2) + "% of one core");
        }

        // preset save/load round trip
        rig.loadFactoryPreset (3);
        auto saved = rig.createPresetState();
        rig.loadFactoryPreset (0);
        rig.restorePresetState (saved);
        check ((int) rig.getState().getRawParameterValue (pid::ampModel)->load() == (int) AmpType::hotLead, "Preset state round trip");
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
        rig.loadFactoryPreset (6);   // bass vintage tube
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
    }

    std::cout << (failures == 0 ? "ALL RIG TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
