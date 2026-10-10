// dawtest - automated tests for the Studio (DAW) engine. Exit code 0 = all passed.
// Set WIS_SOUNDFONT to a .sf2 path to also test the Sound Library instrument.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Daw/Model/Project.h"
#include "Daw/Model/DrumPatterns.h"
#include "Daw/Model/TempoDetect.h"
#include "Daw/Engine/DawEngine.h"
#include "Daw/Plugins/PluginHost.h"
#include "Daw/Plugins/BuiltinEffects.h"
#include "Daw/Plugins/TestEffects.h"
#include "Daw/Plugins/Looper.h"
#include "Daw/Instruments/Sampler.h"
#include "Daw/Instruments/HomeKeys.h"
#include "Daw/Instruments/BeatLab.h"
#include "Daw/Instruments/VintageRhythms.h"
#include "Daw/Instruments/DrumSynth.h"
#include "Daw/Model/MidiLoops.h"
#include <tuple>
#include "Daw/Instruments/InstrumentRefs.h"
#include "Daw/Instruments/SoundFontInstrument.h"
#include "Daw/Instruments/PianoRoom.h"
#include "Daw/Instruments/RoomIr.h"
#include "Daw/Instruments/Junkyard.h"
#include "Daw/Instruments/Whistler.h"
#include "Daw/Model/JunkLoops.h"
#include "Daw/Model/VocalChains.h"
#include <iostream>

using namespace wis::daw;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static constexpr double sr = 48000.0;
static constexpr int block = 256;

/** Renders [fromBeat, toBeat) offline through the engine's normal block path. */
static juce::AudioBuffer<float> renderRange (DawEngine& e, Project& p, double fromBeat, double toBeat)
{
    e.rebuildNow();
    const double spb = sr * 60.0 / p.tempo();
    const int total = (int) ((toBeat - fromBeat) * spb);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    e.setPositionBeats (fromBeat);
    e.play();
    float* ptrs[2];
    for (int done = 0; done < total; done += block)
    {
        const int n = juce::jmin (block, total - done);
        ptrs[0] = out.getWritePointer (0, done);
        ptrs[1] = out.getWritePointer (1, done);
        e.renderBlock (nullptr, 0, ptrs, 2, n);
    }
    e.stop();
    return out;
}

static int firstSampleAbove (const juce::AudioBuffer<float>& b, float thresh, int from = 0)
{
    for (int i = from; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (0, i)) > thresh || std::abs (b.getSample (1, i)) > thresh)
            return i;
    return -1;
}

static float rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    to = juce::jmin (to, b.getNumSamples());
    if (to <= from) return 0.0f;
    return b.getRMSLevel (0, from, to - from);
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

static juce::File writeTestWav (const juce::File& f, double seconds, double toneStart)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    b.clear();
    for (int i = (int) (toneStart * sr); i < b.getNumSamples(); ++i)
        b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr));
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24);
    if (auto w = juce::WavAudioFormat().createWriterFor (os, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

/** Mono WAV: silence with a single-sample click (0.8) at `clickAt` seconds, optionally a sine from `toneFrom`. */
static juce::File writeClickWav (const juce::File& f, double seconds, double clickAt, double toneHz = 0.0, double toneFrom = 0.0)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    b.clear();
    if (clickAt >= 0.0) b.setSample (0, (int) std::llround (clickAt * sr), 0.3f);
    if (toneHz > 0.0)
        for (int i = (int) (toneFrom * sr); i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * toneHz * i / sr));
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24);
    if (auto w = juce::WavAudioFormat().createWriterFor (os, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

static int peakIndex (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    int best = -1; float bv = 0.0f;
    for (int i = from; i < to; ++i) { const float v = std::abs (b.getSample (0, i)); if (v > bv) { bv = v; best = i; } }
    return best;
}

/** Frequency from zero crossings (mono sine-ish signals). */
static double zeroCrossingHz (const juce::AudioBuffer<float>& b, int from, int to)
{
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f) { if (first < 0) first = i; last = i; ++crossings; }
    return crossings > 1 ? (crossings - 1) * sr / (double) (last - first) : 0.0;
}

static juce::ValueTree addPoint (juce::ValueTree lane, double beat, double value)
{
    juce::ValueTree pt (ids::POINT);
    pt.setProperty (ids::b, beat, nullptr);
    pt.setProperty (ids::v, value, nullptr);
    lane.appendChild (pt, nullptr);
    return pt;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // dawtest --tempo <audio file>: prints the detected tempo (handy for tuning the detector on real songs)
    if (argc > 2 && juce::String (argv[1]) == "--tempo")
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (juce::String (juce::CharPointer_UTF8 (argv[2])))));
        if (r == nullptr) { std::cout << "can't read file" << std::endl; return 1; }
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        auto est = estimateTempo (b, r->sampleRate);
        std::cout << "bpm " << est.bpm << "  first beat " << est.firstBeatSeconds << " s  confidence " << est.confidence << std::endl;
        return 0;
    }
    // dawtest --render-homekeys <preset> <out.wav>: plays a slow chord progression on a HomeKeys 20 preset (for listening)
    if (argc > 3 && juce::String (argv[1]) == "--render-homekeys")
    {
        HomeKeys k;
        k.setCurrentProgram (juce::String (argv[2]).getIntValue());
        k.setParam ("rhythmOn", 0.0f);
        k.setParam ("abc", 0.0f);
        k.prepareToPlay (sr, 512);
        // dawtest --render-homekeys <preset> <out.wav> rhythm [<rhythm> <variation> <tempo>]: with the rhythm section running
        if (argc > 4 && juce::String (argv[4]) == "rhythm")
        {
            if (argc > 7)
            {
                k.setParam ("rhythm", (float) juce::String (argv[5]).getIntValue());
                k.setParam ("variation", (float) juce::String (argv[6]).getIntValue());
                k.setParam ("tempo", (float) juce::String (argv[7]).getDoubleValue());
            }
            k.startStop();
        }
        // slow chords, two bars each, with a little top line: D, Bm, G, A
        const int chords[4][4] = { { 50, 57, 62, 66 }, { 47, 54, 62, 66 }, { 43, 55, 59, 62 }, { 45, 57, 61, 64 } };
        const int tops[8] = { 69, 71, 74, 71, 67, 66, 64, 61 };
        const double chordLen = 3.2;
        juce::MidiBuffer all;
        auto at = [] (double s) { return (int) (s * sr); };
        for (int c = 0; c < 4; ++c)
        {
            for (int nn : chords[c])
            {
                all.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 90), at (c * chordLen));
                all.addEvent (juce::MidiMessage::noteOff (1, nn), at ((c + 1) * chordLen - 0.08));
            }
            for (int h = 0; h < 2; ++h)
            {
                all.addEvent (juce::MidiMessage::noteOn (1, tops[c * 2 + h], (juce::uint8) 100), at (c * chordLen + h * chordLen * 0.5 + 0.02));
                all.addEvent (juce::MidiMessage::noteOff (1, tops[c * 2 + h]), at (c * chordLen + (h + 1) * chordLen * 0.5 - 0.1));
            }
        }
        const int total = at (4 * chordLen + 3.0);
        juce::AudioBuffer<float> out (2, total);
        for (int pos = 0; pos < total; pos += 512)
        {
            const int n = std::min (512, total - pos);
            juce::AudioBuffer<float> blockBuf (out.getArrayOfWritePointers(), 2, pos, n);
            blockBuf.clear();
            juce::MidiBuffer mb;
            for (const auto meta : all)
                if (meta.samplePosition >= pos && meta.samplePosition < pos + n)
                    mb.addEvent (meta.getMessage(), meta.samplePosition - pos);
            k.processBlock (blockBuf, mb);
        }
        const juce::File f { juce::String (juce::CharPointer_UTF8 (argv[3])) };
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
        if (auto w = juce::WavAudioFormat().createWriterFor (os, opts))
            w->writeFromAudioSampleBuffer (out, 0, total);
        std::cout << k.getProgramName (k.getCurrentProgram()) << ": peak " << out.getMagnitude (0, total)
                  << "  rms " << juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, total)) << " dB" << std::endl;
        return 0;
    }
    // dawtest --render-junkyard <folder>: Junkyard Percussion, its loops and sound spaces, and the Whistler (for listening)
    if (argc > 2 && juce::String (argv[1]) == "--render-junkyard")
    {
        const juce::File dir { juce::String (juce::CharPointer_UTF8 (argv[2])) };
        dir.createDirectory();
        struct Ev { int note; double on, off; int vel; };
        auto render = [] (BuiltinProcessor& p, const std::vector<Ev>& evs, double seconds, const juce::File& f)
        {
            p.setPlayConfigDetails (0, 2, sr, 512);
            p.setNonRealtime (true);
            p.prepareToPlay (sr, 512);
            if (auto* j = dynamic_cast<Junkyard*> (&p)) j->waitUntilReady();
            juce::MidiBuffer all;
            for (auto& e : evs)
            {
                all.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.vel), (int) (e.on * sr));
                all.addEvent (juce::MidiMessage::noteOff (1, e.note), (int) (e.off * sr));
            }
            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (2, total);
            for (int pos = 0; pos < total; pos += 512)
            {
                const int n = std::min (512, total - pos);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
                view.clear();
                juce::MidiBuffer mb;
                for (const auto meta : all)
                    if (meta.samplePosition >= pos && meta.samplePosition < pos + n) mb.addEvent (meta.getMessage(), meta.samplePosition - pos);
                p.processBlock (view, mb);
            }
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (16);
            if (auto w = juce::WavAudioFormat().createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (out, 0, total);
            std::cout << f.getFileName() << ": peak " << out.getMagnitude (0, total) << "  rms "
                      << juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, total)) << " dB" << std::endl;
        };
        auto loopEvents = [] (int index, double bpm, int bars)
        {
            Project p;
            auto t = p.addTrack (kindInstrument, "x");
            auto c = insertJunkLoop (p, t, index, 0.0, bars);
            std::vector<Ev> evs;
            for (auto n : c.v)
            {
                const double s0 = (double) n[ids::s] * 60.0 / bpm, len = (double) n[ids::l] * 60.0 / bpm;
                evs.push_back ({ (int) n[ids::p], s0, s0 + len, (int) n[ids::v] });
            }
            return evs;
        };
        auto findLoop = [] (const juce::String& name) { for (int i = 0; i < (int) junkLoops().size(); ++i) if (junkLoops()[(size_t) i].name == name) return i; return 0; };

        {
            Junkyard j;
            j.setCurrentProgram (0);
            std::vector<Ev> tour;
            double t = 0.2;
            for (int key = Junkyard::kitLow; key <= Junkyard::kitHigh; ++key)
            {
                if (Junkyard::kitKeyName (key).isEmpty()) continue;
                const bool held = key >= Junkyard::bowedSaw;
                tour.push_back ({ key, t, t + (held ? 2.5 : 0.3), 100 });
                t += held ? 3.2 : 0.7;
            }
            render (j, tour, t + 3.0, dir.getChildFile ("01 Junk Kit tour (Junkyard preset).wav"));
        }
        for (auto [name, bpm] : { std::pair<const char*, double> { "Snaps on 2 & 4", 100.0 }, { "Snaps on 2 & 4", 150.0 }, { "Jazz Club Snaps (swing)", 132.0 },
                                  { "Gospel Stomp & Clap", 88.0 }, { "Bone Machine Stomp", 92.0 }, { "Swordfish March", 104.0 }, { "Clanking Machinery", 116.0 },
                                  { "Haunted Basement", 80.0 }, { "Storm Shelter", 80.0 }, { "Underwater Cave", 80.0 }, { "Ghost Ship", 80.0 } })
        {
            const int li = findLoop (name);
            Junkyard j;
            j.setCurrentProgram (junkLoops()[(size_t) li].preset);
            const double seconds = 8 * 4 * 60.0 / bpm + 3.0;
            render (j, loopEvents (li, bpm, 8), seconds, dir.getChildFile ("02 " + juce::String (name).replace ("&", "and") + " @ " + juce::String ((int) bpm) + " bpm.wav"));
        }
        for (auto [preset, notes] : { std::pair<int, std::vector<int>> { 9, { 57, 60, 64, 67, 69, 67, 64, 60 } }, { 15, { 33, 40, 45, 43, 41, 40, 36, 38 } },
                                      { 19, { 64, 67, 71, 69, 67 } }, { 16, { 48, 55, 60, 55, 48, 43 } } })
        {
            Junkyard j;
            j.setCurrentProgram (preset);
            std::vector<Ev> evs;
            const bool slow = preset == 19;
            double t = 0.2;
            for (int n : notes) { evs.push_back ({ n, t, t + (slow ? 1.4 : 0.4), 96 }); t += slow ? 1.5 : 0.45; }
            render (j, evs, t + 3.0, dir.getChildFile ("03 " + juce::File::createLegalFileName (j.getProgramName (preset)) + ".wav"));
        }
        for (int preset : { 0, 5, 3 })
        {
            Whistler w;
            w.setCurrentProgram (preset);
            // a little tune: legato phrases (overlapping notes glide), then a gap and a new phrase
            const std::vector<std::tuple<int, double, double>> tune { { 67, 0.2, 0.75 }, { 72, 0.7, 1.25 }, { 74, 1.2, 1.75 }, { 76, 1.7, 2.6 },
                                                                     { 74, 2.9, 3.35 }, { 72, 3.3, 3.75 }, { 69, 3.7, 4.2 }, { 67, 4.15, 5.2 },
                                                                     { 72, 5.8, 6.6 }, { 79, 6.55, 7.6 } };
            std::vector<Ev> evs;
            for (auto [n, on, off] : tune) evs.push_back ({ n, on, off, 100 });
            render (w, evs, 10.0, dir.getChildFile ("04 Whistler - " + juce::File::createLegalFileName (w.getProgramName (preset)) + ".wav"));
        }
        return 0;
    }
    // dawtest --make-demo <file.wisproj>: builds a demo song (used for the README screenshots)
    if (argc > 2 && juce::String (argv[1]) == "--make-demo")
    {
        const juce::File target { juce::String (juce::CharPointer_UTF8 (argv[2])) };
        Project p;
        p.createNew ("Dream Demo");
        target.getParentDirectory().createDirectory();
        p.saveAs (target);
        p.setTempo (84.0);
        p.setKey (2, 0);   // D major
        const double bar = p.beatsPerBar();

        auto keys = p.addTrack (kindInstrument, "HomeKeys 20");
        {
            auto ref = builtinPresetRef ("homekeys", 0);
            if (auto proto = createBuiltin ("homekeys")) { proto->setCurrentProgram (0); proto->setParam ("rhythmOn", 0.0f); ref.state = encodeState (*proto); }
            p.setInstrument (keys, ref);
        }
        insertMidiLoop (p, keys, 1, 3, 0.0);
        insertMidiLoop (p, keys, 1, 3, 8 * bar);

        auto drums = p.addTrack (kindInstrument, "Rhythm Box");
        p.setInstrument (drums, builtinPresetRef ("rhythmbox", 0));
        insertVintageRhythm (p, drums, 0, 4 * bar, 12);

        auto bass = p.addTrack (kindInstrument, "Bass");
        p.setInstrument (bass, soundFontRef (0, 33));
        insertMidiLoop (p, bass, 1, 4, 4 * bar);
        insertMidiLoop (p, bass, 1, 4, 12 * bar);

        auto arp = p.addTrack (kindInstrument, "Glass Arp");
        p.setInstrument (arp, synthRef (8));
        p.setPlugin (arp.midiFx(), -1, builtinPresetRef ("arp", 2));
        p.setPlugin (arp.inserts(), -1, builtinRef ("autofilter"));
        p.setPlugin (arp.inserts(), -1, builtinPresetRef ("tape", 0));
        insertMidiLoop (p, arp, 1, 0, 8 * bar);
        {
            auto slot = arp.inserts().getChild (0)[ids::id].toString();
            if (auto proto = createBuiltin ("autofilter"))
            {
                int cutoffIndex = 0;
                auto params = proto->getParameters();
                for (int i = 0; i < params.size(); ++i)
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[i]); r != nullptr && r->paramID == "cutoff") cutoffIndex = i;
                auto lane = p.getOrCreateLane (arp, "plug:" + slot + ":" + juce::String (cutoffIndex));
                addPoint (lane, 8 * bar, 0.15); addPoint (lane, 12 * bar, 0.75); addPoint (lane, 14 * bar, 0.55); addPoint (lane, 16 * bar, 0.9);
                arp.v.setProperty ("autoParam", lane[ids::param], nullptr);
                arp.v.setProperty (ids::showAutomation, true, nullptr);
            }
        }

        // a "vocal": a wobbly sine melody so there's a waveform to look at
        auto vox = p.addTrack (kindAudio, "Vocals");
        {
            const double secs = 8 * bar * 60.0 / 84.0;
            juce::AudioBuffer<float> b (1, (int) (sr * secs));
            const int melody[] = { 66, 69, 71, 69, 66, 64, 62, 64 };
            double ph = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const int note = melody[(int) (t / (secs / 8.0)) % 8];
                const double hz = 440.0 * std::pow (2.0, (note - 69 + 0.3 * std::sin (t * 5.5)) / 12.0);
                ph += 2.0 * juce::MathConstants<double>::pi * hz / sr;
                const double phrase = std::fmod (t, secs / 8.0) / (secs / 8.0);
                const double env = std::sin (juce::MathConstants<double>::pi * phrase) * (0.6 + 0.4 * std::sin (t * 1.7));
                b.setSample (0, i, (float) (0.35 * env * (std::sin (ph) + 0.3 * std::sin (2 * ph) + 0.1 * std::sin (3 * ph))));
            }
            auto f = p.audioFolder().getChildFile ("Vocals take 1.wav");
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24);
            if (auto w = juce::WavAudioFormat().createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            p.addAudioClip (vox, f, 8 * bar, 0.0, secs, "Vocals take 1");
        }
        for (auto id : { "autotune", "deesser", "compressor" }) p.setPlugin (vox.inserts(), -1, builtinRef (id));

        auto bus = p.addBus ("Shimmer Bus");
        {
            auto ref = builtinRef ("shimmer");
            if (auto proto = createBuiltin ("shimmer")) { proto->setParam ("mix", 1.0f); ref.state = encodeState (*proto); }
            p.setPlugin (bus.inserts(), -1, ref);
        }
        p.setSend (keys, bus.id(), -8.0f);
        p.setSend (arp, bus.id(), -4.0f);
        p.setSend (vox, bus.id(), -6.0f);
        bus.v.setProperty (ids::volume, -3.0, nullptr);

        p.addMarker (0.0, "Intro");
        p.addMarker (4 * bar, "Verse");
        p.addMarker (8 * bar, "Chorus");
        p.addMarker (12 * bar, "Verse 2");
        p.setPlugin (p.masterInserts(), -1, builtinRef ("limiter"));
        auto err = p.save();
        std::cout << (err.isEmpty() ? "Demo written to " + target.getFullPathName() : err) << std::endl;
        return err.isEmpty() ? 0 : 1;
    }

    auto tmpRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wis_dawtest");
    tmpRoot.deleteRecursively();
    tmpRoot.createDirectory();

    PluginHost host;
    Project project;
    project.createNew ("Test Song");
    // move the project into the temp folder so tests don't touch Documents
    {
        auto f = tmpRoot.getChildFile ("Test Song").getChildFile ("Test Song.wisproj");
        f.getParentDirectory().createDirectory();
        project.saveAs (f);
    }
    DawEngine engine (project, host);
    engine.prepareOffline (sr, block);

    // -------------------------------------------------------------------------------------------
    std::cout << "Model:" << std::endl;
    {
        auto t = project.addTrack (kindInstrument, "Keys");
        project.setInstrument (t, synthRef (0));
        auto c = project.addMidiClip (t, 4.0, 8.0);
        project.addNote (c, 60, 0.0, 1.0, 100);
        project.addNote (c, 64, 2.0, 1.0, 100);
        project.addNote (c, 67, 5.0, 2.0, 100);
        check (project.numTracks() == 1 && c.v.getNumChildren() == 3, "Add track, clip and notes");

        auto right = project.splitClip (c, 8.0);
        check (right.isValid() && std::abs (c.midiLength() - 4.0) < 1e-9 && std::abs (right.start() - 8.0) < 1e-9
               && right.v.getNumChildren() == 1 && std::abs ((double) right.v.getChild (0)[ids::s] - 1.0) < 1e-9,
               "Split MIDI clip at bar 3");

        project.undo().beginNewTransaction();
        project.deleteClip (right);
        check (t.clips().getNumChildren() == 1, "Delete clip");
        project.undo().undo();
        check (t.clips().getNumChildren() == 2, "Undo restores the clip");
        project.undo().redo();
        check (t.clips().getNumChildren() == 1, "Redo deletes it again");

        auto dup = project.duplicateClip (c, 12.0);
        check (dup.isValid() && dup.id() != c.id() && dup.v.getNumChildren() == c.v.getNumChildren(), "Duplicate clip");

        project.quantize (dup, 1.0);
        check (true, "Quantize runs");

        auto err = project.save();
        Project p2;
        auto err2 = p2.load (project.getFile());
        check (err.isEmpty() && err2.isEmpty() && p2.numTracks() == 1 && p2.track (0).clips().getNumChildren() == 2,
               "Save and reload project (" + project.getFile().getFileName() + ")");

        project.removeTrack (t);
        project.undo().clearUndoHistory();
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Instrument playback timing (Studio Synth, 120 bpm):" << std::endl;
    {
        project.setTempo (120.0);
        auto t = project.addTrack (kindInstrument, "Synth");
        project.setInstrument (t, synthRef (5));   // pluck: fast attack
        auto c = project.addMidiClip (t, 4.0, 4.0);   // bar 2
        project.addNote (c, 60, 0.0, 0.5, 110);
        engine.selectedTrackId = 0;

        auto out = renderRange (engine, project, 0.0, 10.0);
        const int onset = firstSampleAbove (out, 0.01f);
        check (allFinite (out), "Output is finite");
        check (onset > 0 && std::abs (onset - (int) (2.0 * sr)) < 64,
               "Note at beat 4 sounds at 2.000 s (got " + juce::String (onset / sr, 4) + " s)");
        check (rms (out, 0, (int) (1.9 * sr)) < 1.0e-4f, "Silence before the note");

        // mute / solo
        t.v.setProperty (ids::mute, true, nullptr);
        auto muted = renderRange (engine, project, 4.0, 6.0);
        check (muted.getMagnitude (0, muted.getNumSamples()) < 1.0e-5f, "Muted track is silent");
        t.v.setProperty (ids::mute, false, nullptr);

        // volume automation: -60 dB on beat 4 -> nearly silent
        auto lane = project.getOrCreateLane (t, "volume");
        for (auto [b, v] : { std::pair<double, double> { 0.0, -60.0 }, { 100.0, -60.0 } })
        {
            juce::ValueTree pt (ids::POINT);
            pt.setProperty (ids::b, b, nullptr);
            pt.setProperty (ids::v, v, nullptr);
            lane.appendChild (pt, nullptr);
        }
        auto quiet = renderRange (engine, project, 3.5, 6.0);
        check (quiet.getMagnitude (0, quiet.getNumSamples()) < 0.01f, "Volume automation applies");
        t.automation().removeAllChildren (nullptr);

        // oscilloscope taps: the selected track (after its instrument, before the fader) and the master
        {
            wis::ScopeFeed feed;
            engine.scope = &feed;
            std::uint64_t cursor = feed.getWritePosition();
            std::vector<float> sa, sb;
            auto peakOf = [] (const std::vector<float>& v) { float p = 0; for (auto x : v) p = juce::jmax (p, std::abs (x)); return p; };

            renderRange (engine, project, 3.5, 5.0);
            check (feed.read (cursor, sa, sb, 1 << 15) == 0, "Scope off: nothing captured");

            feed.tap = wis::ScopeFeed::studioMaster;
            renderRange (engine, project, 3.5, 5.0);
            feed.read (cursor, sa, sb, 1 << 15);
            check (peakOf (sa) > 0.01f && peakOf (sb) > 0.01f, "Scope sees the master (" + juce::String (peakOf (sa), 3) + ")");

            feed.tap = wis::ScopeFeed::studioDuet;
            engine.selectedTrackId = t.id();
            t.v.setProperty (ids::mute, true, nullptr);
            renderRange (engine, project, 3.5, 5.0);
            feed.read (cursor, sa, sb, 1 << 15);
            check (peakOf (sa) > 0.01f && peakOf (sb) < 1.0e-5f,
                   "Scope duet: the selected track on X even when muted (" + juce::String (peakOf (sa), 3) + "), silent master on Y");
            t.v.setProperty (ids::mute, false, nullptr);

            feed.tap = wis::ScopeFeed::studioTrack;
            engine.selectedTrackId = 0;
            renderRange (engine, project, 3.5, 5.0);
            feed.read (cursor, sa, sb, 1 << 15);
            check (peakOf (sa) < 1.0e-6f, "Scope track tap is silent when another track is selected");
            engine.scope = nullptr;
        }
        project.removeTrack (t);
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Audio clips:" << std::endl;
    {
        project.setTempo (120.0);
        auto wav = writeTestWav (project.audioFolder().getChildFile ("tone.wav"), 3.0, 0.5);
        auto t = project.addTrack (kindAudio, "Audio");
        // clip at beat 2 (1.0 s); the file's tone starts at 0.5 s -> heard at 1.5 s
        auto c = project.addAudioClip (t, wav, 2.0, 0.0, 3.0);
        engine.getCache().getBlocking (wav, sr);
        auto out = renderRange (engine, project, 0.0, 10.0);
        const int onset = firstSampleAbove (out, 0.05f);
        check (std::abs (onset - (int) (1.5 * sr)) < 48, "Clip placed at beat 2 plays its audio at 1.5 s (got " + juce::String (onset / sr, 4) + ")");

        // trim the start by 1 beat (0.5 s): tone now starts right at the clip start (1.5 s)
        project.trimClipStart (c, 3.0);
        auto out2 = renderRange (engine, project, 0.0, 10.0);
        const int onset2 = firstSampleAbove (out2, 0.05f);
        check (std::abs (c.offsetSeconds() - 0.5) < 1e-6 && std::abs (onset2 - (int) (1.5 * sr)) < 48, "Trim start keeps audio in place");

        // clip gain
        c.v.setProperty (ids::gain, -6.0, nullptr);
        auto out3 = renderRange (engine, project, 3.0, 6.0);
        const float ratio = out3.getMagnitude (0, out3.getNumSamples()) / out2.getMagnitude (0, out2.getNumSamples());
        check (std::abs (ratio - 0.501f) < 0.03f, "Clip gain -6 dB (ratio " + juce::String (ratio, 3) + ")");

        // cycle playback wraps
        project.tree().setProperty (ids::cycleOn, true, nullptr);
        project.tree().setProperty (ids::cycleStart, 0.0, nullptr);
        project.tree().setProperty (ids::cycleEnd, 4.0, nullptr);
        renderRange (engine, project, 3.0, 6.0);   // 3 beats from beat 3 -> wraps to beat 2
        check (std::abs (engine.getPositionBeats() - 2.0) < 0.05, "Cycle wraps (position " + juce::String (engine.getPositionBeats(), 3) + " beats)");
        project.tree().setProperty (ids::cycleOn, false, nullptr);
        project.removeTrack (t);
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Recording:" << std::endl;
    {
        project.setTempo (120.0);
        project.tree().setProperty (ids::countIn, false, nullptr);
        auto audioTrack = project.addTrack (kindAudio, "Vocal");
        auto midiTrack = project.addTrack (kindInstrument, "Piano");
        project.setInstrument (midiTrack, synthRef (0));
        audioTrack.v.setProperty (ids::arm, true, nullptr);
        midiTrack.v.setProperty (ids::arm, true, nullptr);
        engine.rebuildNow();

        engine.setPositionBeats (4.0);
        engine.record();
        // simulate an interface: input 0 carries a click at 0.25 s into the take; a key is pressed at 0.5 s
        std::vector<float> inBuf (block, 0.0f), o0 (block), o1 (block);
        const float* ins[] = { inBuf.data() };
        float* outs[] = { o0.data(), o1.data() };
        const int totalBlocks = (int) (sr * 1.5 / block);
        for (int b = 0; b < totalBlocks; ++b)
        {
            std::fill (inBuf.begin(), inBuf.end(), 0.0f);
            const int clickAt = (int) (0.25 * sr);
            if (clickAt >= b * block && clickAt < (b + 1) * block) inBuf[(size_t) (clickAt - b * block)] = 0.9f;
            if (b == (int) (0.5 * sr / block)) engine.keyboardState.noteOn (1, 62, 0.8f);
            if (b == (int) (1.0 * sr / block)) engine.keyboardState.noteOff (1, 62, 0.0f);
            engine.renderBlock (ins, 1, outs, 2, block);
        }
        engine.stop();

        check (audioTrack.clips().getNumChildren() == 1, "Audio take created");
        if (audioTrack.clips().getNumChildren() == 1)
        {
            Clip c (audioTrack.clips().getChild (0));
            check (std::abs (c.start() - 4.0) < 1e-6, "Take starts where recording started (beat " + juce::String (c.start(), 3) + ")");
            auto data = engine.getCache().getBlocking (project.resolve (c.file()), sr);
            int peakAt = -1; float peak = 0;
            if (data != nullptr)
                for (int i = 0; i < data->buffer.getNumSamples(); ++i)
                    if (std::abs (data->buffer.getSample (0, i)) > peak) { peak = std::abs (data->buffer.getSample (0, i)); peakAt = i; }
            check (data != nullptr && std::abs (peakAt - (int) (0.25 * sr)) < 2, "Recorded audio is sample-accurate (click at " + juce::String (peakAt) + ")");
        }
        check (midiTrack.clips().getNumChildren() == 1, "MIDI take created");
        if (midiTrack.clips().getNumChildren() == 1)
        {
            Clip c (midiTrack.clips().getChild (0));
            juce::ValueTree note;
            for (auto n : c.v) if (n.hasType (ids::NOTE)) note = n;
            const double abs = c.start() + (double) note[ids::s];
            check (note.isValid() && (int) note[ids::p] == 62 && std::abs (abs - 5.0) < 0.02 && std::abs ((double) note[ids::l] - 1.0) < 0.02,
                   "Recorded note D4 at beat " + juce::String (abs, 3) + ", length " + juce::String ((double) note[ids::l], 3));
        }
        project.removeTrack (audioTrack);
        project.removeTrack (midiTrack);
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Built-in effects:" << std::endl;
    {
        for (auto& info : builtinPlugins())
        {
            if (info.instrument) continue;
            auto p = info.create();
            p->setRateAndBufferSizeDetails (sr, block);
            p->prepareToPlay (sr, block);
            for (int prog = 0; prog < p->getNumPrograms(); ++prog) p->setCurrentProgram (prog);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer m;
            juce::Random rng (3);
            float outPeak = 0;
            bool finite = true;
            for (int i = 0; i < 200; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < block; ++s) b.setSample (ch, s, (rng.nextFloat() - 0.5f) * 0.4f);
                p->processBlock (b, m);
                outPeak = juce::jmax (outPeak, b.getMagnitude (0, block));
                finite = finite && allFinite (b);
            }
            // state round trip
            juce::MemoryBlock st;
            p->getStateInformation (st);
            auto p2 = info.create();
            p2->setStateInformation (st.getData(), (int) st.getSize());
            check (finite && outPeak > 0.001f && outPeak < 8.0f, info.name + " (peak " + juce::String (outPeak, 2) + ")");
        }
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Sound Library:" << std::endl;
    {
        auto sf = SoundFontCache::defaultSoundFont();
        if (! sf.existsAsFile())
        {
            std::cout << "  (skipped: set WIS_SOUNDFONT to GeneralUser-GS.sf2)" << std::endl;
        }
        else
        {
            auto presets = SoundFontCache::get().presetsFor (sf);
            check (presets.size() > 128, juce::String (presets.size()) + " presets in " + sf.getFileName());
            for (auto [bank, prog, note] : { std::tuple<int, int, int> { 0, 0, 60 }, { 0, 33, 40 }, { 0, 48, 60 }, { 128, 0, 38 } })
            {
                auto t = project.addTrack (kindInstrument, "SF");
                project.setInstrument (t, soundFontRef (bank, prog));
                auto c = project.addMidiClip (t, 0.0, 4.0);
                project.addNote (c, note, 0.0, 1.0, 110);
                auto out = renderRange (engine, project, 0.0, 3.0);
                const float pk = out.getMagnitude (0, out.getNumSamples());
                check (allFinite (out) && pk > 0.01f, (bank == 128 ? juce::String ("Drum kit snare") : gmProgramName (prog)) + " plays (peak " + juce::String (pk, 2) + ")");
                project.removeTrack (t);
            }
        }
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Drum patterns, MIDI files, export:" << std::endl;
    {
        project.setTempo (100.0);
        auto drums = project.addTrack (kindInstrument, "Drums");
        project.setInstrument (drums, synthRef (5));
        int totalNotes = 0;
        for (int i = 0; i < drumPatterns().size(); ++i)
        {
            auto c = insertDrumPattern (project, drums, i, i * 16.0, 4, true);
            totalNotes += c.v.getNumChildren();
        }
        check (drums.clips().getNumChildren() == drumPatterns().size() && totalNotes > 400,
               juce::String (drumPatterns().size()) + " drum patterns, " + juce::String (totalNotes) + " notes");

        auto mid = tmpRoot.getChildFile ("song.mid");
        auto e1 = project.exportMidiFile (mid);
        Project p3;
        auto e2 = p3.importMidiFile (mid, 0.0, true);
        int imported = 0;
        for (auto tv : p3.tracks()) for (auto cv : Track (tv).clips()) for (auto n : cv) imported += n.hasType (ids::NOTE) ? 1 : 0;
        check (e1.isEmpty() && e2.isEmpty() && imported == totalNotes && std::abs (p3.tempo() - 100.0) < 0.01,
               "MIDI file export + import round trip (" + juce::String (imported) + " notes, " + juce::String (p3.tempo(), 1) + " bpm)");

        // master insert + export
        project.setPlugin (project.masterInserts(), -1, builtinRef ("limiter"));
        DawEngine::ExportOptions o;
        o.file = tmpRoot.getChildFile ("mix.wav");
        o.startBeat = 0; o.endBeat = 16;
        o.bitDepth = 24;
        o.tailSeconds = 1.0;
        auto err = engine.exportAudio (o, [] (float) { return true; });
        std::unique_ptr<juce::AudioFormatReader> r (juce::WavAudioFormat().createReaderFor (o.file.createInputStream().release(), true));
        const double expected = 16 * 60.0 / 100.0 + 1.0;
        check (err.isEmpty() && r != nullptr && std::abs ((double) r->lengthInSamples / r->sampleRate - expected) < 0.01,
               "Export WAV (" + (r != nullptr ? juce::String ((double) r->lengthInSamples / r->sampleRate, 2) : juce::String ("-")) + " s)");
        if (r != nullptr)
        {
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
            const float pk = b.getMagnitude (0, b.getNumSamples());
            check (pk > 0.05f && pk <= 1.0f, "Exported mix has signal (peak " + juce::String (pk, 2) + ")");
        }

        o.file = tmpRoot.getChildFile ("mix.flac");
        o.format = 1;
        o.sampleRate = 44100.0;
        auto err2 = engine.exportAudio (o, [] (float) { return true; });
        check (err2.isEmpty() && o.file.getSize() > 1000, "Export FLAC at 44.1 kHz");
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Buses, sends and output routing:" << std::endl;
    {
        project.setTempo (120.0);
        auto clickFile = writeClickWav (tmpRoot.getChildFile ("click.wav"), 2.0, 0.5);
        while (project.numTracks() > 0) project.removeTrack (project.track (0));   // start clean
        project.masterInserts().removeAllChildren (nullptr);
        auto a = project.addTrack (kindAudio, "Source");
        project.addAudioClip (a, clickFile, 0.0, 0.0, 2.0);
        auto bus = project.addBus ("Reverb Bus");
        engine.preloadAudio();

        auto dry = renderRange (engine, project, 0.0, 4.0);
        const float dryPeak = dry.getMagnitude (0, dry.getNumSamples());
        project.setSend (a, bus.id(), 0.0f);
        auto wet = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (wet.getMagnitude (0, wet.getNumSamples()) - 2.0f * dryPeak) < 0.01f && peakIndex (wet) == peakIndex (dry),
               "Post-fader send at 0 dB doubles the signal through the bus (" + juce::String (dryPeak, 3) + " -> " + juce::String (wet.getMagnitude (0, wet.getNumSamples()), 3)
               + ", at " + juce::String (peakIndex (dry)) + "/" + juce::String (peakIndex (wet)) + ")");

        bus.v.setProperty (ids::mute, true, nullptr);
        auto busMuted = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (busMuted.getMagnitude (0, busMuted.getNumSamples()) - dryPeak) < 0.01f, "Muting the bus removes the send");
        bus.v.setProperty (ids::mute, false, nullptr);

        // pre-fader send keeps going when the track fader is down
        project.setSend (a, bus.id(), 0.0f, true);
        a.v.setProperty (ids::volume, -96.0, nullptr);
        auto pre = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (pre.getMagnitude (0, pre.getNumSamples()) - dryPeak) < 0.01f, "Pre-fader send ignores the track fader");
        a.v.setProperty (ids::volume, 0.0, nullptr);
        project.removeSend (a, bus.id());

        // route the track's output into the bus, bus at -6 dB
        project.setOutput (a, bus.id());
        bus.v.setProperty (ids::volume, -6.0206, nullptr);
        auto routed = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (routed.getMagnitude (0, routed.getNumSamples()) - 0.5f * dryPeak) < 0.01f, "Track output routed through a bus (-6 dB): " + juce::String (routed.getMagnitude (0, routed.getNumSamples()), 3));
        check (! project.wouldCreateLoop (a.id(), bus.id()) && project.wouldCreateLoop (bus.id(), bus.id()), "Feedback loops are refused");

        // solo the source: the bus it feeds stays audible
        a.v.setProperty (ids::solo, true, nullptr);
        auto soloed = renderRange (engine, project, 0.0, 4.0);
        check (soloed.getMagnitude (0, soloed.getNumSamples()) > 0.4f * dryPeak, "Soloing a track keeps its bus audible");
        a.v.setProperty (ids::solo, false, nullptr);
        bus.v.setProperty (ids::volume, 0.0, nullptr);
        project.setOutput (a, 0);

        // ---- plugin delay compensation ----
        auto b = project.addTrack (kindAudio, "Delayed");
        project.addAudioClip (b, clickFile, 0.0, 0.0, 2.0);
        project.setPlugin (b.inserts(), -1, builtinRef ("testlatency"));
        engine.rebuildNow();
        check (engine.getTrackLatency (b.id()) == LatencyTestFx::latencySamples, "Track latency is measured (" + juce::String (engine.getTrackLatency (b.id())) + " samples)");
        a.v.setProperty (ids::mute, true, nullptr);
        auto pdc = renderRange (engine, project, 0.0, 4.0);
        check (peakIndex (pdc) == (int) (0.5 * sr), "Latency is compensated: the delayed track still clicks at 0.500 s (got "
                                                     + juce::String (peakIndex (pdc)) + ")");
        a.v.setProperty (ids::mute, false, nullptr);
        auto both = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (both.getMagnitude (0, both.getNumSamples()) - 2.0f * dryPeak) < 0.01f, "Compensated and plain tracks line up sample-accurately: " + juce::String (both.getMagnitude (0, both.getNumSamples()), 3));

        // a plain track sending to a bus that has latency: the direct path is delayed to match
        project.setPlugin (bus.inserts(), -1, builtinRef ("testlatency"));
        project.setSend (a, bus.id(), 0.0f);
        b.v.setProperty (ids::mute, true, nullptr);
        auto viaBus = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (viaBus.getMagnitude (0, viaBus.getNumSamples()) - 2.0f * dryPeak) < 0.01f && peakIndex (viaBus) == (int) (0.5 * sr),
               "Send through a bus with latency stays aligned with the direct sound: " + juce::String (viaBus.getMagnitude (0, viaBus.getNumSamples()), 3) + " at " + juce::String (peakIndex (viaBus)));
        project.removeSend (a, bus.id());
        bus.inserts().removeAllChildren (nullptr);
        b.v.setProperty (ids::mute, false, nullptr);

        // ---- plugin parameter automation: the test plugin's gain is automated to 0, then to 1 ----
        auto slotId = b.inserts().getChild (0)[ids::id].toString();
        auto lane = project.getOrCreateLane (b, "plug:" + slotId + ":0");
        addPoint (lane, 0.0, 0.0); addPoint (lane, 100.0, 0.0);
        a.v.setProperty (ids::mute, true, nullptr);
        auto autoOff = renderRange (engine, project, 0.0, 4.0);
        check (autoOff.getMagnitude (0, autoOff.getNumSamples()) < 0.01f, "Plugin parameter automation turns the plugin's gain down");
        lane.getChild (0).setProperty (ids::v, 1.0, nullptr);
        lane.getChild (1).setProperty (ids::v, 1.0, nullptr);
        auto autoOn = renderRange (engine, project, 0.0, 4.0);
        check (std::abs (autoOn.getMagnitude (0, autoOn.getNumSamples()) - dryPeak) < 0.01f, "...and back up");
        b.automation().removeAllChildren (nullptr);

        project.removeTrack (a);
        project.removeTrack (b);
        project.removeTrack (bus);
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Time stretch, pitch and reverse:" << std::endl;
    {
        project.setTempo (120.0);
        auto f = writeClickWav (tmpRoot.getChildFile ("stretch.wav"), 3.0, 1.0);
        auto t = project.addTrack (kindAudio, "Stretch");
        auto c = project.addAudioClip (t, f, 0.0, 0.0, 3.0);
        c.v.setProperty (ids::stretch, 2.0, nullptr);
        engine.preloadAudio();
        auto st = renderRange (engine, project, 0.0, 14.0);
        const int pk = peakIndex (st);
        check (std::abs (c.lengthBeats (120.0) - 12.0) < 1.0e-6, "Stretch x2 doubles the clip's length (12 beats)");
        check (std::abs (pk - (int) (2.0 * sr)) < 300, "Stretched x2: the click moves from 1.0 s to 2.0 s (got " + juce::String (pk / sr, 3) + ")");

        c.v.setProperty (ids::stretch, 1.0, nullptr);
        c.v.setProperty (ids::reverse, true, nullptr);
        engine.preloadAudio();
        auto rv = renderRange (engine, project, 0.0, 8.0);
        check (std::abs (peakIndex (rv) - (int) (2.0 * sr)) < 4, "Reversed: the click at 1.0 s of 3.0 s plays at 2.0 s (" + juce::String (peakIndex (rv)) + ")");
        c.v.setProperty (ids::reverse, false, nullptr);

        // follow tempo: recorded at 120, song at 60 -> twice as long
        project.setClipFollowTempo (c, 120.0, true);
        project.setTempo (60.0);
        engine.preloadAudio();
        auto fl = renderRange (engine, project, 0.0, 7.0);
        check (std::abs (c.lengthBeats (60.0) - 6.0) < 1.0e-6 && std::abs (peakIndex (fl) - (int) (2.0 * sr)) < 300,
               "Follow tempo: half the tempo plays the audio at half speed (click at " + juce::String (peakIndex (fl) / sr, 3) + " s)");
        project.setTempo (120.0);
        project.setClipFollowTempo (c, 0.0, false);

        // pitch: a 440 Hz tone up an octave
        auto tone = writeClickWav (tmpRoot.getChildFile ("tone.wav"), 2.0, -1.0, 440.0, 0.0);
        c.v.setProperty (ids::file, project.makeRef (tone), nullptr);
        c.v.setProperty (ids::length, 2.0, nullptr);
        c.v.setProperty (ids::pitch, 12.0, nullptr);
        engine.preloadAudio();
        auto up = renderRange (engine, project, 0.0, 4.0);
        const double hz = zeroCrossingHz (up, (int) (0.4 * sr), (int) (1.6 * sr));
        check (std::abs (hz - 880.0) < 5.0, "Transpose +12 semitones: 440 Hz -> " + juce::String (hz, 1) + " Hz, same length");

        // split / trim respect the stretch
        c.v.setProperty (ids::pitch, 0.0, nullptr);
        c.v.setProperty (ids::stretch, 2.0, nullptr);
        auto right = project.splitClip (c, 4.0);
        check (right.isValid() && std::abs (c.lengthSeconds() - 1.0) < 1.0e-9 && std::abs (right.offsetSeconds() - 1.0) < 1.0e-9,
               "Splitting a stretched clip splits its source audio correctly");
        project.removeTrack (t);
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Instruments, MIDI effects, side-chain, Vocal Tune, Looper:" << std::endl;
    {
        project.setTempo (120.0);
        while (project.numTracks() > 0) project.removeTrack (project.track (0));
        project.masterInserts().removeAllChildren (nullptr);

        // ---- Sampler: a C4 sine sample plays at the right pitch across the keyboard ----
        auto t = project.addTrack (kindInstrument, "Sampler");
        project.setInstrument (t, builtinRef ("sampler"));
        auto c = project.addMidiClip (t, 0.0, 16.0);
        project.addNote (c, 60, 0.0, 1.0, 100);
        project.addNote (c, 69, 1.0, 1.0, 100);
        engine.rebuildNow();
        auto* sampler = dynamic_cast<Sampler*> (engine.getProcessor (t.instrument()[ids::id].toString()));
        check (sampler != nullptr, "Sampler loads");
        if (sampler != nullptr)
        {
            juce::AudioBuffer<float> sine (2, (int) (sr * 2.0));
            for (int i = 0; i < sine.getNumSamples(); ++i)
                sine.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.6256 * i / sr));
            sine.copyFrom (1, 0, sine, 0, 0, sine.getNumSamples());
            sampler->loadBuffer (sine, sr, "C4 sine");
            sampler->setParam ("release", 1.0f);
            sampler->setParam ("velsens", 0.0f);
            auto out = renderRange (engine, project, 0.0, 2.0);
            const double h1 = zeroCrossingHz (out, (int) (0.05 * sr), (int) (0.45 * sr));
            const double h2 = zeroCrossingHz (out, (int) (0.55 * sr), (int) (0.95 * sr));
            check (std::abs (h1 - 261.63) < 1.5 && std::abs (h2 - 440.0) < 2.0,
                   "Sampler plays C4 and A4 from a C4 sample (" + juce::String (h1, 1) + " / " + juce::String (h2, 1) + " Hz)");

            // a sample loaded from a file is saved with the song and comes back when it's reopened
            auto sampleFile = writeClickWav (tmpRoot.getChildFile ("sample.wav"), 1.0, 0.1, 330.0, 0.2);
            check (sampler->loadFile (sampleFile).isEmpty() && sampler->getSample() != nullptr, "Sampler loads a WAV file");
            engine.flushPluginStates();
            {
                juce::String err;
                auto fresh = host.create (t.instrument(), sr, block, true, err);
                auto* restored = dynamic_cast<Sampler*> (fresh.get());
                check (restored != nullptr && restored->getSample() != nullptr && restored->getSample()->length() == (int) sr,
                       "...and gets it back from the saved song");
            }
            sampler->loadBuffer (sine, sr, "C4 sine");

            sampler->setParam ("mode", 2.0f);
            sampler->setParam ("slices", 2.0f);   // 8 equal slices
            const auto slices = sampler->currentSlices();
            check (slices.size() >= 8 && slices.size() <= 9, "Slice mode chops the sample into 8 (" + juce::String ((int) slices.size()) + " boundaries)");
            sampler->setParam ("mode", 0.0f);

            // ---- Arpeggiator in the track's MIDI FX slot: a held C major chord -> C, E, G, C... in 8ths ----
            c.v.removeAllChildren (nullptr);
            for (int n : { 60, 64, 67 }) project.addNote (c, n, 4.0, 4.0, 100);
            project.setPlugin (t.midiFx(), -1, builtinRef ("arp"));
            engine.rebuildNow();
            if (auto* arp = engine.getProcessor (t.midiFx().getChild (0)[ids::id].toString()))
            {
                auto* bp = dynamic_cast<BuiltinProcessor*> (arp);
                bp->setParam ("rate", 3.0f);   // 1/8 = 0.25 s at 120 bpm
                bp->setParam ("gate", 0.5f);
            }
            auto arp = renderRange (engine, project, 0.0, 8.0);
            const double expect[] = { 261.63, 329.63, 392.0, 261.63 };
            juce::String got; bool ok = true;
            for (int k = 0; k < 4; ++k)
            {
                const double at = 2.0 + 0.25 * k;
                const double hz = zeroCrossingHz (arp, (int) ((at + 0.02) * sr), (int) ((at + 0.11) * sr));
                ok = ok && std::abs (hz - expect[k]) < 4.0;
                got << juce::String (hz, 0) << " ";
            }
            check (ok, "Arpeggiator plays the chord as 1/8 notes, in time (" + got.trim() + " Hz)");
            check (rms (arp, (int) (2.16 * sr), (int) (2.24 * sr)) < 0.01f, "...with gaps from the gate length");
            check (rms (arp, (int) (4.3 * sr), (int) (7.9 * sr)) < 1.0e-4f, "...and stops when the chord is released");
        }
        project.removeTrack (t);

        // ---- Rhythm Box + the vintage rhythms ----
        int totalNotes = 0, rhythmsWithNotes = 0;
        auto rt = project.addTrack (kindInstrument, "Rhythm");
        project.setInstrument (rt, builtinRef ("rhythmbox"));
        for (int i = 0; i < (int) vintageRhythms().size(); ++i)
        {
            auto clip = insertVintageRhythm (project, rt, i, 64.0 + i * 32.0, 4);
            totalNotes += clip.v.getNumChildren();
            if (clip.v.getNumChildren() > 8) ++rhythmsWithNotes;
        }
        check (vintageRhythms().size() == 20 && rhythmsWithNotes == 20, "All 20 vintage rhythms write drum clips (" + juce::String (totalNotes) + " hits)");
        auto drums = renderRange (engine, project, 64.0, 72.0);
        check (allFinite (drums) && rms (drums, 0, drums.getNumSamples()) > 0.01f, "Rhythm Box plays the Slow Rock pattern (rms "
               + juce::String (rms (drums, 0, drums.getNumSamples()), 3) + ")");
        project.removeTrack (rt);

        // ---- Drum Pads: synth kit by default ----
        auto dp = project.addTrack (kindInstrument, "Pads");
        project.setInstrument (dp, builtinRef ("drumpads"));
        auto dc = project.addMidiClip (dp, 0.0, 4.0);
        for (int k = 0; k < 16; ++k) project.addNote (dc, 36 + k, k * 0.25, 0.2, 110);
        auto pads = renderRange (engine, project, 0.0, 4.0);
        check (allFinite (pads) && rms (pads, 0, pads.getNumSamples()) > 0.01f, "Drum Pads play all 16 pads");
        project.removeTrack (dp);

        // ---- HomeKeys 20: plays a voice, and its rhythm section runs with the song ----
        auto hk = project.addTrack (kindInstrument, "HomeKeys");
        project.setInstrument (hk, builtinRef ("homekeys"));
        engine.rebuildNow();
        auto* keys = dynamic_cast<HomeKeys*> (engine.getProcessor (hk.instrument()[ids::id].toString()));
        check (keys != nullptr, "HomeKeys 20 loads");
        if (keys != nullptr)
        {
            keys->setParam ("rhythmOn", 0.0f);
            auto hc = project.addMidiClip (hk, 0.0, 8.0);
            project.addNote (hc, 69, 0.0, 2.0, 100);
            auto voice = renderRange (engine, project, 0.0, 2.0);
            check (allFinite (voice) && rms (voice, (int) (0.1 * sr), (int) (0.9 * sr)) > 0.01f, "HomeKeys plays a note");

            // the digital organs (tones 17, 18): in tune, and dry when the effects are off
            for (int organ : { 16, 17 })
            {
                keys->setParam ("tone", (float) organ);
                for (auto* id : { "ensemble", "vibrato", "vintage", "wobble", "reverb", "drive" }) keys->setParam (id, 0.0f);
                auto o = renderRange (engine, project, 0.0, 2.0);
                // autocorrelation pitch (the stepped square-wave footages fool a zero-crossing count)
                const int from = (int) (0.4 * sr), len = (int) (0.4 * sr);
                const float* x = o.getReadPointer (0);
                int bestLag = 0; double best = -1.0;
                for (int lag = (int) (sr / 600.0); lag <= (int) (sr / 300.0); ++lag)
                {
                    double c = 0.0;
                    for (int i = 0; i < len; ++i) c += (double) x[from + i] * x[from + i + lag];
                    if (c > best) { best = c; bestLag = lag; }
                }
                const double hz = bestLag > 0 ? sr / bestLag : 0.0;
                check (allFinite (o) && rms (o, from, from + len) > 0.02f && std::abs (hz - 440.0) < 6.0,
                       HomeKeys::toneNames()[organ] + " plays A4 in tune (" + juce::String (hz, 1) + " Hz)");
            }
            // the Teen Dream preset: wobble, amp and a reverb that rings on after the note
            keys->setCurrentProgram (0);
            keys->setParam ("rhythmOn", 0.0f);
            hc.v.removeAllChildren (nullptr);
            project.addNote (hc, 69, 0.0, 1.0, 100);
            auto dream = renderRange (engine, project, 0.0, 3.0);
            const double spb = 60.0 / project.tempo();
            const int noteEnd = (int) (1.0 * spb * sr);
            check (allFinite (dream) && rms (dream, (int) (0.1 * sr), noteEnd) > 0.02f && rms (dream, noteEnd + (int) (0.3 * sr), noteEnd + (int) (0.6 * sr)) > 0.003f,
                   "Teen Dream Organ preset plays with a reverb tail");
            hc.v.removeAllChildren (nullptr);
            keys->setParam ("rhythmOn", 1.0f);
            keys->setParam ("abc", 1.0f);   // single finger
            project.addNote (hc, 43, 0.0, 8.0, 100);   // G below the split point
            auto rhythm = renderRange (engine, project, 0.0, 8.0);
            check (allFinite (rhythm) && rms (rhythm, 0, rhythm.getNumSamples()) > 0.01f && keys->currentStep.load() >= 0, "Rhythm section runs with the song");
            check (keys->chordRoot.load() == 7, "Auto accompaniment recognises the single-finger chord (" + HomeKeys::chordName (keys->chordRoot.load(), keys->chordType.load()) + ")");
            // the Portable '81 rhythm bank: every rhythm, both variations, with its own drum sound
            {
                keys->setParam ("bank", 1.0f);
                keys->setParam ("kit", (float) DrumSynth::numKits);
                keys->setParam ("autoFill", 1.0f);
                keys->setParam ("abc", 0.0f);
                bool allPlay = true;
                juce::String quiet;
                for (int r = 0; r < 8; ++r)
                    for (int v = 0; v < 2; ++v)
                    {
                        keys->setParam ("rhythm", (float) r);
                        keys->setParam ("variation", (float) v);
                        auto beat = renderRange (engine, project, 0.0, 4.0);
                        if (! allFinite (beat) || rms (beat, 0, beat.getNumSamples()) < 0.005f) { allPlay = false; quiet << keys->currentRhythmName() << " "; }
                    }
                check (allPlay && portableRhythms().size() == 16, "Portable '81 rhythms: all 8 rhythms x 2 variations play " + quiet);
                keys->setParam ("bank", 0.0f);
                keys->setParam ("kit", 0.0f);
                keys->setParam ("autoFill", 0.0f);
            }
        }
        project.removeTrack (hk);

        // ---- side-chain: a gate on a pad opens only when a (muted) key track plays ----
        auto padFile = writeClickWav (tmpRoot.getChildFile ("pad.wav"), 4.0, -1.0, 440.0, 0.0);
        auto keyFile = writeClickWav (tmpRoot.getChildFile ("key.wav"), 4.0, -1.0, 200.0, 2.0);
        auto padT = project.addTrack (kindAudio, "Pad");
        project.addAudioClip (padT, padFile, 0.0, 0.0, 4.0);
        auto keyT = project.addTrack (kindAudio, "Key");
        project.addAudioClip (keyT, keyFile, 0.0, 0.0, 4.0);
        keyT.v.setProperty (ids::mute, true, nullptr);
        project.setPlugin (padT.inserts(), -1, builtinRef ("gate"));
        padT.inserts().getChild (0).setProperty (ids::sidechain, keyT.id(), nullptr);
        engine.preloadAudio();
        auto gated = renderRange (engine, project, 0.0, 8.0);
        check (rms (gated, (int) (0.3 * sr), (int) (1.9 * sr)) < 0.001f && rms (gated, (int) (2.3 * sr), (int) (3.8 * sr)) > 0.2f,
               "Side-chained gate opens only when the key track plays (" + juce::String (rms (gated, (int) (0.3 * sr), (int) (1.9 * sr)), 4)
               + " / " + juce::String (rms (gated, (int) (2.3 * sr), (int) (3.8 * sr)), 3) + ")");
        project.removeTrack (padT);
        project.removeTrack (keyT);

        // ---- Vocal Tune: a flat A (430 Hz) is pulled up to 440 Hz ----
        auto flatFile = writeClickWav (tmpRoot.getChildFile ("flat.wav"), 4.0, -1.0, 430.0, 0.0);
        auto vt = project.addTrack (kindAudio, "Vocal");
        project.addAudioClip (vt, flatFile, 0.0, 0.0, 4.0);
        project.setPlugin (vt.inserts(), -1, builtinRef ("autotune"));
        engine.preloadAudio();
        engine.rebuildNow();
        if (auto* tune = dynamic_cast<BuiltinProcessor*> (engine.getProcessor (vt.inserts().getChild (0)[ids::id].toString())))
        {
            tune->setParam ("key", 1.0f);      // C
            tune->setParam ("scale", 13.0f);   // chromatic
            tune->setParam ("speed", 0.0f);
            tune->setParam ("range", 3.0f);    // instrument
        }
        auto tuned = renderRange (engine, project, 0.0, 8.0);
        const double tunedHz = zeroCrossingHz (tuned, (int) (1.0 * sr), (int) (3.0 * sr));
        check (std::abs (tunedHz - 440.0) < 2.0, "Vocal Tune corrects 430 Hz to " + juce::String (tunedHz, 1) + " Hz");
        check (engine.getTrackLatency (vt.id()) > 0, "...and reports its latency (" + juce::String (engine.getTrackLatency (vt.id())) + " samples, compensated)");
        project.removeTrack (vt);
        engine.rebuildNow();

        // ---- Loop Station (driven directly) ----
        Looper looper;
        looper.prepareToPlay (sr, block);
        juce::AudioBuffer<float> io (2, block);
        juce::MidiBuffer none;
        double ph = 0.0;
        auto feed = [&] (double seconds, bool tone)
        {
            juce::AudioBuffer<float> all (2, (int) (seconds * sr));
            for (int done = 0; done < all.getNumSamples(); done += block)
            {
                const int n = juce::jmin (block, all.getNumSamples() - done);
                io.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const float v = tone ? 0.4f * (float) std::sin (ph) : 0.0f;
                    ph += 2.0 * juce::MathConstants<double>::pi * 440.0 / sr;
                    io.setSample (0, i, v); io.setSample (1, i, v);
                }
                looper.processBlock (io, none);
                all.copyFrom (0, done, io, 0, 0, n); all.copyFrom (1, done, io, 1, 0, n);
            }
            return all;
        };
        looper.pressMain(); feed (0.5, true);
        looper.pressMain();
        auto played = feed (1.0, false);
        check (std::abs (looper.getLoopSeconds() - 0.5) < 0.01 && looper.getState() == Looper::playing, "Looper records a 0.5 s loop (" + juce::String (looper.getLoopSeconds(), 3) + " s)");
        check (std::abs (zeroCrossingHz (played, (int) (0.05 * sr), (int) (0.95 * sr)) - 440.0) < 3.0 && rms (played, 0, played.getNumSamples()) > 0.2f, "...and plays it back on repeat");
        looper.pressMain(); feed (0.5, true); looper.pressMain(); feed (0.01, false);
        check (looper.getLayers() == 2, "Overdub adds a layer");
        looper.pressUndo(); feed (0.01, false);
        check (looper.getLayers() == 1 && looper.canRedo(), "Undo removes it (redo available)");
        auto loopFile = tmpRoot.getChildFile ("loop.wav");
        check (looper.exportLoop (loopFile).isEmpty() && loopFile.getSize() > 1000, "Loop exports to a WAV file");
        looper.pressClear(); feed (0.01, false);
        check (looper.getState() == Looper::empty, "Clear empties it");
    }


    // -------------------------------------------------------------------------------------------
    std::cout << "Beat Lab:" << std::endl;
    {
        struct Head : juce::AudioPlayHead
        {
            double ppq = 0.0, bpm = 120.0; bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setIsPlaying (playing); p.setBpm (bpm); p.setPpqPosition (ppq);
                p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                return p;
            }
        } head;
        // renders `seconds` of the Beat Lab, following the host clock (or not)
        auto run = [&] (BeatLab& bl, double seconds)
        {
            juce::AudioBuffer<float> out (2, (int) (seconds * sr));
            out.clear();
            head.ppq = 0.0;
            juce::AudioBuffer<float> buf (2, block);
            for (int pos = 0; pos + block <= out.getNumSamples(); pos += block)
            {
                juce::MidiBuffer midi;
                buf.clear();
                bl.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, block);
                if (head.playing) head.ppq += block / sr * head.bpm / 60.0;
            }
            return out;
        };
        // onsets: first sample above the threshold after 20 ms of quiet
        auto onsets = [] (const juce::AudioBuffer<float>& b, float thr)
        {
            std::vector<double> t;
            int quiet = 100000;
            bool armed = true;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float a = std::abs (b.getSample (0, i));
                if (quiet > (int) (0.02 * sr)) armed = true;
                if (a > thr && armed) { t.push_back (i / sr); armed = false; }
                quiet = a > thr * 0.3f ? 0 : quiet + 1;
            }
            return t;
        };
        auto fresh = [&]
        {
            auto bl = std::make_unique<BeatLab>();
            bl->setPlayHead (&head);
            bl->setCurrentProgram (BeatLab::presetNames().size() - 1);   // Empty Kit
            for (int l = 0; l < BeatLab::numLanes; ++l) bl->setParam (BeatLab::laneParam (l, "mute"), l == 0 ? 0.0f : 1.0f);
            bl->setParam (BeatLab::laneParam (0, "kit"), (float) BeatLab::glitchKit);
            bl->setParam (BeatLab::laneParam (0, "voice"), 2.0f);   // Click: short and sharp
            bl->setParam (BeatLab::laneParam (0, "vol"), 0.0f);
            bl->rebuildSoundsNow();
            bl->prepareToPlay (sr, block);
            return bl;
        };
        auto on = [] (BeatLab& bl, int s, int ratchet = 1, int prob = 100)
        {
            BeatLab::Step st; st.on = true; st.velocity = 127; st.ratchet = ratchet; st.probability = prob;
            bl.setStep (0, 0, s, st);
        };

        {
            auto bl = fresh();
            on (*bl, 0); on (*bl, 8);
            const auto t = onsets (run (*bl, 4.0), 0.05f);
            bool good = t.size() == 4;
            for (size_t i = 0; good && i < t.size(); ++i) good = std::abs (t[i] - 1.0 * (double) i) < 0.003;
            juce::String got; for (auto x : t) got << juce::String (x, 3) << " ";
            check (good, "Steps 1 and 9 play on beats 1 and 3, in sync with the song (" + got.trim() + " s)");
        }
        {
            auto bl = fresh();
            on (*bl, 4, 4);
            const auto t = onsets (run (*bl, 2.0), 0.05f);
            juce::String got; for (auto x : t) got << juce::String (x, 4) << " ";
            const bool good = t.size() == 4 && std::abs (t[0] - 0.5) < 0.003 && std::abs (t[3] - (0.5 + 3 * 0.03125)) < 0.003;
            check (good, "A 4x ratchet rolls four hits inside one 1/16 step (" + got.trim() + " s)");
        }
        {
            auto bl = fresh();
            for (int s = 0; s < 16; ++s) on (*bl, s, 1, 0);
            const auto silent = run (*bl, 2.0);
            for (int s = 0; s < 16; ++s) on (*bl, s, 1, 50);
            const auto half = onsets (run (*bl, 8.0), 0.05f);
            check (silent.getMagnitude (0, silent.getNumSamples()) < 1.0e-4f && half.size() > 16 && half.size() < 48,
                   "Probability: 0% never plays, 50% plays about half the steps (" + juce::String ((int) half.size()) + " of 64)");
        }
        {
            // a 2 s loop with a click every 1/8 of its length; sliced across 16 steps it stays in time at any tempo
            juce::AudioBuffer<float> loop (2, (int) (2.0 * sr));
            loop.clear();
            for (int k = 0; k < 16; ++k) for (int ch = 0; ch < 2; ++ch) loop.setSample (ch, (int) (k * 0.125 * sr) + 50, 0.9f);
            auto bl = fresh();
            bl->loadLaneBuffer (0, loop, sr, "Test loop");
            for (int s = 0; s < 16; ++s) on (*bl, s);
            head.bpm = 90.0;
            const auto t = onsets (run (*bl, 2.6), 0.3f);
            const double step = 60.0 / 90.0 / 4.0;
            bool good = (int) bl->param (BeatLab::laneParam (0, "mode").toRawUTF8()) == BeatLab::loopSlice && t.size() >= 14;
            for (size_t i = 0; good && i < t.size(); ++i) good = std::abs (t[i] - (i * step + 50 / sr)) < 0.003;
            check (good, "A loop dropped on a lane is sliced and stays in time at 90 bpm (" + juce::String ((int) t.size()) + " slices on the grid)");

            // the same pattern as a MIDI clip (Pattern to Song), its own sequencer off: still the right slices, in time
            double len = 0;
            const auto seq = bl->patternToMidi (0, len);
            bl->setParam ("sync", 0.0f);
            bl->prepareToPlay (sr, block);
            juce::AudioBuffer<float> out ((int) 2, (int) (2.6 * sr));
            out.clear();
            head.ppq = 0.0;
            juce::AudioBuffer<float> buf (2, block);
            for (int pos = 0; pos + block <= out.getNumSamples(); pos += block)
            {
                juce::MidiBuffer midi;
                const double b0 = head.ppq, b1 = b0 + block / sr * head.bpm / 60.0;
                for (int i = 0; i < seq.getNumEvents(); ++i)
                {
                    const auto& m = seq.getEventPointer (i)->message;
                    if (m.getTimeStamp() >= b0 && m.getTimeStamp() < b1) midi.addEvent (m, (int) ((m.getTimeStamp() - b0) / (b1 - b0) * block));
                }
                buf.clear();
                bl->processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, block);
                head.ppq = b1;
            }
            head.bpm = 120.0;
            const auto t2 = onsets (out, 0.3f);
            good = t2.size() >= 14;
            for (size_t i = 0; good && i < t2.size(); ++i) good = std::abs (t2[i] - (i * step + 50 / sr)) < 0.003;
            check (good, "...and as a MIDI clip in the song it plays the same slices, in time (" + juce::String ((int) t2.size()) + ")");
        }
        {
            auto bl = fresh();
            head.playing = false;
            bl->setParam ("sync", 0.0f);
            bl->setParam ("tempo", 150.0f);
            on (*bl, 0); on (*bl, 4);
            bl->startStop();
            const auto t = onsets (run (*bl, 1.7), 0.05f);
            head.playing = true;
            juce::String got; for (auto x : t) got << juce::String (x, 3) << " ";
            check (t.size() == 3 && std::abs (t[1] - 0.4) < 0.003 && std::abs (t[2] - 1.6) < 0.003 && bl->isRunning(), "Runs on its own clock (150 bpm) when the song is stopped (" + got.trim() + " s)");
        }
        {
            BeatLab a;
            a.setCurrentProgram (10);   // Drill Machine
            juce::MemoryBlock mb;
            a.getStateInformation (mb);
            BeatLab b;
            b.setCurrentProgram (0);
            b.setStateInformation (mb.getData(), (int) mb.getSize());
            bool same = true;
            for (int p = 0; p < 2; ++p) for (int l = 0; l < BeatLab::numLanes; ++l) for (int s = 0; s < BeatLab::maxSteps; ++s)
                same = same && a.getStep (p, l, s).pack() == b.getStep (p, l, s).pack();
            same = same && b.param (BeatLab::laneParam (1, "kit").toRawUTF8()) == (float) BeatLab::glitchKit;
            double len = 0;
            const auto seq = a.patternToMidi (0, len);
            check (same && seq.getNumEvents() > 40 && std::abs (len - 4.0) < 1.0e-9,
                   "Patterns, ratchets and kits save and restore; a pattern turns into a " + juce::String (len, 0) + "-beat MIDI clip ("
                   + juce::String (seq.getNumEvents() / 2) + " notes)");
        }
        {
            // every preset plays at a sensible level (and nothing clips)
            juce::StringArray levels;
            bool good = true;
            const auto names = BeatLab::presetNames();
            for (int i = 0; i < names.size() - 1; ++i)
            {
                BeatLab bl;
                bl.setPlayHead (&head);
                bl.setCurrentProgram (i);
                bl.prepareToPlay (sr, block);
                const auto out = run (bl, 8.0);
                const float db = juce::Decibels::gainToDecibels (rms (out, 0, out.getNumSamples()));
                const float pk = out.getMagnitude (0, out.getNumSamples());
                levels.add (names[i].fromLastOccurrenceOf (": ", false, false) + " " + juce::String (db, 1));
                good = good && allFinite (out) && db > -30.0f && db < -8.0f && pk <= 1.0f;
            }
            check (good, "Every preset plays at a sensible level (dB rms: " + levels.joinIntoString (", ") + ")");
        }
        {
            auto bl = fresh();
            for (int s = 0; s < 16; ++s) on (*bl, s);
            bl->setParam ("chaos", 1.0f); bl->setParam ("mutate", 1.0f);
            const auto wild = run (*bl, 4.0);
            check (allFinite (wild) && wild.getMagnitude (0, wild.getNumSamples()) <= 1.0f && onsets (wild, 0.05f).size() > 8,
                   "Chaos + mutate stays musical-ish: finite, limited, still playing");
        }
    }
    // -------------------------------------------------------------------------------------------
    std::cout << "Every built-in plugin and preset (smoke test):" << std::endl;
    {
        struct Head : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setIsPlaying (true); p.setBpm (120.0); p.setPpqPosition (ppq);
                p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                return p;
            }
        } head;
        juce::Random rng (1);
        int ok = 0, bad = 0;
        juce::StringArray problems;
        for (auto& info : builtinPlugins())
        {
            if (info.category == "Hidden") continue;
            auto p = info.create();
            p->setPlayHead (&head);
            if (! info.midiFx) p->enableAllBuses();
            p->setRateAndBufferSizeDetails (sr, block);
            const int programs = juce::jmax (1, p->getProgramNames().size());
            bool good = true; float peak = 0.0f;
            for (int prog = 0; prog < programs && good; ++prog)
            {
                if (p->getProgramNames().size() > 0) p->setCurrentProgram (prog);
                p->prepareToPlay (sr, block);
                head.ppq = 0.0;
                juce::AudioBuffer<float> buf (info.midiFx ? 0 : 2, block);
                for (int b = 0; b < (int) (2.0 * sr / block); ++b)
                {
                    juce::MidiBuffer midi;
                    if (b % 40 == 0) { midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (b / 40) * 5 % 24, (juce::uint8) 100), 3); }
                    if (b % 40 == 30) { midi.addEvent (juce::MidiMessage::noteOff (1, 48 + (b / 40) * 5 % 24), 7); }
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                        for (int i = 0; i < block; ++i)
                            buf.setSample (ch, i, info.instrument ? 0.0f : 0.3f * (float) std::sin (0.05 * (b * block + i)) + 0.05f * (rng.nextFloat() - 0.5f));
                    if (info.midiFx) { juce::AudioBuffer<float> empty (nullptr, 0, block); p->processBlock (empty, midi); }
                    else p->processBlock (buf, midi);
                    head.ppq += block / sr * 2.0;
                    if (! info.midiFx)
                    {
                        if (! allFinite (buf)) { good = false; break; }
                        peak = juce::jmax (peak, buf.getMagnitude (0, block));
                    }
                }
                if (peak > 8.0f) good = false;
                if (! good) problems.add (info.name + " / " + (programs > 1 ? p->getProgramNames()[prog] : juce::String ("default")) + " (peak " + juce::String (peak, 2) + ")");
            }
            p->releaseResources();
            (good ? ok : bad)++;
        }
        check (bad == 0, juce::String (ok) + " built-in plugins run every preset with finite, sane output" + (problems.isEmpty() ? juce::String() : ": " + problems.joinIntoString (", ")));
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Piano Room and rooms:" << std::endl;
    {
        // plays `notes` (note, velocity 0..1, on second, off second) through an instrument for `seconds`
        struct Note { int note; float vel; double on, off; };
        auto play = [] (BuiltinProcessor& p, std::vector<Note> notes, double seconds, std::vector<std::pair<double, int>> cc64 = {})
        {
            p.setPlayConfigDetails (0, 2, sr, block);
            p.prepareToPlay (sr, block);
            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (2, total);
            out.clear();
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = juce::jmin (block, total - pos);
                juce::MidiBuffer midi;
                for (auto& nt : notes)
                {
                    const int on = (int) (nt.on * sr), off = (int) (nt.off * sr);
                    if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, nt.note, nt.vel), on - pos);
                    if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, nt.note), off - pos);
                }
                for (auto& [t, v] : cc64)
                    if ((int) (t * sr) >= pos && (int) (t * sr) < pos + n) midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, v), (int) (t * sr) - pos);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
                p.processBlock (view, midi);
            }
            return out;
        };
        // fundamental by autocorrelation (between 40 and 1200 Hz) on a mono window
        auto pitchOf = [] (const juce::AudioBuffer<float>& b, int from, int len)
        {
            std::vector<float> x ((size_t) len);
            for (int i = 0; i < len; ++i) x[(size_t) i] = b.getSample (0, from + i) + b.getSample (1, from + i);
            int bestLag = 0; double best = 0;
            const int minLag = (int) (sr / 1200), maxLag = (int) (sr / 40);
            std::vector<double> ac ((size_t) maxLag + 2, 0.0);
            for (int lag = minLag; lag <= maxLag; ++lag)
            {
                double acc = 0;
                for (int i = 0; i + lag < len; ++i) acc += x[(size_t) i] * x[(size_t) (i + lag)];
                ac[(size_t) lag] = acc;
            }
            const double peak = *std::max_element (ac.begin(), ac.end());
            for (int lag = minLag; lag <= maxLag; ++lag)   // first lag that reaches 90% of the best (avoids octave errors)
                if (ac[(size_t) lag] > 0.9 * peak && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1]) { bestLag = lag; break; }
            juce::ignoreUnused (best);
            return bestLag > 0 ? sr / bestLag : 0.0;
        };

        // ---- rooms ----
        {
            juce::StringArray bad;
            for (int sp = 1; sp < roomir::numSpaces; ++sp)
            {
                auto ir = roomir::design (sp, sr, 1.0f, 0.5f, 0.0f);
                double e = 0;
                for (int i = 0; i < ir.getNumSamples(); ++i) e += ir.getSample (0, i) * ir.getSample (0, i);
                // time for the energy to fall 20 dB, x3 = a T60 estimate (Schroeder integration)
                std::vector<double> tail ((size_t) ir.getNumSamples() + 1, 0.0);
                for (int i = ir.getNumSamples(); --i >= 0;) tail[(size_t) i] = tail[(size_t) i + 1] + ir.getSample (0, i) * ir.getSample (0, i);
                int t5 = 0, t25 = 0;
                for (int i = 0; i < ir.getNumSamples(); ++i)
                {
                    const double db = 10.0 * std::log10 (tail[(size_t) i] / tail[0] + 1e-30);
                    if (t5 == 0 && db < -5.0) t5 = i;
                    if (t25 == 0 && db < -25.0) { t25 = i; break; }
                }
                const double t60 = 3.0 * (t25 - t5) / sr;
                const double want = roomir::decaySeconds (sp);
                const bool okDecay = sp == roomir::canyon || sp == roomir::forest || (t60 > want * 0.5 && t60 < want * 1.8);
                if (! allFinite (ir) || std::abs (e - 1.0) > 0.01 || ! okDecay)
                    bad.add (roomir::spaceNames()[sp] + " (T60 " + juce::String (t60, 2) + " s, wanted ~" + juce::String (want, 2) + ")");
            }
            check (bad.isEmpty(), juce::String (roomir::numSpaces - 1) + " spaces design sane impulse responses" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));

            // the canyon answers back: a distinct echo ~190 ms after the start
            auto canyon = roomir::design (roomir::canyon, sr, 1.0f, 0.5f, 0.0f);
            auto energyIn = [&] (const juce::AudioBuffer<float>& b, double t0, double t1) { double e = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) e += b.getSample (0, i) * b.getSample (0, i); return e; };
            check (energyIn (canyon, 0.185, 0.215) > 4.0 * energyIn (canyon, 0.15, 0.18),
                   "Canyon: a distinct echo comes back (" + juce::String (energyIn (canyon, 0.185, 0.215), 4) + " vs " + juce::String (energyIn (canyon, 0.15, 0.18), 4) + " before it)");
        }

        // ---- pianos ----
        const bool haveSamples = PianoPackCache::isInstalled ("grand") && PianoPackCache::isInstalled ("steinway") && PianoPackCache::isInstalled ("upright");
        std::cout << (haveSamples ? "  (piano samples found)" : "  (piano samples NOT found: sampled models fall back to the modelled grand; set WIS_PIANOS)") << std::endl;
        for (int model = 0; model < 5; ++model)
        {
            PianoRoom piano;
            piano.setParam ("model", (float) model);
            piano.setParam ("space", 0.0f);   // dry, so pitch and decay measure the piano itself
            piano.setNonRealtime (true);
            piano.waitUntilReady();
            const auto name = PianoRoom::modelNames()[model];
            auto soft = play (piano, { { 60, 0.25f, 0.05, 1.5 } }, 1.0);
            auto loud = play (piano, { { 60, 0.95f, 0.05, 1.5 } }, 1.0);
            const float ls = rms (soft, (int) (0.06 * sr), (int) (0.5 * sr)), ll = rms (loud, (int) (0.06 * sr), (int) (0.5 * sr));
            const double f = pitchOf (loud, (int) (0.3 * sr), (int) (0.12 * sr));
            const double want = model == 4 ? 523.25 : 261.63;
            check (allFinite (loud) && ll > 0.005f && ll < 0.9f && ll > ls * 1.6f && std::abs (f / want - 1.0) < 0.04,
                   name + ": plays C" + (model == 4 ? "5" : "4") + " at " + juce::String (f, 1) + " Hz, louder when hit harder (" + juce::String (juce::Decibels::gainToDecibels (ls), 1) + " -> " + juce::String (juce::Decibels::gainToDecibels (ll), 1) + " dB)");
            if (model == 0)
            {
                // released without the pedal: damped; with the pedal down: still ringing
                auto damped = play (piano, { { 48, 0.8f, 0.05, 0.4 } }, 1.6);
                auto held = play (piano, { { 48, 0.8f, 0.05, 0.4 } }, 1.6, { { 0.01, 127 } });
                const float d = rms (damped, (int) (1.3 * sr), (int) (1.6 * sr)), h = rms (held, (int) (1.3 * sr), (int) (1.6 * sr));
                check (h > d * 8.0f, "Sustain pedal keeps the strings ringing (" + juce::String (juce::Decibels::gainToDecibels (h / juce::jmax (1e-9f, d)), 1) + " dB more)");
                auto top = play (piano, { { 96, 0.8f, 0.05, 0.3 } }, 1.0);
                check (rms (top, (int) (0.6 * sr), (int) (0.9 * sr)) > 1.0e-4f, "The top keys have no dampers and ring on");
                piano.setParam ("tune", 432.0f);
                auto low = play (piano, { { 69, 0.8f, 0.05, 1.0 } }, 0.6);
                const double a4 = pitchOf (low, (int) (0.25 * sr), (int) (0.12 * sr));
                check (std::abs (a4 - 432.0) < 432.0 * 0.03, "Tuning: A4 = 432 Hz (" + juce::String (a4, 1) + ")");
            }
        }
        {
            // every preset, including the rooms, runs clean
            PianoRoom piano;
            piano.setNonRealtime (true);
            juce::StringArray bad;
            for (int pr = 0; pr < piano.getNumPrograms(); ++pr)
            {
                piano.setCurrentProgram (pr);
                piano.waitUntilReady();
                auto out = play (piano, { { 48, 0.7f, 0.02, 0.8 }, { 64, 0.7f, 0.02, 0.8 }, { 67, 0.7f, 0.3, 0.8 } }, 1.2);
                const float r = rms (out, 0, out.getNumSamples());
                if (! allFinite (out) || r < 0.002f || out.getMagnitude (0, out.getNumSamples()) > 2.0f)
                    bad.add (piano.getProgramName (pr) + " (" + juce::String (juce::Decibels::gainToDecibels (r), 1) + " dB)");
            }
            check (bad.isEmpty(), juce::String (piano.getNumPrograms()) + " piano presets play cleanly" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
        }

        // ---- Junkyard Percussion ----
        std::cout << "Junkyard Percussion:" << std::endl;
        {
            Junkyard junk;
            junk.setNonRealtime (true);
            junk.setParam ("spaceMix", 0.0f);
            juce::StringArray bad;
            int sounding = 0;
            for (int key = Junkyard::kitLow; key <= Junkyard::kitHigh; ++key)
            {
                if (Junkyard::kitKeyName (key).isEmpty()) continue;
                auto out = play (junk, { { key, 0.8f, 0.02, 1.0 } }, 1.4);
                const float pk = out.getMagnitude (0, out.getNumSamples());
                if (! allFinite (out) || pk < 0.02f || pk > 1.0f) bad.add (Junkyard::kitKeyName (key) + " (peak " + juce::String (pk, 3) + ")");
                else ++sounding;
            }
            check (bad.isEmpty(), juce::String (sounding) + " junk kit sounds play (finite, audible, not clipping)" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));

            auto tail = play (junk, { { Junkyard::wind, 0.8f, 0.0, 0.5 }, { Junkyard::brakeDrum, 0.8f, 0.0, 0.1 }, { Junkyard::bowedBass, 0.8f, 0.0, 1.0 } }, 12.0);
            check (junk.getActiveVoices() == 0 && rms (tail, (int) (11.0 * sr), (int) (12.0 * sr)) < 1.0e-4f,
                   "Junkyard voices die away after their release (" + juce::String (junk.getActiveVoices()) + " left)");

            // played as one instrument across the keyboard: in tune
            const auto sources = Junkyard::sourceNames();
            for (auto [name, note, hz] : { std::tuple<const char*, int, double> { "Marimba", 69, 440.0 }, { "Bowed Cello", 57, 220.0 },
                                           { "Double Bass Pizzicato", 40, 82.41 }, { "Bowed Saw", 72, 523.25 } })
            {
                junk.setParam ("source", (float) sources.indexOf (name));
                junk.setParam ("humanize", 0.0f);
                auto out = play (junk, { { note, 0.8f, 0.02, 1.0 } }, 1.0);
                const double f = pitchOf (out, (int) (0.4 * sr), (int) (0.12 * sr));
                check (allFinite (out) && std::abs (f / hz - 1.0) < 0.03, juce::String (name) + " plays in tune (" + juce::String (f, 1) + " Hz, wanted " + juce::String (hz, 1) + ")");
            }

            juce::StringArray badPresets;
            for (int pr = 0; pr < junk.getNumPrograms(); ++pr)
            {
                junk.setCurrentProgram (pr);
                junk.waitUntilReady();
                const bool kit = (int) junk.param ("source") == 0;
                auto out = kit ? play (junk, { { Junkyard::snapFat, 0.8f, 0.02, 0.3 }, { Junkyard::brakeDrum, 0.8f, 0.3, 0.6 }, { Junkyard::wind, 0.8f, 0.1, 1.0 } }, 1.5)
                               : play (junk, { { 48, 0.8f, 0.02, 0.8 }, { 60, 0.8f, 0.3, 0.9 }, { 67, 0.7f, 0.5, 1.0 } }, 1.5);
                const float r = rms (out, 0, out.getNumSamples()), pk = out.getMagnitude (0, out.getNumSamples());
                if (! allFinite (out) || r < 0.002f || pk > 1.2f)
                    badPresets.add (junk.getProgramName (pr) + " (rms " + juce::String (juce::Decibels::gainToDecibels (r), 1) + " dB, peak " + juce::String (pk, 2) + ")");
            }
            check (badPresets.isEmpty(), juce::String (junk.getNumPrograms()) + " Junkyard presets play cleanly" + (badPresets.isEmpty() ? juce::String() : ": " + badPresets.joinIntoString (", ")));
        }

        // ---- Whistler ----
        std::cout << "Whistler:" << std::endl;
        {
            Whistler w;
            w.setCurrentProgram (w.getNumPrograms() - 1);   // dry
            for (auto [id, v] : { std::pair<const char*, float> { "octave", 1.0f }, { "vibrato", 0.0f }, { "wobble", 0.0f }, { "scoop", 0.0f }, { "glide", 300.0f } })
                w.setParam (id, v);
            auto one = play (w, { { 69, 0.8f, 0.05, 1.0 } }, 1.2);
            const double a4 = pitchOf (one, (int) (0.5 * sr), (int) (0.12 * sr));
            check (allFinite (one) && rms (one, (int) (0.4 * sr), (int) (0.9 * sr)) > 0.01f && std::abs (a4 - 440.0) < 440.0 * 0.02,
                   "Whistles A4 in tune (" + juce::String (a4, 1) + " Hz)");

            // legato: it glides from one note to the next
            auto g = play (w, { { 69, 0.8f, 0.05, 1.5 }, { 76, 0.8f, 0.6, 1.5 } }, 1.6);
            const double before = pitchOf (g, (int) (0.45 * sr), (int) (0.1 * sr));
            const double during = pitchOf (g, (int) (0.70 * sr), (int) (0.04 * sr));
            const double after = pitchOf (g, (int) (1.1 * sr), (int) (0.12 * sr));
            check (std::abs (before - 440.0) < 9.0 && std::abs (after - 659.26) < 13.0 && during > 455.0 && during < 640.0,
                   "Glides between legato notes (" + juce::String (before, 0) + " -> " + juce::String (during, 0) + " -> " + juce::String (after, 0) + " Hz)");

            // and still slides after a short gap
            w.setParam ("fall", 0.0f);
            auto gap = play (w, { { 69, 0.8f, 0.05, 0.5 }, { 76, 0.8f, 0.6, 1.2 } }, 1.9);
            const double mid = pitchOf (gap, (int) (0.66 * sr), (int) (0.04 * sr));
            check (mid > 445.0 && mid < 645.0, "...and slides on from the last note after a short gap (" + juce::String (mid, 0) + " Hz)");
            check (rms (gap, (int) (1.8 * sr), (int) (1.9 * sr)) < 0.005f, "Stops when the keys come up");
        }
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Junkyard loops and vocal chains:" << std::endl;
    {
        while (project.numTracks() > 0) project.removeTrack (project.track (0));   // start clean
        auto jt = project.addTrack (kindInstrument, "Finger Snaps");
        project.setInstrument (jt, builtinPresetRef ("junkyard", 2));
        int notes = 0, empty = 0;
        for (int i = 0; i < (int) junkLoops().size(); ++i)
        {
            auto c = insertJunkLoop (project, jt, i, 0.0, 8);
            notes += c.v.getNumChildren();
            if (c.v.getNumChildren() == 0) ++empty;
        }
        check (empty == 0 && notes > 300, juce::String ((int) junkLoops().size()) + " finger-snap / junk / sound-space loops write notes (" + juce::String (notes) + ")");

        // MIDI loops follow the tempo: the first snap ("Snaps on 2 & 4") is on beat 2 at any tempo
        jt.clips().removeAllChildren (nullptr);
        insertJunkLoop (project, jt, 0, 0.0, 8);
        engine.rebuildNow();
        const double oldTempo = project.tempo();
        bool follows = true;
        juce::String got;
        for (double bpm : { 100.0, 150.0 })
        {
            project.setTempo (bpm);
            auto out = renderRange (engine, project, 0.0, 3.0);
            const double t = firstSampleAbove (out, 0.01f) / sr, want = 60.0 / bpm;
            got << juce::String (t, 3) << " s @ " << (int) bpm << "  ";
            follows = follows && allFinite (out) && std::abs (t - want) < 0.02;
        }
        check (follows, "Snap loop follows the tempo (" + got.trim() + ")");
        project.setTempo (oldTempo);
        project.removeTrack (jt);
        engine.rebuildNow();

        // every vocal chain builds and runs a voice-like signal cleanly
        juce::StringArray bad;
        auto vt = project.addTrack (kindAudio, "Vocals");
        for (int ci = 0; ci < (int) vocalChains().size(); ++ci)
        {
            applyVocalChain (project, vt, ci);
            const auto& chain = vocalChains()[(size_t) ci];
            if (vt.inserts().getNumChildren() != (int) chain.fx.size()) { bad.add (chain.name + " (inserts)"); continue; }
            std::vector<std::unique_ptr<BuiltinProcessor>> procs;
            for (auto& ref : vocalChainRefs (ci))
            {
                auto p = createBuiltin (ref.uid);
                decodeState (*p, ref.state);
                p->setPlayConfigDetails (2, 2, sr, block);
                p->prepareToPlay (sr, block);
                procs.push_back (std::move (p));
            }
            juce::Random rng (5);
            float pk = 0.0f; double energy = 0.0;
            bool finite = true;
            for (int b = 0; b < (int) (1.5 * sr / block); ++b)
            {
                juce::AudioBuffer<float> buf (2, block);
                for (int i = 0; i < block; ++i)
                {
                    const double t = (b * block + i) / sr;
                    // a sung vowel: a 200 Hz buzz with a syllable envelope and some breath
                    const float env = (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * t));
                    const float x = env * (0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * t)
                                           + 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 400.0 * t) + 0.02f * (rng.nextFloat() - 0.5f));
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
                juce::MidiBuffer midi;
                for (auto& p : procs) p->processBlock (buf, midi);
                finite = finite && allFinite (buf);
                pk = juce::jmax (pk, buf.getMagnitude (0, block));
                if (b * block > (int) (0.5 * sr)) energy += (double) buf.getRMSLevel (0, 0, block);
            }
            if (! finite || pk > 4.0f || energy < 0.01) bad.add (chain.name + " (peak " + juce::String (pk, 2) + ")");
        }
        project.removeTrack (vt);
        check (bad.isEmpty(), juce::String ((int) vocalChains().size()) + " vocal chains build and run cleanly" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Tempo detection:" << std::endl;
    for (double bpm : { 92.0, 128.0, 140.0 })
    {
        // kick on every beat, hats on the off-beats, starting 0.37 s in, 30 seconds
        const double tsr = 44100.0, spb = 60.0 / bpm, first = 0.37;
        juce::AudioBuffer<float> drums (2, (int) (30.0 * tsr));
        drums.clear();
        juce::Random rnd (1);
        for (int beat = 0; first + beat * spb < 29.5; ++beat)
        {
            const int k = (int) ((first + beat * spb) * tsr);
            for (int i = 0; i < 4000; ++i)
            {
                const float env = std::exp (-i / 900.0f);
                const float v = 0.8f * env * std::sin (juce::MathConstants<float>::twoPi * (55.0f + 80.0f * std::exp (-i / 300.0f)) * i / (float) tsr);
                drums.addSample (0, k + i, v); drums.addSample (1, k + i, v);
            }
            const int h = (int) ((first + (beat + 0.5) * spb) * tsr);
            float prev = 0.0f;
            for (int i = 0; i < 1500; ++i)   // hi-hat: high-passed noise
            {
                const float n = rnd.nextFloat() * 2.0f - 1.0f;
                const float v = 0.3f * std::exp (-i / 250.0f) * (n - prev);
                prev = n;
                drums.addSample (0, h + i, v); drums.addSample (1, h + i, v);
            }
        }
        auto est = estimateTempo (drums, tsr);
        // the phase must land on a kick (any beat), within 25 ms
        const double signedErr = std::remainder (est.firstBeatSeconds - first, spb), phaseErr = std::abs (signedErr);
        check (std::abs (est.bpm - bpm) < 0.6 && phaseErr < 0.025,
               "Detects " + juce::String (bpm, 0) + " BPM (got " + juce::String (est.bpm, 2) + ", beat phase error "
               + juce::String (signedErr * 1000.0, 1) + " ms)");
    }

    // -------------------------------------------------------------------------------------------
    std::cout << "Plugin host:" << std::endl;
    {
        auto node = Project::makePluginNode (builtinRef ("compressor"));
        juce::String err;
        auto p = host.create (node, sr, block, false, err);
        check (p != nullptr && err.isEmpty(), "Create built-in through the host");
        juce::ValueTree bad = Project::makePluginNode ({ "external", "VST3-Nope-123", "Nope", {}, {} });
        auto p2 = host.create (bad, sr, block, false, err);
        check (p2 == nullptr && err.isNotEmpty(), "Missing plugin reports an error: " + err);
        juce::StringArray names;
        for (auto* f : host.formats.getFormats()) names.add (f->getName());
        check (names.contains ("VST3") && names.contains ("VST") && names.contains ("CLAP"), "Plugin formats: " + names.joinIntoString (", "));
    }

#if defined (WIS_TEST_VST2) && defined (WIS_TEST_CLAP)
    std::vector<std::tuple<juce::String, juce::String, int>> pluginTests { { "VST", WIS_TEST_VST2, 0 }, { "CLAP", WIS_TEST_CLAP, 32 } };
    // Windows CI: the same test synth built 32-bit, through the 32-bit bridge (wisbridge32.exe)
    const auto x86Plugin = juce::SystemStats::getEnvironmentVariable ("WIS_TEST_VST2_X86", {});
    if (x86Plugin.isNotEmpty()) pluginTests.push_back ({ "VST", x86Plugin, 0 });
    for (auto [formatName, path, latency] : pluginTests)
    {
        const bool bridged = path == x86Plugin;
        std::cout << formatName << (bridged ? " hosting through the 32-bit bridge:" : " hosting:") << std::endl;
        while (project.numTracks() > 0) project.removeTrack (project.track (0));
        juce::AudioPluginFormat* format = nullptr;
        for (auto* f : host.formats.getFormats()) if (f->getName() == formatName) format = f;
        juce::OwnedArray<juce::PluginDescription> found;
        if (format != nullptr) format->findAllTypesForFile (found, path);
        check (found.size() == 1 && found[0]->isInstrument && found[0]->name.startsWith ("WIS Test Synth")
               && (! bridged || found[0]->descriptiveName.contains ("32-bit")),
               juce::String ("Scan finds the test plugin (") + (found.isEmpty() ? juce::String ("nothing") : found[0]->descriptiveName) + ")");
        if (found.isEmpty()) continue;

        project.setTempo (120.0);
        auto t = project.addTrack (kindInstrument, formatName);
        project.setInstrument (t, PluginHost::refFor (*found[0]));
        auto c = project.addMidiClip (t, 0.0, 8.0);
        project.addNote (c, 69, 2.0, 2.0, 100);   // A4 from 1.0 s to 2.0 s
        engine.rebuildNow();
        auto slotId = t.instrument()[ids::id].toString();
        auto* proc = engine.getProcessor (slotId);
        check (proc != nullptr && engine.getSlotError (slotId).isEmpty(), "Loads on an instrument track " + engine.getSlotError (slotId));
        if (proc == nullptr) continue;
        check (engine.getTrackLatency (t.id()) == latency, "Reports its latency (" + juce::String (engine.getTrackLatency (t.id())) + " samples)");

        auto out = renderRange (engine, project, 0.0, 6.0);
        const double hz = zeroCrossingHz (out, (int) (1.1 * sr), (int) (1.9 * sr));
        const int onset = firstSampleAbove (out, 0.01f);
        check (std::abs (hz - 440.0) < 2.0, "MIDI note plays A4 (" + juce::String (hz, 1) + " Hz)");
        check (std::abs (onset - (int) sr) < 64, "...on time, latency compensated (onset " + juce::String (onset / sr, 4) + " s)");
        check (rms (out, (int) (2.2 * sr), (int) (5.5 * sr)) < 1.0e-4f, "Note off stops it");

        // the host's tempo reaches the plugin
        auto params = proc->getParameters();
        check (params.size() >= 2 && std::abs (params[1]->getValue() * 300.0f - 120.0f) < 0.5f,
               "Plugin sees the song tempo (" + (params.size() >= 2 ? params[1]->getText (params[1]->getValue(), 32) : juce::String ("-")) + ")");

        // parameter automation reaches the plugin (gain -> 0 on audio input isn't testable on an instrument; check the value instead)
        params[0]->setValue (0.25f);
        renderRange (engine, project, 0.0, 0.5);   // parameter changes reach the plugin with the next block
        engine.flushPluginStates();
        auto stateCopy = t.instrument()[ids::state].toString();
        params[0]->setValue (0.9f);
        juce::String err;
        auto fresh = host.create (t.instrument(), sr, block, true, err);
        check (fresh != nullptr && std::abs (fresh->getParameters()[0]->getValue() - 0.25f) < 0.01f, "State saves and restores (gain 0.25)");
        project.removeTrack (t);
        engine.rebuildNow();
    }
#endif

    tmpRoot.deleteRecursively();
    std::cout << (failures == 0 ? "ALL DAW TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
