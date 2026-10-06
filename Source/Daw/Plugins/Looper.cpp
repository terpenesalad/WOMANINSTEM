#include "Looper.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (Looper&)> Looper::editorFactory;

enum Action { none = 0, mainAction = 1, stopAction = 2, undoAction = 3, redoAction = 4, clearAction = 5 };

static prm::Layout looperLayout()
{
    prm::Layout l;
    prm::addChoice (l, "sync", "Start / Stop", { "Instantly", "On the Bar" }, 1);
    prm::addChoice (l, "bars", "Loop Length", { "Free", "1 Bar", "2 Bars", "4 Bars", "8 Bars" }, 0);
    prm::addBool (l, "half", "Half Speed", false);
    prm::addBool (l, "reverse", "Reverse", false);
    prm::addPercent (l, "feedback", "Overdub Keep", 1.0f);
    prm::addDb (l, "level", "Loop Level", -30.0f, 6.0f, 0.0f);
    prm::addDb (l, "input", "Input Through", -60.0f, 6.0f, 0.0f);
    return l;
}

Looper::Looper() : BuiltinProcessor ("looper", "Loop Station", false, looperLayout()) {}

void Looper::prepareToPlay (double sr, int)
{
    if (std::abs (sr - sampleRate) > 1.0 || loop.getNumSamples() == 0)
    {
        sampleRate = sr;
        const int size = (int) (sr * maxSeconds);
        loop.setSize (2, size);
        undoBuf.setSize (2, size);
        loop.clear();
        undoBuf.clear();
        state = empty; loopLength = 0; layers = 0; recordLength = 0;
    }
    lastBar = -1;
}

void Looper::pressMain()  { pending = mainAction; }
void Looper::pressStop()  { pending = stopAction; }
void Looper::pressUndo()  { pending = undoAction; }
void Looper::pressRedo()  { pending = redoAction; }
void Looper::pressClear() { pending = clearAction; }

std::array<float, 64> Looper::getOverview() const
{
    const juce::SpinLock::ScopedLockType sl (overviewLock);
    return overview;
}

void Looper::apply (int action)
{
    const int len = loopLength.load();
    auto finishRecording = [&]
    {
        loopLength = juce::jmax (1, recordLength);
        // short fades at the seam so the loop doesn't click
        const int f = juce::jmin (loopLength.load() / 4, (int) (sampleRate * 0.004));
        for (int ch = 0; ch < 2; ++ch)
        {
            loop.applyGainRamp (ch, 0, f, 0.0f, 1.0f);
            loop.applyGainRamp (ch, loopLength.load() - f, f, 1.0f, 0.0f);
        }
        layers = 1;
        readPos = 0.0;
        std::array<float, 64> ov {};
        const int L = loopLength.load();
        for (int i = 0; i < L; ++i)
        {
            auto& b = ov[(size_t) juce::jmin (63, i * 64 / L)];
            b = juce::jmax (b, std::abs (loop.getSample (0, i)), std::abs (loop.getSample (1, i)));
        }
        const juce::SpinLock::ScopedLockType sl (overviewLock);
        overview = ov;
    };

    switch (action)
    {
        case mainAction:
            switch (state.load())
            {
                case empty:     recordLength = 0; state = recording; break;
                case recording: finishRecording(); state = playing; break;
                case playing:
                    for (int ch = 0; ch < 2; ++ch) undoBuf.copyFrom (ch, 0, loop, ch, 0, len);
                    redoAvailable = false;
                    state = overdubbing;
                    break;
                case overdubbing: layers = layers + 1; state = playing; break;
                case stopped:   readPos = 0.0; state = playing; break;
                default: break;
            }
            break;
        case stopAction:
            if (state.load() == recording) finishRecording();
            if (state.load() == overdubbing) layers = layers + 1;
            state = loopLength.load() > 0 ? stopped : empty;
            break;
        case undoAction:
        case redoAction:
            if (len > 0 && (action == undoAction ? (layers.load() > 1 || state.load() == overdubbing) : redoAvailable.load()))
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* a = loop.getWritePointer (ch);
                    auto* b = undoBuf.getWritePointer (ch);
                    for (int i = 0; i < len; ++i) std::swap (a[i], b[i]);
                }
                if (state.load() == overdubbing) state = playing;
                else layers = juce::jmax (1, layers.load() + (action == undoAction ? -1 : 1));
                redoAvailable = action == undoAction;
            }
            break;
        case clearAction:
            loop.clear(); undoBuf.clear();
            loopLength = 0; recordLength = 0; layers = 0; redoAvailable = false;
            state = empty;
            {
                const juce::SpinLock::ScopedLockType sl (overviewLock);
                overview.fill (0.0f);
            }
            break;
        default: break;
    }
}

void Looper::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);

    // where are the bar lines in this block?
    bool songRolling = false;
    double ppq = 0.0, bpm = 120.0, bpb = 4.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            songRolling = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) bpb = ts->numerator * 4.0 / ts->denominator;
        }
    const bool syncToBar = param ("sync") > 0.5f && songRolling;
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    int barOffset = -1;
    if (songRolling)
    {
        const auto barNow = (juce::int64) std::floor (ppq / bpb + 1.0e-9);
        const double nextBarPpq = (double) (barNow + 1) * bpb;
        const int off = (int) std::ceil ((nextBarPpq - ppq) / beatsPerSample - 1.0e-9);
        if (barNow != lastBar && lastBar >= 0 && std::abs (ppq - barNow * bpb) < beatsPerSample * 2) barOffset = 0;
        else if (off < n) barOffset = off;
        lastBar = (juce::int64) std::floor ((ppq + n * beatsPerSample) / bpb + 1.0e-9);
    }
    if (! syncToBar && pending.load() != none) apply (pending.exchange (none));

    const int barsChoice = (int) param ("bars");
    const int fixedBars = barsChoice == 0 ? 0 : barsChoice == 1 ? 1 : barsChoice == 2 ? 2 : barsChoice == 3 ? 4 : 8;
    const int fixedLength = fixedBars > 0 ? (int) std::llround (fixedBars * bpb * 60.0 / bpm * sampleRate) : 0;
    const bool half = param ("half") > 0.5f, reverse = param ("reverse") > 0.5f;
    const float keep = param ("feedback");
    const float level = juce::Decibels::decibelsToGain (param ("level"));
    const float through = param ("input") <= -59.0f ? 0.0f : juce::Decibels::decibelsToGain (param ("input"));
    const int maxLen = loop.getNumSamples();

    for (int i = 0; i < n; ++i)
    {
        if (i == barOffset && syncToBar && pending.load() != none) apply (pending.exchange (none));

        const float inL = L[i], inR = R[i];
        float outL = inL * through, outR = inR * through;
        const int st = state.load();
        if (st == recording)
        {
            if (recordLength < maxLen)
            {
                loop.setSample (0, recordLength, inL);
                loop.setSample (1, recordLength, inR);
                ++recordLength;
            }
            // a fixed length (or a full buffer) closes the loop by itself
            if ((fixedLength > 0 && recordLength >= fixedLength) || recordLength >= maxLen) apply (mainAction);
        }
        else if ((st == playing || st == overdubbing) && loopLength.load() > 0)
        {
            const int len = loopLength.load();
            const int idx = juce::jlimit (0, len - 1, (int) readPos);
            outL += loop.getSample (0, idx) * level;
            outR += loop.getSample (1, idx) * level;
            if (st == overdubbing)
            {
                loop.setSample (0, idx, loop.getSample (0, idx) * keep + inL);
                loop.setSample (1, idx, loop.getSample (1, idx) * keep + inR);
            }
            readPos += (half ? 0.5 : 1.0) * (reverse ? -1.0 : 1.0);
            if (readPos >= len) readPos -= len;
            if (readPos < 0) readPos += len;
            playPos = (int) readPos;
        }
        L[i] = outL;
        if (R != L) R[i] = outR;
    }
}

juce::String Looper::exportLoop (const juce::File& f)
{
    const int len = loopLength.load();
    if (len <= 0) return "The loop is empty";
    juce::AudioBuffer<float> copy (2, len);
    for (int ch = 0; ch < 2; ++ch) copy.copyFrom (ch, 0, loop, ch, 0, len);
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return "Couldn't write " + f.getFullPathName();
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
    auto w = juce::WavAudioFormat().createWriterFor (os, opts);
    if (w == nullptr) return "Couldn't create the WAV file";
    w->writeFromAudioSampleBuffer (copy, 0, len);
    return {};
}

} // namespace wis::daw
