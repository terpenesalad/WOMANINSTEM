#include "CabSim.h"
#include "SpeakerModel.h"
#include "Separation/AudioFileLoader.h"

namespace wis
{

using AC = juce::dsp::IIR::ArrayCoefficients<float>;

template <size_t N>
static void assign (juce::dsp::IIR::Filter<float>& f, const std::array<float, N>& c) { *f.coefficients = c; }

juce::StringArray cabTypeNames()
{
    return { "4x12 British", "4x12 Modern", "2x12 American", "2x12 British Blue", "1x12 Open Back", "1x12 Tweed",
             "Bass 1x15 Vintage", "Bass 2x15 60s", "Bass 4x10 + Horn", "Bass 8x10", "Bass 2x10 Modern", "Impulse Response" };
}

bool cabTypeIsBass (CabType t) { return t >= CabType::bass1x15Vintage && t <= CabType::bass2x10Modern; }

int migrateCabTypeV1 (int old)
{
    // 1.x - 3.0: 4x12 British, 2x12 American, 1x12 Open Back, 4x10 Bass, 8x10 Bass, Impulse Response
    static const CabType map[] = { CabType::brit4x12, CabType::american2x12, CabType::openBack1x12, CabType::bass4x10Horn,
                                   CabType::bass8x10, CabType::impulseResponse };
    return (int) map[juce::jlimit (0, 5, old)];
}

juce::StringArray micTypeNames()
{
    return { "Dynamic Mic", "Ribbon Mic", "Condenser Mic", "Dynamic + Ribbon" };
}

// =====================================================================================================
//  Impulse response design
// =====================================================================================================
juce::AudioBuffer<float> CabSim::designImpulseResponse (CabType type, MicType mic, float pos, float distance, double sr)
{
    if (type == CabType::impulseResponse || type == CabType::count)
        type = CabType::brit4x12;
    return speaker::designCloseMic (type, mic, pos, distance, sr);
}

// =====================================================================================================
//  Background builder
// =====================================================================================================
struct CabSim::Builder : public juce::Thread
{
    explicit Builder (CabSim& o) : juce::Thread ("Cab IR builder"), owner (o) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (15);
            owner.conv.collectGarbage();
            owner.userIr.collect();
            const int want = owner.wantedKey.load();
            if (want < 0 || want == owner.builtKey.load() || ! owner.conv.isIdle()) continue;
            CabType t; MicType m; float pos, room;
            decode (want, t, m, pos, room);
            auto ir = designImpulseResponse (t, m, pos, room, owner.sampleRate);
            if (owner.conv.offer (Convolver::Kernel::make (ir.getReadPointer (0), ir.getNumSamples())))
                owner.builtKey = want;
        }
    }
    static void decode (int key, CabType& t, MicType& m, float& pos, float& room)
    {
        room = (float) (key % 21) / 20.0f; key /= 21;
        pos = (float) (key % 21) / 20.0f;  key /= 21;
        m = (MicType) (key % 4);           key /= 4;
        t = (CabType) key;
    }
    CabSim& owner;
};

int CabSim::keyFor (CabType t, MicType m, float pos, float room)
{
    const int pq = juce::roundToInt (juce::jlimit (0.0f, 1.0f, pos) * 20.0f);
    const int rq = juce::roundToInt (juce::jlimit (0.0f, 1.0f, room) * 20.0f);
    return (((int) t * 4 + (int) m) * 21 + pq) * 21 + rq;
}

CabSim::CabSim() : builder (std::make_unique<Builder> (*this)) {}

CabSim::~CabSim()
{
    builder->stopThread (2000);
}

void CabSim::prepare (double sr, int maxBlock)
{
    builder->stopThread (2000);
    sampleRate = sr;
    for (auto* f : { &userLowCut, &userHighCut, &userHighCut2 }) { f->coefficients->coefficients.ensureStorageAllocated (8); f->reset(); }
    rebuildNow();
    if (userIr.install (convolution) >= 0)   // a file loaded while stopped: prepare() applies it straight away
        irInstalled = true;
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 1 });
    dirty = true;
    updateFilters();
    builder->startThread (juce::Thread::Priority::low);
}

void CabSim::rebuildNow()
{
    int key = wantedKey.load();
    if (key < 0) key = keyFor (type == CabType::impulseResponse ? CabType::brit4x12 : type, MicType::dynamic, 0.3f, 0.1f);
    CabType kt; MicType km; float kp, kr;
    Builder::decode (key, kt, km, kp, kr);
    auto ir = designImpulseResponse (kt, km, kp, kr, sampleRate);
    conv.reset (Convolver::Kernel::make (ir.getReadPointer (0), ir.getNumSamples()));
    wantedKey = key;
    builtKey = key;
}

void CabSim::reset()
{
    userLowCut.reset(); userHighCut.reset(); userHighCut2.reset();
    convolution.reset();
}

void CabSim::setParameters (CabType t, float lo, float hi, MicType mic, float micPos, float room)
{
    if (lo != lowCut || hi != highCut)
        dirty = true;
    type = t; lowCut = lo; highCut = hi;
    if (t != CabType::impulseResponse)
        wantedKey = keyFor (t, mic, micPos, room);
}

void CabSim::updateFilters()
{
    if (! dirty) return;
    dirty = false;
    const float nyq = (float) (sampleRate * 0.45);
    assign (userLowCut,   AC::makeHighPass (sampleRate, juce::jlimit (10.0f, 1000.0f, lowCut), 0.707f));
    assign (userHighCut,  AC::makeLowPass  (sampleRate, juce::jlimit (1000.0f, nyq, highCut), 0.54f));
    assign (userHighCut2, AC::makeLowPass  (sampleRate, juce::jlimit (1000.0f, nyq, highCut), 1.31f));   // 4th-order Butterworth pair
}

bool CabSim::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> test (sharedFormatManager().createReaderFor (file));
    if (test == nullptr || test->lengthInSamples < 16)
        return false;

    // Cabinet IRs rarely need more than 500 ms; trimming keeps CPU low with long room IRs. Read here, installed by the
    // audio thread (JUCE wants loadImpulseResponse in step with process), resampled and normalised by the convolution.
    const int len = (int) juce::jmin (test->lengthInSamples, (juce::int64) (test->sampleRate * 0.5));
    juce::AudioBuffer<float> ir (1, juce::jmax (1, len));
    ir.clear();
    test->read (&ir, 0, len, 0, true, false);
    userIr.post (std::move (ir), test->sampleRate, 1, false, true, true);
    irFile = file;
    irReady = true;
    return true;
}

void CabSim::clearImpulseResponse()
{
    userIr.discard();
    irReady = false;
    irFile = juce::File();
}

void CabSim::process (float* data, int numSamples)
{
    updateFilters();

    float* chans[] = { data };
    juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
    if (userIr.install (convolution) >= 0)
        irInstalled = true;
    if (type == CabType::impulseResponse)
    {
        if (irReady.load() && irInstalled.load())
        {
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
            // IRs are normalised; bring them back to roughly the same loudness as the built-in cabs.
            juce::FloatVectorOperations::multiply (data, 0.8f, numSamples);
        }
    }
    else
    {
        conv.process (data, numSamples);
    }

    for (int i = 0; i < numSamples; ++i)
        data[i] = userHighCut2.processSample (userHighCut.processSample (userLowCut.processSample (data[i])));
}

} // namespace wis
