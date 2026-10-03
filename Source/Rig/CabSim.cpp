#include "CabSim.h"
#include "Separation/AudioFileLoader.h"

namespace wis
{

using AC = juce::dsp::IIR::ArrayCoefficients<float>;

template <size_t N>
static void assign (juce::dsp::IIR::Filter<float>& f, const std::array<float, N>& c) { *f.coefficients = c; }

juce::StringArray cabTypeNames()
{
    return { "4x12 British", "2x12 American", "1x12 Open Back", "4x10 Bass", "8x10 Bass", "Impulse Response" };
}

void CabSim::prepare (double sr, int maxBlock)
{
    sampleRate = sr;

    for (auto& b : bands) { b.coefficients->coefficients.ensureStorageAllocated (8); b.reset(); }
    for (auto* f : { &userLowCut, &userHighCut, &userHighCut2 }) { f->coefficients->coefficients.ensureStorageAllocated (8); f->reset(); }

    convolution.prepare ({ sr, (juce::uint32) maxBlock, 1 });
    dirty = true;
    updateFilters();
}

void CabSim::reset()
{
    for (auto& b : bands) b.reset();
    userLowCut.reset(); userHighCut.reset(); userHighCut2.reset();
    convolution.reset();
}

void CabSim::setParameters (CabType t, float lo, float hi)
{
    if (t != type || lo != lowCut || hi != highCut)
        dirty = true;
    type = t; lowCut = lo; highCut = hi;
}

void CabSim::updateFilters()
{
    if (! dirty) return;
    dirty = false;

    const double sr = sampleRate;
    const float nyq = (float) (sr * 0.45);
    auto g = [] (float db) { return juce::Decibels::decibelsToGain (db); };
    auto lp = [&] (float hz) { return juce::jmin (hz, nyq); };

    // Each cabinet: speaker low-end resonance, cone break-up peaks, the classic "cab dip",
    // and a steep high roll-off (two cascaded low-passes = 24 dB/oct, like a real speaker).
    switch (type)
    {
        case CabType::brit4x12:
            assign (bands[0], AC::makeHighPass  (sr, 75.0f, 1.0f));
            assign (bands[1], AC::makePeakFilter(sr, 115.0f, 1.3f, g (3.0f)));
            assign (bands[2], AC::makePeakFilter(sr, 450.0f, 1.0f, g (-3.5f)));
            assign (bands[3], AC::makePeakFilter(sr, 2400.0f, 1.6f, g (4.0f)));
            assign (bands[4], AC::makeLowPass   (sr, lp (5200.0f), 0.9f));
            assign (bands[5], AC::makeLowPass   (sr, lp (6000.0f), 0.6f));
            break;
        case CabType::american2x12:
            assign (bands[0], AC::makeHighPass  (sr, 70.0f, 0.9f));
            assign (bands[1], AC::makePeakFilter(sr, 125.0f, 1.1f, g (2.0f)));
            assign (bands[2], AC::makePeakFilter(sr, 600.0f, 0.9f, g (-2.5f)));
            assign (bands[3], AC::makePeakFilter(sr, 2800.0f, 1.4f, g (3.0f)));
            assign (bands[4], AC::makeLowPass   (sr, lp (6500.0f), 0.8f));
            assign (bands[5], AC::makeLowPass   (sr, lp (7500.0f), 0.6f));
            break;
        case CabType::openBack1x12:
            assign (bands[0], AC::makeHighPass  (sr, 95.0f, 0.7f));
            assign (bands[1], AC::makePeakFilter(sr, 140.0f, 1.0f, g (1.0f)));
            assign (bands[2], AC::makePeakFilter(sr, 400.0f, 0.8f, g (-1.5f)));
            assign (bands[3], AC::makePeakFilter(sr, 2000.0f, 1.2f, g (3.0f)));
            assign (bands[4], AC::makeLowPass   (sr, lp (7000.0f), 0.8f));
            assign (bands[5], AC::makeLowPass   (sr, lp (8500.0f), 0.6f));
            break;
        case CabType::bass4x10:
            assign (bands[0], AC::makeHighPass  (sr, 45.0f, 0.9f));
            assign (bands[1], AC::makePeakFilter(sr, 85.0f, 1.0f, g (3.0f)));
            assign (bands[2], AC::makePeakFilter(sr, 500.0f, 0.9f, g (-3.0f)));
            assign (bands[3], AC::makePeakFilter(sr, 2500.0f, 1.2f, g (2.0f)));
            assign (bands[4], AC::makeLowPass   (sr, lp (5500.0f), 0.8f));
            assign (bands[5], AC::makeLowPass   (sr, lp (6500.0f), 0.6f));
            break;
        case CabType::bass8x10:
            assign (bands[0], AC::makeHighPass  (sr, 38.0f, 0.9f));
            assign (bands[1], AC::makePeakFilter(sr, 70.0f, 1.0f, g (4.0f)));
            assign (bands[2], AC::makePeakFilter(sr, 400.0f, 0.8f, g (-4.0f)));
            assign (bands[3], AC::makePeakFilter(sr, 1800.0f, 1.2f, g (1.5f)));
            assign (bands[4], AC::makeLowPass   (sr, lp (4200.0f), 0.8f));
            assign (bands[5], AC::makeLowPass   (sr, lp (5000.0f), 0.6f));
            break;
        case CabType::impulseResponse:
        case CabType::count:
        default:
            for (auto& b : bands)
                assign (b, AC::makeAllPass (sr, 1000.0f, 0.1f));   // unused when an IR is active
            break;
    }

    assign (userLowCut,   AC::makeHighPass (sr, juce::jlimit (10.0f, 1000.0f, lowCut), 0.707f));
    assign (userHighCut,  AC::makeLowPass  (sr, juce::jlimit (1000.0f, nyq, highCut), 0.54f));
    assign (userHighCut2, AC::makeLowPass  (sr, juce::jlimit (1000.0f, nyq, highCut), 1.31f));   // 4th-order Butterworth pair
}

bool CabSim::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> test (sharedFormatManager().createReaderFor (file));
    if (test == nullptr || test->lengthInSamples < 16)
        return false;

    // Cabinet IRs rarely need more than 500 ms; trimming keeps CPU low with long room IRs.
    convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes,
                                     (size_t) (sampleRate * 0.5), juce::dsp::Convolution::Normalise::yes);
    irFile = file;
    irReady = true;
    return true;
}

void CabSim::clearImpulseResponse()
{
    irReady = false;
    irFile = juce::File();
}

void CabSim::process (float* data, int numSamples)
{
    updateFilters();

    if (type == CabType::impulseResponse && irReady.load())
    {
        float* chans[] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        // IRs are normalised; bring them back to roughly the same loudness as the built-in cabs.
        juce::FloatVectorOperations::multiply (data, 0.8f, numSamples);
    }
    else if (type != CabType::impulseResponse)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i];
            for (auto& b : bands)
                x = b.processSample (x);
            data[i] = x * 1.6f;   // make-up for the energy a speaker removes (+4 dB)
        }
    }

    for (int i = 0; i < numSamples; ++i)
        data[i] = userHighCut2.processSample (userHighCut.processSample (userLowCut.processSample (data[i])));
}

} // namespace wis
