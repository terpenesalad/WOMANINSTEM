#include "Daydream.h"
#include <cmath>

namespace wis::daw
{

using namespace prm;
using namespace daydream;

std::function<juce::AudioProcessorEditor* (Daydream&)> Daydream::editorFactory;

juce::StringArray Daydream::perfNames()
{
    return { "Chord", "Strum", "Strum 2 Octaves", "Sloppy Strum", "Arpeggio", "Arpeggio 2 Octaves", "Pattern", "Harp" };
}

juce::StringArray Daydream::bassModeNames()
{
    return { "Off", "With Chords", "Chords + Single Notes", "Bass Only" };
}

namespace
{
    juce::StringArray keyNames() { return { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; }

    Layout daydreamLayout()
    {
        Layout l;
        // ---- playing ----------------------------------------------------------------------------------
        addDb      (l, "volume", "Volume", -30.0f, 6.0f, -3.0f);
        addChoice  (l, "position", "Chord Position", positionNames(), stayCentred);
        addFloat   (l, "centre", "Centre", 48.0f, 76.0f, 62.0f, "", 0.0f, 0);
        addChoice  (l, "spread", "Spread", spreadNames(), close);
        addFloat   (l, "voicing", "Voicing", -24.0f, 24.0f, 0.0f, "", 0.0f, 0);
        addFloat   (l, "bassVoicing", "Bass Voicing", -12.0f, 12.0f, 0.0f, "", 0.0f, 0);
        addBool    (l, "sus2", "Sus Plays Sus2", false);
        addBool    (l, "keyMode", "Key Mode", false);
        addChoice  (l, "key", "Key", keyNames(), 0);
        addChoice  (l, "scale", "Scale", scaleNames(), 0);
        addBool    (l, "followSongKey", "Follow the Song's Key", false);
        addFloat   (l, "transpose", "Transpose", -12.0f, 12.0f, 0.0f, "st", 0.0f, 0);
        addChoice  (l, "buttonZone", "Chord Buttons on Keys", { "C1 to C2", "C2 to C3", "C3 to C4", "Off (on-screen only)" }, 1);
        addChoice  (l, "extAdd", "Adding Colours", { "Add Just the New Notes", "Play the Whole Chord Again" }, 0);
        addBool    (l, "hold", "Hold Chords", false);
        // ---- performance ------------------------------------------------------------------------------
        addChoice  (l, "perf", "Performance", Daydream::perfNames(), 0);
        addMs      (l, "strumMs", "Strum Speed", 5.0f, 300.0f, 40.0f);
        addChoice  (l, "strumDir", "Strum Direction", { "Up", "Down", "Up and Down" }, 0);
        addPercent (l, "slop", "Slop", 0.5f);
        addChoice  (l, "arpRate", "Arp Rate", { "1/4", "1/8", "1/8 Triplet", "1/16", "1/16 Triplet", "1/32" }, 1);
        addChoice  (l, "arpOrder", "Arp Order", { "Up", "Down", "Up and Down", "Down and Up", "Random", "Outside In" }, 0);
        addPercent (l, "gate", "Gate", 0.6f);
        addChoice  (l, "pattern", "Pattern", patternNames(), 0);
        addMs      (l, "harpMs", "Harp Speed", 8.0f, 150.0f, 28.0f);
        addChoice  (l, "harpOctaves", "Harp Octaves", { "2", "3", "4", "5" }, 1);
        addPercent (l, "swing", "Swing", 0.0f);
        // ---- bass ---------------------------------------------------------------------------------------
        addChoice  (l, "bassMode", "Bass", Daydream::bassModeNames(), 0);
        addChoice  (l, "bassSound", "Bass Sound", bassSoundNames(), 1);
        addDb      (l, "bassVolume", "Bass Volume", -30.0f, 6.0f, -4.0f);
        addChoice  (l, "bassOctave", "Bass Octave", { "1", "2", "3" }, 1);
        addMs      (l, "bassGlide", "Bass Glide", 0.0f, 400.0f, 0.0f);
        addPercent (l, "bassTone", "Bass Tone", 0.5f);
        // ---- tempo, beats, loop -------------------------------------------------------------------------
        addChoice  (l, "tempoSrc", "Tempo", { "Follow the Song", "Own Tempo" }, 0);
        addFloat   (l, "bpm", "BPM", 40.0f, 240.0f, 100.0f, "bpm", 0.0f, 0);
        addChoice  (l, "beat", "Beat", grooveNames(), 0);
        addChoice  (l, "beatKit", "Beat Kit", DrumSynth::kitNames(), 1);
        addDb      (l, "beatVolume", "Beat Volume", -30.0f, 6.0f, -8.0f);
        addBool    (l, "beatOn", "Beat Playing", false);
        addChoice  (l, "loopLength", "Loop Length", { "Free", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }, 3);
        addBool    (l, "lockPerf", "Lock Performance", false);
        addBool    (l, "lockFx", "Lock Effects", false);
        // ---- effects ------------------------------------------------------------------------------------
        addChoice  (l, "fxFocus", "FX Knob Turns", fxNames(), 7);
        addPercent (l, "drive", "Drive", 0.0f);
        addPercent (l, "driveTone", "Drive Tone", 0.6f);
        addPercent (l, "tremolo", "Tremolo", 0.0f);
        addFloat   (l, "tremRate", "Tremolo Speed", 0.5f, 12.0f, 5.0f, "Hz", 0.0f, 1);
        addBool    (l, "tremPan", "Tremolo Pans", false);
        addPercent (l, "phaser", "Phaser", 0.0f);
        addFloat   (l, "phaserRate", "Phaser Speed", 0.05f, 6.0f, 0.4f, "Hz", 0.8f, 2);
        addPercent (l, "phaserFeedback", "Phaser Feedback", 0.5f);
        addPercent (l, "flanger", "Flanger", 0.0f);
        addFloat   (l, "flangerRate", "Flanger Speed", 0.03f, 4.0f, 0.2f, "Hz", 0.5f, 2);
        addPercent (l, "flangerFeedback", "Flanger Feedback", 0.5f);
        addPercent (l, "chorus", "Chorus", 0.0f);
        addFloat   (l, "chorusRate", "Chorus Speed", 0.05f, 5.0f, 0.6f, "Hz", 0.8f, 2);
        addPercent (l, "chorusDepth", "Chorus Depth", 0.5f);
        addPercent (l, "ensemble", "Ensemble", 0.0f);
        addPercent (l, "delay", "Delay", 0.0f);
        addChoice  (l, "delayTime", "Delay Time", delayDivisionNames(), 3);
        addPercent (l, "delayFeedback", "Delay Feedback", 0.35f);
        addPercent (l, "delayTone", "Delay Tone", 0.55f);
        addBool    (l, "pingPong", "Ping-Pong", true);
        addPercent (l, "reverb", "Reverb", 0.0f);
        addPercent (l, "reverbSize", "Reverb Size", 0.6f);
        addPercent (l, "reverbDamp", "Reverb Damping", 0.4f);
        // ---- the sound ----------------------------------------------------------------------------------
        addChoice  (l, "engine", "Engine", engineNames(), 0);
        addDb      (l, "trim", "Sound Level", -24.0f, 12.0f, 0.0f);
        addChoice  (l, "waveA", "Osc 1 Wave", waveNames(), 0);
        addChoice  (l, "waveB", "Osc 2 Wave", waveNames(), 1);
        addFloat   (l, "semiB", "Osc 2 Pitch", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0);
        addFloat   (l, "fineB", "Osc 2 Detune", -50.0f, 50.0f, 7.0f, "ct", 0.0f, 0);
        addPercent (l, "mixB", "Osc Mix", 0.35f);
        addPercent (l, "sub", "Sub Osc", 0.0f);
        addPercent (l, "noise", "Noise", 0.0f);
        addFloat   (l, "pw", "Pulse Width", 0.05f, 0.95f, 0.35f, "", 0.0f, 2);
        addChoice  (l, "unison", "Unison", { "1", "2", "3", "4" }, 0);
        addFloat   (l, "detune", "Unison Detune", 0.0f, 50.0f, 12.0f, "ct", 0.0f, 0);
        addChoice  (l, "algorithm", "FM Algorithm", algorithmNames(), 2);
        for (int i = 1; i <= 4; ++i)
        {
            const juce::String n (i);
            const float defRatio[] = { 1.0f, 1.0f, 1.0f, 3.0f }, defLevel[] = { 1.0f, 0.4f, 0.8f, 0.25f };
            addFloat   (l, "op" + n + "Ratio", "Op " + n + " Ratio", 0.5f, 16.0f, defRatio[i - 1], "", 3.0f, 2);
            addPercent (l, "op" + n + "Level", "Op " + n + " Level", defLevel[i - 1]);
            addMs      (l, "op" + n + "Attack", "Op " + n + " Attack", 0.0f, 3000.0f, 1.0f);
            addMs      (l, "op" + n + "Decay", "Op " + n + " Decay", 10.0f, 10000.0f, i == 1 || i == 3 ? 3000.0f : 700.0f);
            addPercent (l, "op" + n + "Sustain", "Op " + n + " Sustain", i == 1 || i == 3 ? 0.6f : 0.15f);
        }
        addPercent (l, "feedback", "FM Feedback", 0.0f);
        addPercent (l, "fmVelocity", "FM Touch", 0.5f);
        addPercent (l, "bark", "Reed Bark", 0.45f);
        addPercent (l, "reedTone", "Reed Tone", 0.5f);
        addFloat   (l, "reedDecay", "Reed Decay", 0.3f, 2.5f, 1.0f, "x", 0.0f, 2);
        addPercent (l, "mechanics", "Key Noise", 0.35f);
        addChoice  (l, "filterType", "Filter", filterNames(), 0);
        addHz      (l, "cutoff", "Cutoff", 30.0f, 18000.0f, 2400.0f);
        addPercent (l, "reso", "Resonance", 0.15f);
        addFloat   (l, "envAmount", "Filter Env", -1.0f, 1.0f, 0.35f, "", 0.0f, 2);
        addPercent (l, "keyTrack", "Key Track", 0.5f);
        addPercent (l, "velCutoff", "Touch > Cutoff", 0.3f);
        addPercent (l, "synthDrive", "Synth Drive", 0.0f);
        addMs      (l, "attack", "Attack", 1.0f, 5000.0f, 4.0f);
        addMs      (l, "decay", "Decay", 10.0f, 10000.0f, 900.0f);
        addPercent (l, "sustain", "Sustain", 0.75f);
        addMs      (l, "release", "Release", 5.0f, 8000.0f, 350.0f);
        addMs      (l, "fAttack", "Filter Attack", 1.0f, 5000.0f, 4.0f);
        addMs      (l, "fDecay", "Filter Decay", 10.0f, 10000.0f, 600.0f);
        addPercent (l, "fSustain", "Filter Sustain", 0.3f);
        addMs      (l, "fRelease", "Filter Release", 5.0f, 8000.0f, 400.0f);
        addFloat   (l, "lfoRate", "LFO Speed", 0.05f, 15.0f, 5.0f, "Hz", 2.0f, 2);
        addMs      (l, "lfoDelay", "LFO Delay", 0.0f, 3000.0f, 300.0f);
        addFloat   (l, "vibrato", "Vibrato", 0.0f, 100.0f, 0.0f, "ct", 0.0f, 0);
        addFloat   (l, "lfoFilter", "LFO > Filter", 0.0f, 3.0f, 0.0f, "oct", 0.0f, 2);
        addPercent (l, "lfoPwm", "LFO > Pulse Width", 0.0f);
        addPercent (l, "lfoAmp", "LFO > Level", 0.0f);
        addFloat   (l, "drift", "Analogue Drift", 0.0f, 30.0f, 3.0f, "ct", 0.0f, 0);
        addPercent (l, "velSens", "Touch", 0.6f);
        addPercent (l, "width", "Stereo Width", 0.5f);
        return l;
    }

    // ---- the sounds ------------------------------------------------------------------------------------
    using Values = std::vector<std::pair<const char*, float>>;
    struct Preset { const char* name; const char* category; Values values; };

    // a few building blocks
    Values join (std::initializer_list<Values> parts)
    {
        Values v;
        for (auto& p : parts) v.insert (v.end(), p.begin(), p.end());
        return v;
    }
    const Values reedBase { { "engine", 2 }, { "filterType", 1 }, { "cutoff", 16000 }, { "envAmount", 0 }, { "reso", 0 }, { "attack", 1 }, { "decay", 10000 },
                            { "sustain", 1 }, { "release", 300 }, { "velSens", 0.75f }, { "drift", 1 }, { "width", 0.6f } };
    const Values fmBase { { "engine", 1 }, { "filterType", 1 }, { "cutoff", 16000 }, { "envAmount", 0 }, { "reso", 0 }, { "attack", 1 }, { "decay", 10000 },
                          { "sustain", 1 }, { "drift", 1 } };
    const Values organ { { "engine", 1 }, { "algorithm", 5 }, { "filterType", 1 }, { "cutoff", 9000 }, { "envAmount", 0 }, { "reso", 0 },
                         { "op1Ratio", 0.5f }, { "op2Ratio", 1 }, { "op3Ratio", 2 }, { "op4Ratio", 3 },
                         { "op1Level", 0.7f }, { "op2Level", 0.9f }, { "op3Level", 0.55f }, { "op4Level", 0.3f },
                         { "op1Sustain", 1 }, { "op2Sustain", 1 }, { "op3Sustain", 1 }, { "op4Sustain", 1 },
                         { "op1Decay", 10000 }, { "op2Decay", 10000 }, { "op3Decay", 10000 }, { "op4Decay", 10000 },
                         { "attack", 6 }, { "decay", 10000 }, { "sustain", 1 }, { "release", 60 }, { "velSens", 0.0f }, { "drift", 2 } };
    const Values pad { { "engine", 0 }, { "waveA", 0 }, { "waveB", 0 }, { "fineB", 9 }, { "mixB", 0.5f }, { "unison", 2 }, { "detune", 16 },
                       { "attack", 700 }, { "decay", 4000 }, { "sustain", 0.85f }, { "release", 1800 }, { "envAmount", 0.15f }, { "fAttack", 900 },
                       { "fDecay", 3000 }, { "fSustain", 0.5f }, { "velSens", 0.3f }, { "drift", 6 }, { "width", 0.8f } };
    const Values pluck { { "engine", 1 }, { "algorithm", 3 }, { "filterType", 0 }, { "cutoff", 7000 }, { "envAmount", 0.1f }, { "reso", 0.05f },
                         { "op1Ratio", 1 }, { "op2Ratio", 2 }, { "op3Ratio", 3.01f }, { "op4Ratio", 5 },
                         { "op1Level", 1 }, { "op2Level", 0.45f }, { "op3Level", 0.25f }, { "op4Level", 0.12f },
                         { "op1Decay", 3600 }, { "op1Sustain", 0 }, { "op2Decay", 380 }, { "op2Sustain", 0 }, { "op3Decay", 160 }, { "op3Sustain", 0 },
                         { "op4Decay", 60 }, { "op4Sustain", 0 }, { "attack", 1 }, { "decay", 3800 }, { "sustain", 0 }, { "release", 500 }, { "drift", 2 } };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p {
            // ---- keys ---------------------------------------------------------------------------------------
            { "Velvet Reed Piano", "Keys", join ({ reedBase, { { "bark", 0.4f }, { "reedTone", 0.5f }, { "tremolo", 0.22f }, { "tremRate", 5.2f }, { "tremPan", 1 },
                                                    { "reverb", 0.2f }, { "fxFocus", 1 } } }) },
            { "Barking Reed", "Keys", join ({ reedBase, { { "bark", 0.85f }, { "reedTone", 0.72f }, { "mechanics", 0.55f }, { "drive", 0.22f }, { "driveTone", 0.5f },
                                               { "phaser", 0.3f }, { "phaserRate", 0.3f }, { "reverb", 0.12f }, { "fxFocus", 0 } } }) },
            { "Mellow Reed Stereo", "Keys", join ({ reedBase, { { "bark", 0.22f }, { "reedTone", 0.32f }, { "tremolo", 0.5f }, { "tremRate", 4.4f }, { "tremPan", 1 },
                                                     { "chorus", 0.2f }, { "reverb", 0.22f }, { "fxFocus", 1 } } }) },
            { "Late Night Bossa Keys", "Keys", join ({ reedBase, { { "bark", 0.35f }, { "reedTone", 0.45f }, { "perf", Daydream::perfPattern }, { "pattern", 4 },
                                                        { "chorus", 0.18f }, { "reverb", 0.25f }, { "fxFocus", 7 } } }) },
            { "Glass Tine Piano", "Keys", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 1 }, { "op3Ratio", 1 }, { "op4Ratio", 14 },
                                                   { "op1Level", 1 }, { "op2Level", 0.32f }, { "op3Level", 0.45f }, { "op4Level", 0.3f },
                                                   { "op1Decay", 4500 }, { "op1Sustain", 0.1f }, { "op2Decay", 900 }, { "op2Sustain", 0.1f },
                                                   { "op3Decay", 1200 }, { "op3Sustain", 0 }, { "op4Decay", 140 }, { "op4Sustain", 0 },
                                                   { "release", 450 }, { "velSens", 0.7f }, { "fmVelocity", 0.7f }, { "chorus", 0.3f }, { "reverb", 0.18f }, { "fxFocus", 4 } } }) },
            { "Sunday Organ", "Keys", join ({ organ, { { "chorus", 0.35f }, { "chorusRate", 0.9f }, { "tremolo", 0.12f }, { "tremRate", 6.5f }, { "reverb", 0.3f }, { "fxFocus", 4 } } }) },
            { "Thrift Combo Organ", "Keys", { { "waveA", 1 }, { "waveB", 1 }, { "semiB", 12 }, { "fineB", 2 }, { "mixB", 0.4f }, { "filterType", 1 }, { "cutoff", 3800 },
                                              { "envAmount", 0 }, { "attack", 3 }, { "sustain", 1 }, { "release", 50 }, { "vibrato", 14 }, { "lfoRate", 6.6f },
                                              { "lfoDelay", 0 }, { "velSens", 0 }, { "drive", 0.18f }, { "reverb", 0.25f }, { "fxFocus", 7 } } },
            { "Wobbly Tape Keys", "Keys", join ({ reedBase, { { "bark", 0.3f }, { "reedTone", 0.38f }, { "drift", 14 }, { "vibrato", 9 }, { "lfoRate", 0.8f },
                                                   { "lfoDelay", 0 }, { "flanger", 0.25f }, { "flangerRate", 0.12f }, { "chorus", 0.25f }, { "reverb", 0.2f }, { "fxFocus", 3 } } }) },
            // ---- pads ---------------------------------------------------------------------------------------
            { "Moon Pad", "Pads", join ({ pad, { { "unison", 2 }, { "detune", 18 }, { "cutoff", 1400 }, { "ensemble", 0.45f }, { "reverb", 0.55f },
                                                 { "reverbSize", 0.85f }, { "fxFocus", 7 } } }) },
            { "Velour Strings", "Pads", join ({ pad, { { "unison", 1 }, { "waveB", 0 }, { "cutoff", 2600 }, { "attack", 320 }, { "release", 1200 },
                                                       { "ensemble", 0.85f }, { "reverb", 0.3f }, { "fxFocus", 5 } } }) },
            { "Glacier", "Pads", join ({ fmBase, { { "algorithm", 1 }, { "op1Level", 1 }, { "op2Level", 0.25f }, { "op3Level", 0.3f }, { "op4Level", 0.2f },
                                                   { "op2Ratio", 2 }, { "op3Ratio", 3 }, { "op4Ratio", 7 }, { "op2Attack", 1200 }, { "op2Sustain", 0.8f },
                                                   { "attack", 900 }, { "release", 2500 }, { "sustain", 0.9f }, { "velSens", 0.2f }, { "width", 0.9f },
                                                   { "delay", 0.3f }, { "delayTime", 5 }, { "reverb", 0.6f }, { "reverbSize", 0.9f }, { "fxFocus", 6 } } }) },
            { "Warm Blanket", "Pads", join ({ pad, { { "waveA", 3 }, { "waveB", 0 }, { "mixB", 0.4f }, { "cutoff", 900 }, { "lfoFilter", 0.35f }, { "lfoRate", 0.25f },
                                                     { "lfoDelay", 0 }, { "chorus", 0.4f }, { "reverb", 0.4f }, { "fxFocus", 4 } } }) },
            { "Dusty Choir Pad", "Pads", join ({ pad, { { "waveA", 2 }, { "waveB", 2 }, { "pw", 0.3f }, { "lfoPwm", 0.5f }, { "lfoRate", 0.6f }, { "lfoDelay", 0 },
                                                        { "filterType", 2 }, { "cutoff", 1100 }, { "reso", 0.45f }, { "envAmount", 0.05f },
                                                        { "ensemble", 0.6f }, { "reverb", 0.5f }, { "trim", 4 }, { "fxFocus", 5 } } }) },
            { "Deep Space Drift", "Pads", join ({ pad, { { "unison", 3 }, { "detune", 22 }, { "cutoff", 1100 }, { "lfoFilter", 0.8f }, { "lfoRate", 0.08f },
                                                         { "lfoDelay", 0 }, { "flanger", 0.3f }, { "flangerRate", 0.06f }, { "delay", 0.32f }, { "delayTime", 6 },
                                                         { "reverb", 0.7f }, { "reverbSize", 0.95f }, { "fxFocus", 7 } } }) },
            { "Aurora Swells", "Pads", join ({ pad, { { "perf", Daydream::perfPattern }, { "pattern", 9 }, { "attack", 380 }, { "release", 900 },
                                                      { "cutoff", 1700 }, { "chorus", 0.3f }, { "reverb", 0.5f }, { "fxFocus", 7 } } }) },
            // ---- synths -------------------------------------------------------------------------------------
            { "Psych Poly", "Synths", { { "waveA", 0 }, { "waveB", 1 }, { "mixB", 0.4f }, { "sub", 0.25f }, { "cutoff", 1500 }, { "envAmount", 0.45f },
                                        { "fDecay", 380 }, { "fSustain", 0.25f }, { "reso", 0.25f }, { "decay", 2000 }, { "sustain", 0.7f },
                                        { "phaser", 0.45f }, { "phaserRate", 0.25f }, { "delay", 0.22f }, { "reverb", 0.18f }, { "fxFocus", 2 } } },
            { "Vintage Brass Synth", "Synths", { { "waveA", 0 }, { "waveB", 0 }, { "fineB", 11 }, { "mixB", 0.5f }, { "cutoff", 750 }, { "envAmount", 0.55f },
                                                 { "fAttack", 70 }, { "fDecay", 600 }, { "fSustain", 0.45f }, { "attack", 25 }, { "sustain", 0.85f },
                                                 { "release", 260 }, { "vibrato", 6 }, { "lfoDelay", 600 }, { "chorus", 0.25f }, { "reverb", 0.15f }, { "fxFocus", 4 } } },
            { "Square Dance", "Synths", { { "waveA", 2 }, { "waveB", 2 }, { "pw", 0.25f }, { "semiB", 12 }, { "mixB", 0.25f }, { "cutoff", 3000 },
                                          { "envAmount", 0.2f }, { "decay", 600 }, { "sustain", 0.4f }, { "release", 200 },
                                          { "perf", Daydream::perfArp }, { "arpRate", 3 }, { "arpOrder", 0 }, { "gate", 0.55f },
                                          { "delay", 0.25f }, { "delayTime", 3 }, { "fxFocus", 6 } } },
            { "Lucid Chords", "Synths", { { "waveA", 0 }, { "waveB", 0 }, { "unison", 3 }, { "detune", 24 }, { "cutoff", 3800 }, { "envAmount", 0.2f },
                                          { "synthDrive", 0.15f }, { "sustain", 0.8f }, { "release", 500 }, { "drive", 0.15f }, { "reverb", 0.3f }, { "fxFocus", 0 } } },
            { "Rubber Funk Stabs", "Synths", { { "waveA", 1 }, { "waveB", 0 }, { "mixB", 0.3f }, { "cutoff", 480 }, { "reso", 0.5f }, { "envAmount", 0.8f },
                                               { "fDecay", 180 }, { "fSustain", 0 }, { "decay", 400 }, { "sustain", 0.3f }, { "release", 120 },
                                               { "perf", Daydream::perfPattern }, { "pattern", 1 }, { "phaser", 0.35f }, { "phaserRate", 1.2f }, { "fxFocus", 2 } } },
            { "Sweeping Saws", "Synths", { { "waveA", 0 }, { "waveB", 0 }, { "unison", 2 }, { "detune", 14 }, { "filterType", 2 }, { "cutoff", 900 },
                                           { "reso", 0.55f }, { "envAmount", 0 }, { "lfoFilter", 1.4f }, { "lfoRate", 0.18f }, { "lfoDelay", 0 },
                                           { "sustain", 0.9f }, { "release", 700 }, { "flanger", 0.3f }, { "reverb", 0.3f }, { "trim", 3 }, { "fxFocus", 3 } } },
            // ---- plucks and strums -----------------------------------------------------------------------------
            { "Twelve-String Strum", "Plucks & Strums", join ({ pluck, { { "perf", Daydream::perfStrum2 }, { "strumMs", 32 }, { "chorus", 0.35f },
                                                                        { "reverb", 0.2f }, { "fxFocus", 4 } } }) },
            { "Nylon Strum", "Plucks & Strums", join ({ pluck, { { "op2Level", 0.3f }, { "op3Level", 0.12f }, { "op4Level", 0.03f }, { "cutoff", 4200 },
                                                                { "perf", Daydream::perfStrum }, { "strumMs", 65 }, { "strumDir", 2 }, { "reverb", 0.22f }, { "fxFocus", 7 } } }) },
            { "Harp Glissando", "Plucks & Strums", { { "waveA", 3 }, { "waveB", 4 }, { "mixB", 0.5f }, { "cutoff", 5000 }, { "envAmount", 0.2f }, { "fDecay", 300 },
                                                     { "decay", 1600 }, { "sustain", 0 }, { "release", 1200 }, { "perf", Daydream::perfHarp }, { "harpMs", 30 },
                                                     { "harpOctaves", 1 }, { "reverb", 0.45f }, { "reverbSize", 0.8f }, { "fxFocus", 7 } } },
            { "Kalimba Arp", "Plucks & Strums", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 7.13f }, { "op3Ratio", 1 }, { "op4Ratio", 3 },
                                                                 { "op1Level", 1 }, { "op2Level", 0.3f }, { "op3Level", 0.3f }, { "op4Level", 0.2f },
                                                                 { "op1Decay", 900 }, { "op1Sustain", 0 }, { "op2Decay", 50 }, { "op2Sustain", 0 },
                                                                 { "op3Decay", 600 }, { "op3Sustain", 0 }, { "op4Decay", 80 }, { "op4Sustain", 0 },
                                                                 { "sustain", 0 }, { "decay", 1200 }, { "release", 600 },
                                                                 { "perf", Daydream::perfArp }, { "arpRate", 1 }, { "arpOrder", 2 }, { "gate", 0.9f },
                                                                 { "delay", 0.3f }, { "delayTime", 3 }, { "reverb", 0.2f }, { "fxFocus", 6 } } }) },
            { "Marimba Pattern", "Plucks & Strums", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 4 }, { "op3Ratio", 1 }, { "op4Ratio", 10 },
                                                                     { "op1Level", 1 }, { "op2Level", 0.35f }, { "op3Level", 0.2f }, { "op4Level", 0.15f },
                                                                     { "op1Decay", 700 }, { "op1Sustain", 0 }, { "op2Decay", 40 }, { "op2Sustain", 0 },
                                                                     { "op3Decay", 500 }, { "op3Sustain", 0 }, { "op4Decay", 30 }, { "op4Sustain", 0 },
                                                                     { "sustain", 0 }, { "decay", 900 }, { "release", 300 },
                                                                     { "perf", Daydream::perfPattern }, { "pattern", 2 }, { "reverb", 0.18f }, { "fxFocus", 7 } } }) },
            { "Music Box", "Plucks & Strums", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 2 }, { "op2Ratio", 7 }, { "op3Ratio", 4 }, { "op4Ratio", 1 },
                                                               { "op1Level", 1 }, { "op2Level", 0.2f }, { "op3Level", 0.3f }, { "op4Level", 0.1f },
                                                               { "op1Decay", 1800 }, { "op1Sustain", 0 }, { "op2Decay", 120 }, { "op2Sustain", 0 },
                                                               { "op3Decay", 900 }, { "op3Sustain", 0 }, { "op4Decay", 200 }, { "op4Sustain", 0 },
                                                               { "sustain", 0 }, { "decay", 2000 }, { "release", 800 },
                                                               { "perf", Daydream::perfArp2 }, { "arpRate", 3 }, { "arpOrder", 2 }, { "gate", 0.9f },
                                                               { "reverb", 0.4f }, { "fxFocus", 7 } } }) },
            { "Sloppy Piano Hands", "Plucks & Strums", join ({ reedBase, { { "bark", 0.35f }, { "reedTone", 0.55f }, { "perf", Daydream::perfSlop }, { "slop", 0.6f },
                                                                          { "reverb", 0.22f }, { "fxFocus", 7 } } }) },
            // ---- bells ---------------------------------------------------------------------------------------
            { "Crystal Bells", "Bells", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 3.5f }, { "op3Ratio", 2 }, { "op4Ratio", 7.2f },
                                                          { "op1Level", 1 }, { "op2Level", 0.5f }, { "op3Level", 0.4f }, { "op4Level", 0.25f },
                                                          { "op1Decay", 5000 }, { "op1Sustain", 0 }, { "op2Decay", 1800 }, { "op2Sustain", 0 },
                                                          { "op3Decay", 3000 }, { "op3Sustain", 0 }, { "op4Decay", 900 }, { "op4Sustain", 0 },
                                                          { "sustain", 0 }, { "decay", 6000 }, { "release", 1500 }, { "reverb", 0.45f }, { "fxFocus", 7 } } }) },
            { "Vibraphone Haze", "Bells", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 4 }, { "op3Ratio", 1 }, { "op4Ratio", 10 },
                                                            { "op1Level", 1 }, { "op2Level", 0.18f }, { "op3Level", 0.15f }, { "op4Level", 0.05f },
                                                            { "op1Decay", 4000 }, { "op1Sustain", 0 }, { "op2Decay", 700 }, { "op2Sustain", 0.05f },
                                                            { "op3Decay", 2000 }, { "op3Sustain", 0 }, { "op4Decay", 100 }, { "op4Sustain", 0 },
                                                            { "sustain", 0 }, { "decay", 4500 }, { "release", 900 },
                                                            { "tremolo", 0.4f }, { "tremRate", 5.4f }, { "reverb", 0.3f }, { "fxFocus", 1 } } }) },
            { "Toy Celesta", "Bells", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 8 }, { "op3Ratio", 2 }, { "op4Ratio", 1 },
                                                        { "op1Level", 1 }, { "op2Level", 0.25f }, { "op3Level", 0.3f }, { "op4Level", 0.1f },
                                                        { "op1Decay", 1500 }, { "op1Sustain", 0 }, { "op2Decay", 80 }, { "op2Sustain", 0 },
                                                        { "op3Decay", 800 }, { "op3Sustain", 0 }, { "op4Decay", 300 }, { "op4Sustain", 0 },
                                                        { "sustain", 0 }, { "decay", 1600 }, { "release", 900 },
                                                        { "perf", Daydream::perfHarp }, { "harpMs", 18 }, { "harpOctaves", 0 },
                                                        { "delay", 0.25f }, { "delayTime", 2 }, { "reverb", 0.3f }, { "fxFocus", 6 } } }) },
            // ---- motion ------------------------------------------------------------------------------------
            { "Night Drive Arp", "Arps & Patterns", { { "waveA", 0 }, { "waveB", 2 }, { "pw", 0.3f }, { "mixB", 0.35f }, { "cutoff", 1100 }, { "reso", 0.35f },
                                                      { "envAmount", 0.5f }, { "fDecay", 200 }, { "fSustain", 0.1f }, { "decay", 400 }, { "sustain", 0.3f },
                                                      { "release", 160 }, { "perf", Daydream::perfArp2 }, { "arpRate", 3 }, { "arpOrder", 2 }, { "gate", 0.5f },
                                                      { "delay", 0.35f }, { "delayTime", 3 }, { "delayFeedback", 0.45f }, { "reverb", 0.2f }, { "fxFocus", 6 } } },
            { "Motorik Chug", "Arps & Patterns", { { "waveA", 1 }, { "waveB", 0 }, { "mixB", 0.5f }, { "cutoff", 900 }, { "envAmount", 0.45f }, { "fDecay", 140 },
                                                   { "fSustain", 0.15f }, { "decay", 250 }, { "sustain", 0.5f }, { "release", 90 },
                                                   { "perf", Daydream::perfPattern }, { "pattern", 5 }, { "phaser", 0.3f }, { "phaserRate", 0.15f }, { "fxFocus", 2 } } },
            { "Rising Fives", "Arps & Patterns", join ({ pluck, { { "perf", Daydream::perfPattern }, { "pattern", 6 }, { "delay", 0.22f }, { "delayTime", 2 },
                                                                 { "reverb", 0.2f }, { "fxFocus", 6 } } }) },
            { "Waltz Box", "Arps & Patterns", join ({ reedBase, { { "bark", 0.3f }, { "perf", Daydream::perfPattern }, { "pattern", 13 }, { "reverb", 0.25f },
                                                                 { "tremolo", 0.15f }, { "fxFocus", 7 } } }) },
            { "Gallop Synth", "Arps & Patterns", { { "waveA", 0 }, { "waveB", 2 }, { "pw", 0.4f }, { "mixB", 0.4f }, { "cutoff", 1300 }, { "envAmount", 0.4f },
                                                   { "fDecay", 160 }, { "decay", 300 }, { "sustain", 0.45f }, { "release", 100 },
                                                   { "perf", Daydream::perfPattern }, { "pattern", 7 }, { "drive", 0.25f }, { "reverb", 0.15f }, { "fxFocus", 0 } } },
            { "Arp Rain", "Arps & Patterns", join ({ fmBase, { { "algorithm", 2 }, { "op1Ratio", 1 }, { "op2Ratio", 3.5f }, { "op3Ratio", 2 }, { "op4Ratio", 5 },
                                                              { "op1Level", 1 }, { "op2Level", 0.35f }, { "op3Level", 0.3f }, { "op4Level", 0.15f },
                                                              { "op1Decay", 1500 }, { "op1Sustain", 0 }, { "op2Decay", 400 }, { "op2Sustain", 0 },
                                                              { "op3Decay", 900 }, { "op3Sustain", 0 }, { "op4Decay", 200 }, { "op4Sustain", 0 },
                                                              { "sustain", 0 }, { "decay", 1800 }, { "release", 700 },
                                                              { "perf", Daydream::perfArp2 }, { "arpRate", 3 }, { "arpOrder", 4 }, { "gate", 0.8f },
                                                              { "delay", 0.4f }, { "delayTime", 3 }, { "reverb", 0.5f }, { "fxFocus", 7 } } }) },
            // ---- textures -----------------------------------------------------------------------------------
            { "Cassette Memory", "Textures", join ({ reedBase, { { "bark", 0.3f }, { "reedTone", 0.25f }, { "drift", 18 }, { "vibrato", 12 }, { "lfoRate", 0.6f },
                                                                { "lfoDelay", 0 }, { "chorus", 0.4f }, { "drive", 0.15f }, { "driveTone", 0.2f }, { "reverb", 0.2f }, { "fxFocus", 4 } } }) },
            { "Underwater Organ", "Textures", join ({ organ, { { "cutoff", 2200 }, { "phaser", 0.6f }, { "phaserRate", 0.18f }, { "phaserFeedback", 0.7f },
                                                               { "reverb", 0.4f }, { "fxFocus", 2 } } }) },
            { "Jet Stream Chords", "Textures", { { "waveA", 0 }, { "waveB", 0 }, { "unison", 2 }, { "detune", 10 }, { "cutoff", 4500 }, { "envAmount", 0.1f },
                                                 { "sustain", 0.9f }, { "release", 600 }, { "flanger", 0.7f }, { "flangerRate", 0.15f }, { "flangerFeedback", 0.7f },
                                                 { "reverb", 0.2f }, { "fxFocus", 3 } } },
            { "Surf Tremolo Keys", "Textures", { { "waveA", 3 }, { "waveB", 1 }, { "mixB", 0.3f }, { "cutoff", 3500 }, { "envAmount", 0.15f }, { "decay", 1800 },
                                                 { "sustain", 0.4f }, { "release", 400 }, { "tremolo", 0.7f }, { "tremRate", 7 }, { "reverb", 0.4f },
                                                 { "reverbSize", 0.45f }, { "reverbDamp", 0.2f }, { "fxFocus", 1 } } },
            { "Distant Radio", "Textures", { { "waveA", 0 }, { "waveB", 1 }, { "filterType", 2 }, { "cutoff", 1300 }, { "reso", 0.3f }, { "envAmount", 0 },
                                             { "sustain", 0.85f }, { "drive", 0.4f }, { "driveTone", 0.3f }, { "noise", 0.04f }, { "trim", 3 }, { "fxFocus", 0 } } },
            { "Hymn in a Cave", "Textures", join ({ organ, { { "attack", 450 }, { "release", 1500 }, { "op4Level", 0.1f }, { "reverb", 0.8f }, { "reverbSize", 1 },
                                                             { "reverbDamp", 0.6f }, { "fxFocus", 7 } } }) },
            { "Pure Sine Chords", "Textures", join ({ organ, { { "op1Level", 0 }, { "op3Level", 0 }, { "op4Level", 0 }, { "attack", 20 }, { "release", 250 }, { "fxFocus", 7 } } }) },
            { "Init (Dry Analogue)", "Textures", {} },
        };
        return p;
    }

    // Loudness trims (dB) measured on the render progression so every sound sits near -18 dB RMS.
    const float presetTrims[] = { 2.0f, 2.5f, 2.5f, 1.0f, -1.5f, -8.5f, -7.5f, 2.5f, -4.0f, -3.0f, -9.5f, -3.5f, -3.0f, -7.0f, -3.0f, -3.0f, -2.0f, 0, -5.5f, 2.0f, -1.5f, 0, 0.5f, 1.5f, 4.0f, 6.0f, 1.0f, 4.0f, 0, 3.0f, 2.5f, 6.0f, -1.0f, -3.0f, 4.0f, -3.5f, 1.0f, 3.0f, -7.0f, -3.0f, 3.0f, -3.5f, -10.5f, -7.0f, -4.5f };

    // Settings that stay put when the sound changes (they're yours, not the sound's).
    const juce::StringArray& globalIds()
    {
        static const juce::StringArray ids { "volume", "position", "centre", "spread", "voicing", "bassVoicing", "sus2", "keyMode", "key", "scale",
                                             "followSongKey", "transpose", "buttonZone", "extAdd", "hold", "bassMode", "bassSound", "bassVolume",
                                             "bassOctave", "bassGlide", "bassTone", "tempoSrc", "bpm", "beat", "beatKit", "beatVolume", "beatOn",
                                             "loopLength", "lockPerf", "lockFx" };
        return ids;
    }
    const juce::StringArray& perfIds()
    {
        static const juce::StringArray ids { "perf", "strumMs", "strumDir", "slop", "arpRate", "arpOrder", "gate", "pattern", "harpMs", "harpOctaves", "swing" };
        return ids;
    }
    const juce::StringArray& fxIds()
    {
        static const juce::StringArray ids { "fxFocus", "drive", "driveTone", "tremolo", "tremRate", "tremPan", "phaser", "phaserRate", "phaserFeedback",
                                             "flanger", "flangerRate", "flangerFeedback", "chorus", "chorusRate", "chorusDepth", "ensemble", "delay",
                                             "delayTime", "delayFeedback", "delayTone", "pingPong", "reverb", "reverbSize", "reverbDamp" };
        return ids;
    }

    const double arpRateBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
}

juce::StringArray Daydream::presetNames()
{
    juce::StringArray n;
    for (auto& p : presets()) n.add (p.name);
    return n;
}

juce::String Daydream::presetCategory (int i)
{
    return juce::isPositiveAndBelow (i, (int) presets().size()) ? presets()[(size_t) i].category : "";
}

void Daydream::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    const bool keepPerf = param ("lockPerf") > 0.5f, keepFx = param ("lockFx") > 0.5f;
    auto kept = [&] (const juce::String& id)
    {
        return globalIds().contains (id) || (keepPerf && perfIds().contains (id)) || (keepFx && fxIds().contains (id));
    };
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! kept (rp->getParameterID()))
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : presets()[(size_t) index].values)
        if (! kept (id)) setParam (id, v);
    constexpr int numTrims = (int) (sizeof (presetTrims) / sizeof (presetTrims[0]));
    if (index < numTrims && presetTrims[index] != 0.0f) setParam ("trim", param ("trim") + presetTrims[index]);
}

Daydream::Daydream() : BuiltinProcessor ("daydream", "Daydream", true, daydreamLayout())
{
    for (int i = 0; i <= maxLayers; ++i)
    {
        players[(size_t) i].owner = i;
        players[(size_t) i].held.reserve (32);
        players[(size_t) i].singles.reserve (32);
        players[(size_t) i].sounding.reserve (64);
        players[(size_t) i].events.reserve (256);
        players[(size_t) i].notes.reserve (48);
        players[(size_t) i].seq.reserve (96);
        players[(size_t) i].chord.intervals.reserve (16);
    }
    for (auto& l : layers) l.reserve (4096);
    loadProgram (0);
}

Daydream::~Daydream() = default;

void Daydream::prepareToPlay (double sampleRate, int blockSize)
{
    sr = sampleRate;
    sp.sampleRate = sampleRate;
    for (auto& v : voices) v.kill();
    for (auto& p : players) { p.reset(); p.midiMask = p.midiMaskRaw = 0; }
    lastLiveMask = 0;
    bass.prepare (sampleRate);
    fx.prepare (sampleRate, juce::jmax (blockSize, 4096));
    drums.prepare (sampleRate);
    work.setSize (4, juce::jmax (blockSize, 4096), false, true, true);
    loopState = loopIdle; numLayers = 0; recordingLayer = -1;
    grooveWasOn = false;
    dcL = dcR = dcInL = dcInR = 0.0f;
}

void Daydream::saveExtraState (juce::ValueTree& t)
{
    t.setProperty ("latched", (int) latched.load(), nullptr);
}

void Daydream::loadExtraState (const juce::ValueTree& t)
{
    latched = (unsigned) (int) t.getProperty ("latched", 0);
}

// ---- editor calls ------------------------------------------------------------------------------------------

void Daydream::toggleLatched (int button)
{
    if (juce::isPositiveAndBelow (button, (int) numButtons)) latched.fetch_xor (1u << button);
}

void Daydream::uiNote (int note, bool on, float velocity)
{
    const int w = uiWrite.load();
    const int next = (w + 1) % (int) uiQueue.size();
    if (next == uiRead.load()) return;   // full
    uiQueue[(size_t) w] = { note, on, velocity };
    uiWrite.store (next);
}

void Daydream::loopButton()  { loopCmd.store (1); }
void Daydream::overdubButton() { loopCmd.store (2); }
void Daydream::loopUndo()    { loopCmd.store (3); }
void Daydream::loopClear()   { loopCmd.store (4); }

Daydream::Display Daydream::getDisplay() const
{
    const juce::SpinLock::ScopedLockType l (displayLock);
    return display;
}

void Daydream::getScope (float* dest, int n) const
{
    const int size = (int) scope.size();
    const int w = scopeWrite.load();
    for (int i = 0; i < n; ++i)
        dest[i] = scope[(size_t) (((w - n + i) % size + size) % size)].load (std::memory_order_relaxed);
}

int Daydream::getKeyTonic() const
{
    return param ("followSongKey") > 0.5f ? juce::jlimit (0, 11, songKey.load()) : (int) param ("key");
}

int Daydream::getKeyScale() const
{
    return param ("followSongKey") > 0.5f ? (songScale.load() == 1 ? 1 : 0) : (int) param ("scale");
}

int Daydream::buttonNote (int button) const
{
    const int zone = (int) param ("buttonZone");
    if (zone >= 3 || ! juce::isPositiveAndBelow (button, (int) numButtons)) return -1;
    static const int whites[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
    return 24 + 12 * zone + whites[button];
}

// ---- chords --------------------------------------------------------------------------------------------------

bool Daydream::makeChord (const Player&, int key, unsigned mask, Chord& out) const
{
    const int t = juce::roundToInt (param ("transpose"));
    const int pc = ((key % 12) + 12) % 12;
    if ((mask & typeMask) != 0)
    {
        out.root = ((pc + t) % 12 + 12) % 12;
        out.intervals = intervalsFor (mask, param ("sus2") > 0.5f);
        return true;
    }
    if (param ("keyMode") > 0.5f)
    {
        auto c = inKey (pc, getKeyTonic(), getKeyScale(), mask);
        out.root = ((c.root + t) % 12 + 12) % 12;
        out.intervals = c.intervals;
        return true;
    }
    return false;
}

Chord Daydream::chordFor (int note, unsigned buttons) const
{
    Chord c;
    if (! makeChord (players[0], note, buttons, c)) { c.root = ((note % 12) + 12) % 12; c.intervals.clear(); }
    return c;
}

std::vector<int> Daydream::notesFor (const Chord& c) const
{
    return place (c, (int) param ("position"), juce::roundToInt (param ("centre")), (int) param ("spread"), juce::roundToInt (param ("voicing")));
}

unsigned Daydream::effectiveMask (const Player& p) const { return p.midiMask; }

juce::int64 Daydream::samplesPerStep (int rateIndex) const
{
    return juce::jmax ((juce::int64) 16, (juce::int64) beatsToSamples (arpRateBeats[juce::jlimit (0, 5, rateIndex)]));
}

void Daydream::synthOn (Player& p, int note, float vel)
{
    if (! juce::isPositiveAndBelow (note, 128)) return;
    for (auto& v : voices)
        if (v.active && ! v.released && v.owner == p.owner && v.note == note) v.release (sp);
    daydream::Voice* pick = nullptr;
    for (auto& v : voices) if (! v.active) { pick = &v; break; }
    if (pick == nullptr)
    {
        // steal: the quietest released voice, else the oldest
        float quietest = 1e9f;
        for (auto& v : voices) if (v.released && v.level() < quietest) { quietest = v.level(); pick = &v; }
        if (pick == nullptr)
        {
            pick = &voices[0];
            for (auto& v : voices) if (v.order < pick->order) pick = &v;
        }
    }
    const float pan = juce::jlimit (-1.0f, 1.0f, (note - 60) / 30.0f + ((voiceCounter & 1) ? 0.12f : -0.12f));
    pick->start (p.owner, note, (float) note, vel, sp, ++voiceCounter, pan);
}

void Daydream::synthOff (Player& p, int note)
{
    for (auto& v : voices)
        if (v.active && ! v.released && v.owner == p.owner && v.note == note)
        {
            if (p.sustain) v.sustained = true;
            else v.release (sp);
        }
}

void Daydream::publish (const Player& p)
{
    const juce::SpinLock::ScopedTryLockType l (displayLock);
    if (! l.isLocked()) return;
    display.chord = p.chordOn;
    display.buttons = p.midiMask;
    if (p.chordOn)
    {
        display.root = p.chord.root;
        display.numIntervals = juce::jmin ((int) p.chord.intervals.size(), 12);
        for (int i = 0; i < display.numIntervals; ++i) display.intervals[(size_t) i] = p.chord.intervals[(size_t) i];
        display.numNotes = juce::jmin ((int) p.notes.size(), 48);
        for (int i = 0; i < display.numNotes; ++i) display.notes[(size_t) i] = p.notes[(size_t) i];
        display.bass = (int) param ("bassMode") != bassOff ? p.bass : -1;
        display.inKey = (p.midiMask & typeMask) == 0;
    }
    else
    {
        display.numIntervals = 0;
        display.numNotes = juce::jmin ((int) p.singles.size(), 48);
        for (int i = 0; i < display.numNotes; ++i) display.notes[(size_t) i] = p.singles[(size_t) i];
        display.bass = -1;
        display.inKey = false;
    }
    ++display.serial;
}

void Daydream::startChord (Player& p, int key, const Chord& c, float vel, bool)
{
    if (p.chordOn) stopChord (p);
    p.chordOn = true;
    p.rootKey = key;
    p.chord = c;
    p.vel = vel;
    p.notes = notesFor (c);
    p.bass = bassNote (c, (int) param ("bassOctave") + 1, juce::roundToInt (param ("bassVoicing")));
    p.events.clear();
    p.step = 0;
    p.gridAt = now;
    p.lastRandom = -1;

    const int mode = (int) param ("bassMode");
    const int perf = (int) param ("perf");
    const bool chordSound = mode != bassOnly;
    auto schedule = [&] (juce::int64 at, int note, float v, bool on) { p.events.push_back ({ at, note, v, on }); };

    if (chordSound)
    {
        switch (perf)
        {
            case perfStrum: case perfStrum2:
            {
                std::vector<int> list = p.notes;
                if (perf == perfStrum2) for (int n : p.notes) list.push_back (n + 12);
                std::sort (list.begin(), list.end());
                list.erase (std::unique (list.begin(), list.end()), list.end());
                const int dir = (int) param ("strumDir");
                bool up = dir == 0 || (dir == 2 && p.strumUp);
                if (dir == 2) p.strumUp = ! p.strumUp;
                if (! up) std::reverse (list.begin(), list.end());
                const double spacing = param ("strumMs") * 0.001 * sr;
                for (size_t i = 0; i < list.size(); ++i)
                    schedule (now + (juce::int64) (spacing * (double) i), list[i], vel * (1.0f - 0.03f * (float) i), true);
                break;
            }
            case perfSlop:
            {
                const float slop = param ("slop");
                for (int n : p.notes)
                {
                    const double delay = rnd.nextDouble() * slop * 0.11 * sr;
                    const float v = juce::jlimit (0.05f, 1.0f, vel * (1.0f + (rnd.nextFloat() - 0.5f) * 0.45f * slop));
                    schedule (now + (juce::int64) delay, n, v, true);
                }
                break;
            }
            case perfArp: case perfArp2: case perfPattern:
                break;   // runPlayer plays the steps
            case perfHarp:
            {
                std::vector<int> ladder;
                const int octs = (int) param ("harpOctaves") + 2;
                for (int o = 0; o < octs; ++o) for (int n : p.notes) ladder.push_back (n + 12 * o);
                std::sort (ladder.begin(), ladder.end());
                ladder.erase (std::unique (ladder.begin(), ladder.end()), ladder.end());
                const double spacing = param ("harpMs") * 0.001 * sr;
                for (size_t i = 0; i < ladder.size(); ++i)
                    if (ladder[i] <= 108) schedule (now + (juce::int64) (spacing * (double) i), ladder[i], vel * (1.0f - 0.25f * (float) i / (float) ladder.size()), true);
                break;
            }
            default:
                for (int n : p.notes) synthOn (p, n, vel), p.sounding.push_back (n);
                break;
        }
    }
    if (mode != bassOff && p.bass >= 0)
        bass.noteOn (p.owner, p.bass, vel, (int) param ("bassSound"), param ("bassGlide"));
    publish (p);
}

void Daydream::stopChord (Player& p)
{
    p.events.clear();
    for (int n : p.sounding) synthOff (p, n);
    p.sounding.clear();
    if (p.bass >= 0) bass.noteOff (p.owner, p.bass);
    p.chordOn = false;
    p.rootKey = -1;
}

void Daydream::updateChord (Player& p, bool revoice)
{
    Chord c;
    if (! p.chordOn || ! makeChord (p, p.rootKey, p.midiMask, c)) return;
    if (! revoice && c.root == p.chord.root && c.intervals == p.chord.intervals) { publish (p); return; }
    const int perf = (int) param ("perf");
    const bool retrigger = (! revoice && (int) param ("extAdd") == 1) || perf == perfStrum2 || perf == perfHarp;
    if (retrigger) { startChord (p, p.rootKey, c, p.vel, true); return; }

    auto newNotes = notesFor (c);
    if (perf == perfChord || perf == perfStrum || perf == perfSlop)
    {
        if ((int) param ("bassMode") != bassOnly)
        {
            for (int n : p.sounding)
                if (std::find (newNotes.begin(), newNotes.end(), n) == newNotes.end()) synthOff (p, n);
            std::vector<int> keep;
            for (int n : newNotes)
            {
                if (std::find (p.sounding.begin(), p.sounding.end(), n) == p.sounding.end()) synthOn (p, n, p.vel);
                keep.push_back (n);
            }
            p.sounding = keep;
            p.events.clear();
        }
    }
    p.chord = c;
    p.notes = newNotes;
    const int newBass = bassNote (c, (int) param ("bassOctave") + 1, juce::roundToInt (param ("bassVoicing")));
    if ((int) param ("bassMode") != bassOff && newBass != p.bass)
    {
        bass.noteOn (p.owner, newBass, p.vel, (int) param ("bassSound"), param ("bassGlide"));
    }
    p.bass = newBass;
    publish (p);
}

void Daydream::handleRootOn (Player& p, int key, float vel, bool rec)
{
    if (rec) record (0, key, vel);
    p.held.erase (std::remove (p.held.begin(), p.held.end(), key), p.held.end());
    p.held.push_back (key);
    Chord c;
    if (makeChord (p, key, p.midiMask, c))
    {
        startChord (p, key, c, vel, true);
        return;
    }
    // a single note
    const int mode = (int) param ("bassMode");
    const int pitch = key + juce::roundToInt (param ("transpose"));
    if (mode != bassOnly && juce::isPositiveAndBelow (pitch, 128)) synthOn (p, pitch, vel);
    p.singles.erase (std::remove (p.singles.begin(), p.singles.end(), pitch), p.singles.end());
    p.singles.push_back (pitch);
    if (mode == bassAlways || mode == bassOnly)
    {
        Chord one { ((pitch % 12) + 12) % 12, { 0 } };
        bass.noteOn (p.owner, bassNote (one, (int) param ("bassOctave") + 1, 0), vel, (int) param ("bassSound"), param ("bassGlide"));
    }
    if (! p.chordOn) publish (p);
}

void Daydream::handleRootOff (Player& p, int key, bool rec)
{
    if (rec) record (1, key, 0.0f);
    p.held.erase (std::remove (p.held.begin(), p.held.end(), key), p.held.end());

    if (p.chordOn && key == p.rootKey)
    {
        if (param ("hold") > 0.5f) return;
        Chord c;
        if (! p.held.empty() && makeChord (p, p.held.back(), p.midiMask, c))
            startChord (p, p.held.back(), c, p.vel, true);
        else
        {
            stopChord (p);
            publish (p);
        }
        return;
    }
    const int pitch = key + juce::roundToInt (param ("transpose"));
    auto it = std::find (p.singles.begin(), p.singles.end(), pitch);
    if (it != p.singles.end())
    {
        p.singles.erase (it);
        synthOff (p, pitch);
        if (p.singles.empty() && ! p.chordOn) bass.allOff (p.owner);
        if (! p.chordOn) publish (p);
    }
}

void Daydream::handleMask (Player& p, unsigned mask)
{
    if (mask == p.midiMask) return;
    p.midiMask = mask;
    if (p.chordOn) { updateChord (p); return; }
    // buttons pressed after the key: turn the held note into a chord
    Chord c;
    if (! p.held.empty() && makeChord (p, p.held.back(), mask, c))
    {
        const int key = p.held.back();
        const int pitch = key + juce::roundToInt (param ("transpose"));
        auto it = std::find (p.singles.begin(), p.singles.end(), pitch);
        if (it != p.singles.end()) { p.singles.erase (it); synthOff (p, pitch); }
        startChord (p, key, c, p.vel, true);
    }
    else if (! p.chordOn) publish (p);
}

void Daydream::playStepNotes (Player& p, juce::int64 at)
{
    const int perf = (int) param ("perf");
    if (p.notes.empty()) return;
    const juce::int64 off = at - now;   // when inside this chunk
    juce::ignoreUnused (off);
    if (perf == perfArp || perf == perfArp2)
    {
        // the sequence
        std::vector<int>& seq = p.seq;
        seq.clear();
        std::vector<int> base = p.notes;
        if (perf == perfArp2) for (int n : p.notes) base.push_back (n + 12);
        std::sort (base.begin(), base.end());
        base.erase (std::unique (base.begin(), base.end()), base.end());
        const int order = (int) param ("arpOrder");
        switch (order)
        {
            case 1: seq.assign (base.rbegin(), base.rend()); break;
            case 2: seq = base; for (int i = (int) base.size() - 2; i >= 1; --i) seq.push_back (base[(size_t) i]); break;
            case 3: seq.assign (base.rbegin(), base.rend()); for (size_t i = 1; i + 1 < base.size(); ++i) seq.push_back (base[i]); break;
            case 5: { int lo = 0, hi = (int) base.size() - 1; while (lo <= hi) { seq.push_back (base[(size_t) lo++]); if (lo <= hi) seq.push_back (base[(size_t) hi--]); } break; }
            default: seq = base; break;
        }
        int note;
        if (order == 4)
        {
            int idx = rnd.nextInt ((int) base.size());
            if (base.size() > 1 && idx == p.lastRandom) idx = (idx + 1 + rnd.nextInt ((int) base.size() - 1)) % (int) base.size();
            p.lastRandom = idx;
            note = base[(size_t) idx];
        }
        else note = seq[(size_t) (p.step % (int) seq.size())];
        const float accent = (p.step % (int) juce::jmax ((size_t) 1, seq.size())) == 0 ? 1.0f : 0.86f;
        const juce::int64 len = (juce::int64) ((double) samplesPerStep ((int) param ("arpRate")) * juce::jlimit (0.05f, 1.0f, param ("gate")));
        synthOn (p, note, p.vel * accent);
        if (std::find (p.sounding.begin(), p.sounding.end(), note) == p.sounding.end()) p.sounding.push_back (note);
        p.events.push_back ({ at + len, note, 0.0f, false });
    }
    else if (perf == perfPattern)
    {
        const auto& pats = patterns();
        const auto& pat = pats[(size_t) juce::jlimit (0, (int) pats.size() - 1, (int) param ("pattern"))];
        const int s = p.step % juce::jmax (1, pat.steps);
        const double unit = beatsToSamples (4.0 / juce::jmax (1, pat.steps));
        const int n = (int) p.notes.size();
        for (auto& [step, ev] : pat.events)
        {
            if (step != s) continue;
            const juce::int64 len = (juce::int64) (unit * ev.length * 0.92);
            auto play = [&] (int note)
            {
                if (! juce::isPositiveAndBelow (note, 128)) return;
                synthOn (p, note, p.vel * ev.velocity);
                if (std::find (p.sounding.begin(), p.sounding.end(), note) == p.sounding.end()) p.sounding.push_back (note);
                p.events.push_back ({ at + len, note, 0.0f, false });
            };
            for (int idx : ev.notes)
            {
                if (idx == 99) for (int x : p.notes) play (x);
                else if (idx < 0) play (p.bass + 12);
                else play (p.notes[(size_t) (idx % n)] + 12 * (idx / n));
            }
        }
    }
    ++p.step;
}

void Daydream::runPlayer (Player& p, juce::int64 from, juce::int64 to)
{
    juce::ignoreUnused (from);
    const int perf = (int) param ("perf");
    const bool chordSound = (int) param ("bassMode") != bassOnly;
    // rhythmic modes: play the steps that fall in this chunk
    if (p.chordOn && chordSound && (perf == perfArp || perf == perfArp2 || perf == perfPattern))
    {
        const auto& pats = patterns();
        const juce::int64 stepLen = perf == perfPattern
            ? juce::jmax ((juce::int64) 16, (juce::int64) beatsToSamples (4.0 / juce::jmax (1, pats[(size_t) juce::jlimit (0, (int) pats.size() - 1, (int) param ("pattern"))].steps)))
            : samplesPerStep ((int) param ("arpRate"));
        const double swing = param ("swing");
        for (int guard = 0; guard < 64; ++guard)
        {
            const juce::int64 shift = (p.step % 2 == 1) ? (juce::int64) (swing * 0.5 * (double) stepLen) : 0;
            if (p.gridAt + shift >= to) break;
            playStepNotes (p, p.gridAt + shift);
            p.gridAt += stepLen;
        }
    }
    // scheduled notes
    if (! p.events.empty())
    {
        for (size_t i = 0; i < p.events.size();)
        {
            const auto e = p.events[i];
            if (e.at < to)
            {
                p.events.erase (p.events.begin() + (std::ptrdiff_t) i);
                if (e.on)
                {
                    synthOn (p, e.note, e.vel);
                    if (std::find (p.sounding.begin(), p.sounding.end(), e.note) == p.sounding.end()) p.sounding.push_back (e.note);
                }
                else
                {
                    // an arp / pattern note ends (unless the same note started again since)
                    bool restarted = false;
                    for (auto& o : p.events) if (o.on && o.note == e.note) restarted = true;
                    synthOff (p, e.note);
                    if (! restarted) p.sounding.erase (std::remove (p.sounding.begin(), p.sounding.end(), e.note), p.sounding.end());
                }
            }
            else ++i;
        }
    }
}

// ---- the looper ------------------------------------------------------------------------------------------------

void Daydream::record (int type, int a, float b)
{
    if (loopState == loopArmed && type == 0)
    {
        // free mode: recording starts with the first note
        loopState = loopRecording;
        loopPos = 0.0;
        loopLength = 0.0;
        numLayers = 1;
        recordingLayer = 0;
        layers[0].clear();
        layers[0].push_back ({ 0.0, 2, (int) players[0].midiMask, 0.0f });
    }
    if ((loopState != loopRecording && loopState != loopOverdub) || recordingLayer < 0) return;
    auto& l = layers[(size_t) recordingLayer];
    if (l.size() >= l.capacity()) return;
    const LoopEvent e { loopPos, type, a, b };
    l.insert (std::upper_bound (l.begin(), l.end(), e, [] (const LoopEvent& x, const LoopEvent& y) { return x.beat < y.beat; }), e);
}

void Daydream::finishRecording()
{
    if (recordingLayer < 0) return;
    auto& l = layers[(size_t) recordingLayer];
    if (loopState == loopRecording && loopLength <= 0.0) loopLength = juce::jmax (0.5, loopPos);
    // notes still held when recording stops end just before the loop comes round
    const double endBeat = loopState == loopOverdub ? loopPos : loopLength - 1.0e-4;
    for (int key : players[0].held)
        if (l.size() < l.capacity())
            l.insert (std::upper_bound (l.begin(), l.end(), endBeat, [] (double b, const LoopEvent& x) { return b < x.beat; }), LoopEvent { endBeat, 1, key, 0.0f });
    recordingLayer = -1;
}

void Daydream::loopCommand (int cmd)
{
    auto releaseLoopPlayers = [this] (int fromLayer)
    {
        for (int i = fromLayer; i < maxLayers; ++i)
        {
            auto& pl = players[(size_t) (1 + i)];
            stopChord (pl);
            for (int n : pl.singles) synthOff (pl, n);
            pl.reset();
            pl.midiMask = 0;
            bass.allOff (pl.owner);
        }
    };
    const int bars[] = { 0, 1, 2, 4, 8, 16 };
    switch (cmd)
    {
        case 1:
            switch (loopState)
            {
                case loopIdle:
                {
                    const int lengthChoice = (int) param ("loopLength");
                    numLayers = 0;
                    if (lengthChoice == 0) { loopState = loopArmed; loopLength = 0.0; }
                    else { loopState = loopCountIn; countIn = 4.0; countClicks = 0; loopLength = 4.0 * bars[juce::jlimit (1, 5, lengthChoice)]; }
                    break;
                }
                case loopArmed: case loopCountIn: loopState = loopIdle; break;
                case loopRecording: finishRecording(); loopState = loopPlaying; loopPos = 0.0; break;
                case loopOverdub: finishRecording(); loopState = loopPlaying; break;
                case loopPlaying: releaseLoopPlayers (0); loopState = loopPaused; break;
                case loopPaused: loopPos = 0.0; loopState = loopPlaying; break;
                default: break;
            }
            break;
        case 2:
            if (loopState == loopPlaying && numLayers < maxLayers)
            {
                recordingLayer = numLayers++;
                layers[(size_t) recordingLayer].clear();
                loopState = loopOverdub;
                layers[(size_t) recordingLayer].push_back ({ loopPos, 2, (int) players[0].midiMask, 0.0f });
                for (int key : players[0].held) record (0, key, players[0].vel);
            }
            else if (loopState == loopOverdub) { finishRecording(); loopState = loopPlaying; }
            break;
        case 3:
            if (loopState == loopOverdub) { layers[(size_t) recordingLayer].clear(); recordingLayer = -1; --numLayers; loopState = loopPlaying; }
            else if (numLayers > 1) { --numLayers; releaseLoopPlayers (numLayers); layers[(size_t) numLayers].clear(); }
            else { releaseLoopPlayers (0); numLayers = 0; loopState = loopIdle; }
            break;
        case 4:
            releaseLoopPlayers (0);
            numLayers = 0; recordingLayer = -1; loopState = loopIdle;
            break;
        default: break;
    }
}

void Daydream::runLoop (double beats)
{
    switch (loopState)
    {
        case loopCountIn:
        {
            // a click on each beat of the count-in (the first one louder)
            const int due = juce::jmin (4, (int) std::floor (4.0 - countIn + 1.0e-9) + 1);
            while (countClicks < due) { drums.trigger (75, countClicks == 0 ? 1.0f : 0.7f); ++countClicks; }
            countIn -= beats;
            if (countIn <= 0.0)
            {
                loopState = loopRecording;
                loopPos = -countIn;
                numLayers = 1;
                recordingLayer = 0;
                layers[0].clear();
                layers[0].push_back ({ 0.0, 2, (int) players[0].midiMask, 0.0f });
                for (int key : players[0].held) layers[0].push_back ({ 0.0, 0, key, players[0].vel });
            }
            break;
        }
        case loopRecording:
            loopPos += beats;
            if (loopLength > 0.0 && loopPos >= loopLength)
            {
                finishRecording();
                loopState = loopPlaying;
                loopPos -= loopLength;
            }
            else if (loopLength <= 0.0 && loopPos >= 256.0)
            {
                finishRecording();
                loopState = loopPlaying;
                loopPos = 0.0;
            }
            break;
        case loopPlaying: case loopOverdub:
        {
            if (loopLength <= 0.0) break;
            const double a = loopPos, b = loopPos + beats;
            auto dispatch = [&] (double from, double to)
            {
                for (int L = 0; L < numLayers; ++L)
                {
                    if (L == recordingLayer) continue;
                    auto& pl = players[(size_t) (1 + L)];
                    for (auto& e : layers[(size_t) L])
                    {
                        if (e.beat < from) continue;
                        if (e.beat >= to) break;
                        switch (e.type)
                        {
                            case 0: handleRootOn (pl, e.a, e.b, false); break;
                            case 1: handleRootOff (pl, e.a, false); break;
                            case 2: handleMask (pl, (unsigned) e.a); break;
                            case 3:
                                pl.sustain = e.a != 0;
                                if (! pl.sustain) for (auto& v : voices) if (v.active && v.owner == pl.owner && v.sustained) v.release (sp);
                                break;
                            default: break;
                        }
                    }
                }
            };
            if (b < loopLength) dispatch (a, b);
            else { dispatch (a, loopLength); dispatch (0.0, b - loopLength); }
            loopPos = std::fmod (b, loopLength);
            break;
        }
        default: break;
    }
    loopStateOut = (int) loopState;
    loopLayersOut = numLayers;
    loopBeatsOut = loopLength;
    loopProgressOut = loopState == loopCountIn ? (float) (1.0 - countIn / 4.0)
                    : loopLength > 0.0 ? (float) (loopPos / loopLength)
                    : (float) std::fmod (loopPos / 4.0, 1.0);
}

// ---- drum grooves ----------------------------------------------------------------------------------------------

void Daydream::runGroove (double from, double to, int chunkStart, int chunkLen)
{
    juce::ignoreUnused (chunkStart, chunkLen);
    const auto& gs = grooves();
    const int idx = (int) param ("beat") - 1;
    if (! juce::isPositiveAndBelow (idx, (int) gs.size())) return;
    const auto& g = gs[(size_t) idx];
    const double unit = 4.0 / g.stepsPerBar;
    const juce::int64 s0 = (juce::int64) std::floor (from / unit) - 1, s1 = (juce::int64) std::floor (to / unit) + 1;
    for (juce::int64 s = s0; s <= s1; ++s)
    {
        if (s < 0) continue;
        const double t = (double) s * unit + ((s % 2 == 1 && g.stepsPerBar == 16) ? g.swing * 0.5 * unit : 0.0);
        if (t < from || t >= to) continue;
        const int step = (int) (s % g.stepsPerBar);
        for (auto& lane : g.lanes)
        {
            const char c = lane.steps[step];
            const float v = c == 'X' ? 1.0f : c == 'x' ? 0.72f : c == 'o' ? 0.42f : 0.0f;
            if (v > 0.0f) drums.trigger (lane.note, v);
        }
    }
}

// ---- audio -----------------------------------------------------------------------------------------------------

void Daydream::readParams()
{
    auto P = [this] (const char* id) { return param (id); };
    sp.engine = (int) P ("engine");
    sp.waveA = (int) P ("waveA"); sp.waveB = (int) P ("waveB");
    sp.semiB = (float) juce::roundToInt (P ("semiB")); sp.fineB = P ("fineB"); sp.mixB = P ("mixB");
    sp.sub = P ("sub"); sp.noise = P ("noise"); sp.pw = P ("pw"); sp.unison = (int) P ("unison") + 1; sp.detune = P ("detune");
    sp.algorithm = (int) P ("algorithm");
    static const char* ratio[] = { "op1Ratio", "op2Ratio", "op3Ratio", "op4Ratio" };
    static const char* level[] = { "op1Level", "op2Level", "op3Level", "op4Level" };
    static const char* att[] = { "op1Attack", "op2Attack", "op3Attack", "op4Attack" };
    static const char* dec[] = { "op1Decay", "op2Decay", "op3Decay", "op4Decay" };
    static const char* sus[] = { "op1Sustain", "op2Sustain", "op3Sustain", "op4Sustain" };
    for (int i = 0; i < 4; ++i)
    {
        sp.ratio[i] = P (ratio[i]); sp.level[i] = P (level[i]);
        sp.opAttack[i] = P (att[i]) * 0.001f; sp.opDecay[i] = P (dec[i]) * 0.001f; sp.opSustain[i] = P (sus[i]);
    }
    sp.feedback = P ("feedback"); sp.fmVelocity = P ("fmVelocity");
    sp.bark = P ("bark"); sp.reedTone = P ("reedTone"); sp.reedDecay = P ("reedDecay"); sp.mechanics = P ("mechanics");
    sp.filterType = (int) P ("filterType"); sp.cutoff = P ("cutoff"); sp.reso = P ("reso"); sp.envAmount = P ("envAmount");
    sp.keyTrack = P ("keyTrack"); sp.velCutoff = P ("velCutoff"); sp.drive = P ("synthDrive");
    sp.attack = P ("attack") * 0.001f; sp.decay = P ("decay") * 0.001f; sp.sustain = P ("sustain"); sp.release = P ("release") * 0.001f;
    sp.fAttack = P ("fAttack") * 0.001f; sp.fDecay = P ("fDecay") * 0.001f; sp.fSustain = P ("fSustain"); sp.fRelease = P ("fRelease") * 0.001f;
    sp.lfoRate = P ("lfoRate"); sp.lfoDelay = P ("lfoDelay") * 0.001f; sp.vibrato = P ("vibrato") + modWheel * 45.0f;
    sp.lfoFilter = P ("lfoFilter"); sp.lfoPwm = P ("lfoPwm"); sp.lfoAmp = P ("lfoAmp"); sp.drift = P ("drift");
    sp.velSens = P ("velSens"); sp.width = P ("width"); sp.bend = bendSemis;
    sp.sampleRate = sr;

    fxp.drive = P ("drive"); fxp.driveTone = P ("driveTone");
    fxp.tremolo = P ("tremolo"); fxp.tremRate = P ("tremRate"); fxp.tremPan = P ("tremPan") > 0.5f;
    fxp.phaser = P ("phaser"); fxp.phaserRate = P ("phaserRate"); fxp.phaserFeedback = P ("phaserFeedback");
    fxp.flanger = P ("flanger"); fxp.flangerRate = P ("flangerRate"); fxp.flangerFeedback = P ("flangerFeedback");
    fxp.chorus = P ("chorus"); fxp.chorusRate = P ("chorusRate"); fxp.chorusDepth = P ("chorusDepth");
    fxp.ensemble = P ("ensemble");
    fxp.delay = P ("delay"); fxp.delayDivision = (int) P ("delayTime"); fxp.delayFeedback = P ("delayFeedback");
    fxp.delayTone = P ("delayTone"); fxp.pingPong = P ("pingPong") > 0.5f;
    fxp.reverb = P ("reverb"); fxp.reverbSize = P ("reverbSize"); fxp.reverbDamp = P ("reverbDamp");
}

void Daydream::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    if (work.getNumSamples() < total) work.setSize (4, total, false, false, true);
    work.clear (0, total);

    // tempo
    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) hostPpq = *q;
            if (auto b = pos->getBpm()) hostBpm = *b;
        }
    bpm = (int) param ("tempoSrc") == 0 && hostBpm > 0.0 ? hostBpm : (double) param ("bpm");
    currentBpm = bpm;
    readParams();
    drums.setKit ((int) param ("beatKit"));

    // turning the voicing (or the position, spread, centre, transpose) moves the chords that are held
    {
        const std::array<int, 6> v { juce::roundToInt (param ("voicing")), juce::roundToInt (param ("bassVoicing")), (int) param ("position"),
                                     (int) param ("spread"), juce::roundToInt (param ("centre")), juce::roundToInt (param ("transpose")) };
        if (v != lastVoicing)
        {
            lastVoicing = v;
            for (auto& p : players) if (p.chordOn) updateChord (p, true);
        }
    }

    if (const int c = loopCmd.exchange (0)) loopCommand (c);

    auto& live = players[0];

    // the on-screen keyboard
    while (uiRead.load() != uiWrite.load())
    {
        const auto e = uiQueue[(size_t) uiRead.load()];
        uiRead.store ((uiRead.load() + 1) % (int) uiQueue.size());
        if (e.on) handleRootOn (live, e.note, e.vel, true); else handleRootOff (live, e.note, true);
    }

    const int zone = (int) param ("buttonZone");
    const int zoneLow = zone < 3 ? 24 + 12 * zone : -100;
    auto buttonFor = [&] (int note) -> int
    {
        if (note < zoneLow || note > zoneLow + 12) return -2;   // not in the zone
        static const int map[] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6, 7 };
        return map[note - zoneLow];
    };

    const bool beatOn = param ("beatOn") > 0.5f && (int) param ("beat") > 0;
    if (beatOn && ! grooveWasOn) grooveBeat = 0.0;
    grooveWasOn = beatOn;

    float* wl = work.getWritePointer (0);
    float* wr = work.getWritePointer (1);
    float* bl = work.getWritePointer (2);
    float* br = work.getWritePointer (3);
    auto it = midi.cbegin();
    const double beatsPerSample = bpm / 60.0 / sr;

    for (int pos = 0; pos < total; pos += 16)
    {
        const int n = juce::jmin (16, total - pos);
        for (; it != midi.cend() && (*it).samplePosition < pos + n; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn())
            {
                const int b = buttonFor (m.getNoteNumber());
                if (b >= 0) live.midiMaskRaw |= 1u << b;
                else if (b == -2) handleRootOn (live, m.getNoteNumber(), m.getFloatVelocity(), true);
            }
            else if (m.isNoteOff())
            {
                const int b = buttonFor (m.getNoteNumber());
                if (b >= 0) live.midiMaskRaw &= ~(1u << b);
                else if (b == -2) handleRootOff (live, m.getNoteNumber(), true);
            }
            else if (m.isSustainPedalOn() || m.isSustainPedalOff())
            {
                live.sustain = m.isSustainPedalOn();
                record (3, live.sustain ? 1 : 0, 0.0f);
                if (! live.sustain) for (auto& v : voices) if (v.active && v.owner == 0 && v.sustained) v.release (sp);
            }
            else if (m.isPitchWheel()) bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
            else if (m.isControllerOfType (1)) modWheel = (float) m.getControllerValue() / 127.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                for (auto& p : players) { stopChord (p); p.singles.clear(); p.held.clear(); }
                for (auto& v : voices) v.release (sp);
                bass.allOff();
            }
            const unsigned mm = live.midiMaskRaw | latched.load();
            heldButtons = mm;
            if (mm != lastLiveMask) { lastLiveMask = mm; record (2, (int) mm, 0.0f); handleMask (live, mm); }
        }
        {
            const unsigned mm = live.midiMaskRaw | latched.load();
            heldButtons = mm;
            if (mm != lastLiveMask) { lastLiveMask = mm; record (2, (int) mm, 0.0f); handleMask (live, mm); }
        }

        const double chunkBeats = n * beatsPerSample;
        runLoop (chunkBeats);
        if (beatOn)
        {
            const double from = hostPlaying && (int) param ("tempoSrc") == 0 ? hostPpq + pos * beatsPerSample : grooveBeat;
            runGroove (from, from + chunkBeats, pos, n);
            grooveBeat += chunkBeats;
        }
        for (auto& p : players) runPlayer (p, now, now + n);

        sp.bend = bendSemis;
        for (auto& v : voices) v.render (sp, wl + pos, wr + pos, n);
        bass.render (bl + pos, br + pos, n, (int) param ("bassSound"), param ("bassTone"), juce::Decibels::decibelsToGain (param ("bassVolume")));
        {
            // drums go in with the bass (dry)
            const float dg = juce::Decibels::decibelsToGain (param ("beatVolume"));
            float tmpL[16] {}, tmpR[16] {};
            drums.render (tmpL, tmpR, n);
            for (int i = 0; i < n; ++i) { bl[pos + i] += tmpL[i] * dg; br[pos + i] += tmpR[i] * dg; }
        }
        now += n;
    }

    fx.process (wl, wr, total, fxp, bpm);

    const float g = juce::Decibels::decibelsToGain (param ("volume") + param ("trim"));
    const float gb = juce::Decibels::decibelsToGain (param ("volume"));
    auto knee = [] (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
    };
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    int sw = scopeWrite.load();
    for (int i = 0; i < total; ++i)
    {
        float l = wl[i] * g + bl[i] * gb, r = wr[i] * g + br[i] * gb;
        // DC blocker
        const float yl = l - dcInL + 0.9995f * dcL; dcInL = l; dcL = yl;
        const float yr = r - dcInR + 0.9995f * dcR; dcInR = r; dcR = yr;
        l = knee (yl); r = knee (yr);
        if (outR != nullptr) { outL[i] = l; outR[i] = r; }
        else outL[i] = 0.5f * (l + r);
        if ((i & 1) == 0) { scope[(size_t) sw].store (0.5f * (l + r), std::memory_order_relaxed); sw = (sw + 1) % (int) scope.size(); }
    }
    scopeWrite = sw;

    bool on[128] {};
    for (auto& v : voices) if (v.active && ! v.released && juce::isPositiveAndBelow (v.note, 128)) on[v.note] = true;
    for (int i = 0; i < 128; ++i) soundingOut[(size_t) i].store (on[i], std::memory_order_relaxed);
    bassSoundingOut = bass.soundingNote();
    midi.clear();
}

} // namespace wis::daw
