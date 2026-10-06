#include "RhythmBox.h"

namespace wis::daw
{

static prm::Layout rhythmBoxLayout()
{
    prm::Layout l;
    prm::addChoice (l, "kit", "Kit", DrumSynth::kitNames(), 0);
    prm::addFloat (l, "tune", "Tune", -12.0f, 12.0f, 0.0f, "st", 0.0f, 1);
    prm::addFloat (l, "decay", "Decay", 0.3f, 2.5f, 1.0f, "x", 1.0f, 2);
    prm::addFloat (l, "tone", "Tone", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    prm::addPercent (l, "drive", "Drive", 0.0f);
    prm::addDb (l, "volume", "Volume", -30.0f, 6.0f, -3.0f);
    return l;
}

RhythmBox::RhythmBox() : BuiltinProcessor ("rhythmbox", "Rhythm Box", true, rhythmBoxLayout()) {}

void RhythmBox::prepareToPlay (double sr, int)
{
    drums.prepare (sr);
}

void RhythmBox::loadProgram (int index)
{
    setParam ("kit", (float) index);
    setParam ("tune", index == 3 ? 2.0f : 0.0f);
    setParam ("decay", index == 2 ? 1.2f : 1.0f);
    setParam ("tone", index == 3 ? -0.3f : 0.0f);
    setParam ("drive", index == 2 ? 0.2f : index == 3 ? 0.35f : 0.05f);
}

void RhythmBox::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();
    drums.setKit ((int) param ("kit"));
    drums.setShape (param ("tune"), param ("decay"), param ("tone"));
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { drums.render (L + pos, R + pos, at - pos); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) drums.trigger (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) drums.allOff();
    }
    if (pos < n) drums.render (L + pos, R + pos, n - pos);

    const float drive = param ("drive");
    const float g = juce::Decibels::decibelsToGain (param ("volume"));
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            if (drive > 0.001f) x = std::tanh (x * (1.0f + drive * 6.0f)) / (1.0f + drive * 1.5f);
            d[i] = x * g;
        }
    }
    if (buffer.getNumChannels() > 1 && R == L) buffer.copyFrom (1, 0, buffer, 0, 0, n);
}

} // namespace wis::daw
