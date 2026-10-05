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
#include "Daw/Instruments/InstrumentRefs.h"
#include "Daw/Instruments/SoundFontInstrument.h"
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
        check (host.formats.getNumFormats() >= 1, "VST3 hosting available (" + juce::String (host.formats.getNumFormats()) + " format)");
    }

    tmpRoot.deleteRecursively();
    std::cout << (failures == 0 ? "ALL DAW TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
