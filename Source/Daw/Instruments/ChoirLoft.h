#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include <array>

namespace wis::daw
{

// ---------------------------------------------------------------------------------------------------------
//  Choir Loft: a choir (or one singer) built from people. Nothing is sampled.
//
//  Every singer is a small source-filter voice model:
//    vocal folds   an LF (Liljencrants-Fant) glottal flow-derivative pulse, shaped by Rd (pressed .. breathy),
//                  with per-cycle jitter and shimmer, aspiration noise gated by the open phase, rasp
//                  (alternating cycles + rough noise), registers (chest -> head above the break)
//    vocal tract   nasal pole/zero pair + five cascaded formant resonators. The formants come from a vowel
//                  chart (front/back x close/open), scaled by the singer's tract length and bent by their
//                  anatomy (jaw, lips, mouth, cheeks, neck, nose, head size); F1 follows the pitch on high
//                  notes the way trained singers tune it, and classical voices cluster F3-F5 into a "ring".
//  Who the singers are comes from the cast (how many, women / men, ages, diversity, a cast number to re-roll)
//  and can be edited one by one. They stand on a stage, are picked up by a modelled stereo pair (spaced omnis,
//  near-coincident or coincident cardioids, crossed figure-8s or one mic) and sit in a designed room.
// ---------------------------------------------------------------------------------------------------------
class ChoirLoft : public BuiltinProcessor, private juce::Timer
{
public:
    ChoirLoft();
    ~ChoirLoft() override;

    static constexpr int maxSingers = 64;

    enum Mode { sections = 0, octaves, roundRobin, solo };
    enum Motion { still = 0, wave, envelope, steps, randomPerNote, drift };
    enum Onset { onsetSoft = 0, onsetHard, onsetH, onsetM, onsetN, onsetL, onsetD, onsetB, onsetW, onsetY, onsetRandom };
    enum Mics { micSpaced = 0, micNearCoincident, micXY, micFigure8, micMono };
    enum Arrangement { arrRows = 0, arrArc, arrSections, arrLine, arrCluster };

    /** One person. Anatomy and voice values are 0..1 (0.5 = average) unless noted. */
    struct Singer
    {
        float sex = 0.0f;        // 0 woman .. 1 man
        float age = 30.0f;       // years
        float head = 0.5f, faceLength = 0.5f, jaw = 0.5f, chin = 0.5f, cheeks = 0.5f,
              nose = 0.5f, lips = 0.5f, mouth = 0.5f, neck = 0.5f, chest = 0.5f;
        float pitch = 0.5f, rasp = 0.0f, air = 0.5f, vibSpeed = 0.5f, vibWidth = 0.5f, bright = 0.5f, accuracy = 0.6f;
        int skin = 0, hair = 0, hairColour = 0, robe = 0, name = 0;
        bool beard = false, glasses = false;
        bool locked = false;     // kept when the cast is re-rolled
    };

    /** Editable fields of a Singer, for the editor's sliders. */
    enum Field { fSex = 0, fAge, fHead, fFaceLength, fJaw, fChin, fCheeks, fNose, fLips, fMouth, fNeck, fChest,
                 fPitch, fRasp, fAir, fVibSpeed, fVibWidth, fBright, fAccuracy, numFields };
    static juce::String fieldName (int f);
    static juce::String fieldTip (int f);
    static float& field (Singer&, int f);
    static float fieldMin (int f) { return f == fAge ? 6.0f : 0.0f; }
    static float fieldMax (int f) { return f == fAge ? 90.0f : 1.0f; }

    /** What a singer's body makes of their voice. */
    struct VoiceInfo
    {
        float tractCm = 17.0f, formantScale = 1.0f, f1Mul = 1.0f, f2Mul = 1.0f, f3Mul = 1.0f, bwMul = 1.0f;
        float nasal = 0.1f, nasalHz = 270.0f, chestDb = 0.0f;
        float centre = 55.0f, low = 45.0f, high = 65.0f, breakNote = 64.0f;
        float rd = 1.0f, breath = 0.1f, jitter = 0.005f, shimmer = 0.03f, vibRate = 5.5f, vibScale = 1.0f, wobble = 0.03f;
        float bright = 0.5f, rasp = 0.0f, errorCents = 10.0f;
        juce::String kind;       // Soprano, Mezzo, Alto, Tenor, Baritone, Bass, Treble...
    };
    static VoiceInfo describe (const Singer&);
    static juce::String noteName (float midi);
    static juce::String personName (const Singer&);
    static juce::String personLabel (const Singer&);     // "Woman, 34" / "Boy, 9"

    // ---- the cast (message thread) ----
    int numSingers() const;
    Singer getSinger (int index) const;
    void setSinger (int index, const Singer&);          // stores it (and locks it)
    void setLocked (int index, bool locked);
    void rerollSinger (int index);                      // a new random person in that place
    void rerollCast();                                  // new cast number, unlocked singers change
    juce::Point<float> getPosition (int index) const;  // metres: x across the stage, y = distance from the mics
    /** Everyone at once (much cheaper than one by one): who they are and where they stand. */
    void getCast (std::vector<Singer>& singers, std::vector<juce::Point<float>>& positions) const;
    void setPosition (int index, juce::Point<float>);
    void rearrange();                                   // next arrangement, dragged places forgotten
    static juce::StringArray styleNames();
    static juce::String styleDescription (int style);
    static juce::StringArray onsetNames();
    static juce::StringArray micNames();
    static juce::StringArray arrangementNames();
    static juce::StringArray motionNames();
    /** Vowel chart: the 12 vowels and where they sit (x front..back, y close..open). */
    struct VowelPoint { const char* symbol; const char* example; float x, y; };
    static const std::array<VowelPoint, 12>& vowelChart();
    /** F1..F3 of a male reference voice at a chart position (for the tract display). */
    static std::array<float, 3> referenceFormants (float x, float y);

    // ---- live state for the editor (any thread) ----
    struct Live { float level = 0.0f, vowelX = 0.5f, vowelY = 0.5f, open = 0.0f, hum = 0.0f; int note = -1; };
    Live getLive (int index) const;
    int getActiveSingers() const { return activeSingers.load(); }

    // BuiltinProcessor
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 8.0; }
    juce::StringArray getProgramNames() override { return presetNames(); }
    static juce::StringArray presetNames();
    static juce::String presetDescription (int program);
    void loadProgram (int) override;
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (ChoirLoft&)> editorFactory;

    /** Tests: block until the room is loaded. */
    void waitUntilReady();

    struct Voice;   // one singer's DSP (public for the implementation's helpers)

private:
    void timerCallback() override;
    void rebuildRoomIfNeeded (bool force);
    struct CastKey { int seed = -1, count = -1, arrangement = -1; float women = -1, age = -1, spread = -1, diversity = -1, distance = -1; bool operator!= (const CastKey& o) const; };
    CastKey castKey() const;
    Singer generate (int index, const CastKey&) const;
    juce::Point<float> arrangedPosition (int index, const CastKey&, const std::array<Singer, maxSingers>&) const;
    static juce::Point<float> sectionsPosition (int rank, int count);
    void refreshCast (bool force);          // audio thread: copies edits, derives the voices

    // notes
    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, int channel, float velocity);
    void noteOff (int note, int channel);
    void releaseAll (bool hard);
    void startSinger (int s, int note, int channel, float velocity, bool legato);
    float singerCost (int s, int note) const;
    int sungNote (int s, int note) const;

    // ---- message-thread edits ----
    mutable juce::SpinLock castLock;
    std::array<Singer, maxSingers> custom {};
    std::array<bool, maxSingers> hasCustom {};
    std::array<juce::Point<float>, maxSingers> customPos {};
    std::array<bool, maxSingers> hasPos {};
    std::atomic<int> castVersion { 1 };

    // ---- audio thread ----
    double sampleRate = 48000.0;
    int maxBlock = 512;
    int seenVersion = 0, seenMics = -1;
    juce::int64 sampleClock = 0, now = 0;   // now: the sample the current MIDI event lands on
    CastKey seenKey;
    std::array<Singer, maxSingers> aSinger {};
    std::array<VoiceInfo, maxSingers> aInfo {};
    std::array<juce::Point<float>, maxSingers> aPos {};
    std::vector<std::unique_ptr<Voice>> voices;
    int count = 8;

    struct Held { int note = 0, channel = 1; float velocity = 0.8f; juce::uint32 order = 0; bool down = false; juce::int64 time = 0; };
    std::vector<Held> held;               // notes being sung (down, or held by the sustain pedal)
    juce::uint32 noteCounter = 0;
    int rrNext = 0;
    bool sustain = false;
    float modWheel = 0.0f, expression = 1.0f;
    std::array<float, 17> chanBend {}, chanPressure {};
    juce::Random random;

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> roomBus, wet;
    std::atomic<bool> roomReady { false };
    int roomKey[2] { -1, -1 };
    float dcL = 0, dcR = 0, dcXL = 0, dcXR = 0;

    std::atomic<int> activeSingers { 0 };
    std::array<std::atomic<float>, maxSingers> liveLevel {}, liveX {}, liveY {}, liveOpen {}, liveHum {};
    std::array<std::atomic<int>, maxSingers> liveNote {};
};

} // namespace wis::daw
