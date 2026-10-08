#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

/** Yodel Yeti: a singing voice synth. A glottal pulse with breath noise goes through five formant filters that
    morph between the vowels U-O-A-E-I (the X/Y pad, the mod wheel, an LFO or a new vowel every note), with
    glide, vibrato that swells in, a closed-mouth "mmm" at the edges of notes, throat-singing overtones, a choir
    of detuned voices, and a stereo delay and mountain reverb behind it. Its editor animates the yeti. */
class YetiVoice : public BuiltinProcessor
{
public:
    YetiVoice();
    ~YetiVoice() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (YetiVoice&)> editorFactory;

    static juce::StringArray voiceNames();
    static juce::StringArray vowelModeNames();

    // ---- the X/Y pad: sing with the mouse (message thread writes, audio thread reads) ----
    std::atomic<bool> padDown { false };
    std::atomic<float> padX { 0.5f }, padY { 0.5f };   // vowel, pitch (2 octaves)

    // ---- for the animation (audio thread writes) ----
    struct Face
    {
        std::atomic<float> level { 0.0f };       // loudness 0..1
        std::atomic<float> vowel { 0.5f };       // 0 U .. 1 I
        std::atomic<float> mouth { 0.0f };       // 0 closed (mmm) .. 1 open
        std::atomic<float> pitch { 0.5f };       // 0..1 across the voice's range
        std::atomic<float> vibrato { 0.0f };     // -1..1 current vibrato swing
        std::atomic<float> heldSeconds { 0.0f }; // how long the current note has been held
        std::atomic<int> notes { 0 };            // note-on counter (bob on each one)
        std::atomic<int> sounding { 0 };
        std::atomic<float> echo { 0.0f };        // delay output level (echo rings)
    } face;

    static constexpr int maxVoices = 6;

private:
    struct Voice;
    void startNote (int note, float velocity);
    void stopNote (int note);
    void render (float* L, float* R, int n);

    std::vector<std::unique_ptr<Voice>> voices;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    int noteCount = 0, vowelStep = 0;
    float modWheel = 0.0f, bend = 0.0f;
    bool padWasDown = false;
    juce::Random random;
    double lfoPhase = 0.0;
    std::vector<int> heldNotes;

    // delay + reverb
    std::vector<float> delayBuf[2];
    int delayPos = 0;
    float delayLp[2] {};
    juce::Reverb reverb;
    juce::AudioBuffer<float> revBuf;
    float echoEnv = 0.0f;
};

} // namespace wis::daw
