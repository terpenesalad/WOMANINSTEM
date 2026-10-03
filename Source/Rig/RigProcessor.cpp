#include "RigProcessor.h"
#include "Separation/ModelManager.h"

namespace wis
{

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

static const char* allParamIds[] = {
    pid::inputGain, pid::gateOn, pid::gateThresh, pid::gateRelease,
    pid::compOn, pid::compThresh, pid::compRatio, pid::compAttack, pid::compRelease, pid::compLevel,
    pid::driveOn, pid::driveType, pid::driveAmount, pid::driveTone, pid::driveLevel,
    pid::ampOn, pid::ampModel, pid::ampGain, pid::ampBass, pid::ampMid, pid::ampTreble, pid::ampPresence, pid::ampMaster,
    pid::cabOn, pid::cabType, pid::cabLowCut, pid::cabHighCut,
    pid::eqOn, pid::eqLow, pid::eqLowMid, pid::eqHighMid, pid::eqHigh,
    pid::chorusOn, pid::chorusRate, pid::chorusDepth, pid::chorusMix,
    pid::delayOn, pid::delayTime, pid::delayFeedback, pid::delayTone, pid::delayMix, pid::delayPingPong,
    pid::reverbOn, pid::reverbSize, pid::reverbDamp, pid::reverbPreDelay, pid::reverbMix, pid::reverbWidth,
    pid::outLevel
};

static Range freqRange (float lo, float hi)
{
    Range r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

static juce::AudioParameterFloatAttributes unit (const char* label, int decimals = 1)
{
    return juce::AudioParameterFloatAttributes().withLabel (label)
        .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
}

static juce::AudioParameterFloatAttributes hz()
{
    return juce::AudioParameterFloatAttributes().withLabel ("Hz")
        .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + "k" : juce::String ((int) v); });
}

juce::AudioProcessorValueTreeState::ParameterLayout RigProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto knob = Range (0.0f, 10.0f, 0.01f);
    auto pct  = Range (0.0f, 1.0f, 0.001f);

    l.add (std::make_unique<APF> (juce::ParameterID { pid::inputGain, 1 }, "Input", Range (-24.0f, 24.0f, 0.1f), 0.0f, unit ("dB")));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::gateOn, 1 }, "Gate", true));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::gateThresh, 1 }, "Threshold", Range (-90.0f, -20.0f, 0.1f), -64.0f, unit ("dB")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::gateRelease, 1 }, "Release", Range (5.0f, 500.0f, 1.0f, 0.5f), 80.0f, unit ("ms", 0)));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::compOn, 1 }, "Compressor", false));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::compThresh, 1 }, "Threshold", Range (-50.0f, 0.0f, 0.1f), -22.0f, unit ("dB")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::compRatio, 1 }, "Ratio", Range (1.0f, 20.0f, 0.1f, 0.5f), 4.0f, unit (":1")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::compAttack, 1 }, "Attack", Range (0.5f, 80.0f, 0.1f, 0.5f), 8.0f, unit ("ms")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::compRelease, 1 }, "Release", Range (20.0f, 800.0f, 1.0f, 0.5f), 140.0f, unit ("ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::compLevel, 1 }, "Level", Range (-12.0f, 24.0f, 0.1f), 4.0f, unit ("dB")));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::driveOn, 1 }, "Drive", false));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::driveType, 1 }, "Type", driveTypeNames(), 0));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::driveAmount, 1 }, "Drive", knob, 4.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::driveTone, 1 }, "Tone", knob, 5.5f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::driveLevel, 1 }, "Level", knob, 6.0f));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::ampOn, 1 }, "Amp", true));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::ampModel, 1 }, "Model", ampTypeNames(), 1));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::ampGain, 1 }, "Gain", knob, 5.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::ampBass, 1 }, "Bass", knob, 5.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::ampMid, 1 }, "Mid", knob, 5.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::ampTreble, 1 }, "Treble", knob, 5.5f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::ampPresence, 1 }, "Presence", knob, 5.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::ampMaster, 1 }, "Master", knob, 6.0f));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::cabOn, 1 }, "Cab", true));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::cabType, 1 }, "Cabinet", cabTypeNames(), 0));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::cabLowCut, 1 }, "Low Cut", freqRange (20.0f, 400.0f), 60.0f, hz()));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::cabHighCut, 1 }, "High Cut", freqRange (2000.0f, 20000.0f), 10000.0f, hz()));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::eqOn, 1 }, "EQ", false));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::eqLow, 1 }, "Low", Range (-12.0f, 12.0f, 0.1f), 0.0f, unit ("dB")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::eqLowMid, 1 }, "Lo Mid", Range (-12.0f, 12.0f, 0.1f), 0.0f, unit ("dB")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::eqHighMid, 1 }, "Hi Mid", Range (-12.0f, 12.0f, 0.1f), 0.0f, unit ("dB")));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::eqHigh, 1 }, "High", Range (-12.0f, 12.0f, 0.1f), 0.0f, unit ("dB")));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::chorusOn, 1 }, "Chorus", false));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::chorusRate, 1 }, "Rate", Range (0.05f, 5.0f, 0.01f, 0.5f), 0.7f, unit ("Hz", 2)));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::chorusDepth, 1 }, "Depth", pct, 0.35f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::chorusMix, 1 }, "Mix", pct, 0.45f));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::delayOn, 1 }, "Delay", false));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::delayTime, 1 }, "Time", Range (20.0f, 2000.0f, 1.0f, 0.5f), 380.0f, unit ("ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::delayFeedback, 1 }, "Feedback", Range (0.0f, 0.95f, 0.001f), 0.32f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::delayTone, 1 }, "Tone", freqRange (800.0f, 16000.0f), 4500.0f, hz()));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::delayMix, 1 }, "Mix", pct, 0.22f));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::delayPingPong, 1 }, "Ping-Pong", false));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::reverbOn, 1 }, "Reverb", true));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::reverbSize, 1 }, "Size", pct, 0.5f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::reverbDamp, 1 }, "Damping", pct, 0.5f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::reverbPreDelay, 1 }, "Pre-Delay", Range (0.0f, 250.0f, 1.0f, 0.6f), 18.0f, unit ("ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::reverbMix, 1 }, "Mix", pct, 0.16f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::reverbWidth, 1 }, "Width", pct, 1.0f));

    l.add (std::make_unique<APF> (juce::ParameterID { pid::outLevel, 1 }, "Output", Range (-36.0f, 12.0f, 0.1f), 0.0f, unit ("dB")));

    return l;
}

RigProcessor::RigProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RIG", createLayout())
{
    for (auto* id : allParamIds)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr);
        params[id] = raw;
    }
}

RigProcessor::~RigProcessor() = default;

void RigProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (16, samplesPerBlock);

    mono.assign ((size_t) maxBlock, 0.0f);
    wetL.assign ((size_t) maxBlock, 0.0f);
    wetR.assign ((size_t) maxBlock, 0.0f);

    gate.prepare (sr);
    compressor.prepare ({ sr, (juce::uint32) maxBlock, 1 });
    drive.prepare (sr, maxBlock);
    amp.prepare (sr, maxBlock);
    nam.prepare (sr, maxBlock);
    cab.prepare (sr, maxBlock);
    for (auto& f : eq) { f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
    eqCache.fill (99.0f);
    chorus.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    delay.prepare (sr, maxBlock);
    preDelay.prepare (sr);
    reverb.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    tuner.prepare (sr);

    inGain.reset (sr, 0.02);
    compMakeup.reset (sr, 0.02);
    outGain.reset (sr, 0.02);
    reverbMixSm.reset (sr, 0.05);
}

void RigProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2)
        return;
    processMonoToStereo (buffer.getReadPointer (0), buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
}

void RigProcessor::processMonoToStereo (const float* input, float* outL, float* outR, int numSamples)
{
    // Large device buffers: process in chunks of maxBlock.
    if (numSamples > maxBlock)
    {
        for (int pos = 0; pos < numSamples; pos += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - pos);
            processMonoToStereo (input + pos, outL + pos, outR + pos, n);
        }
        return;
    }

    float* x = mono.data();
    const int n = numSamples;

    // ---- input ----
    inGain.setTargetValue (juce::Decibels::decibelsToGain (p (pid::inputGain)));
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        x[i] = input[i] * inGain.getNextValue();
        peak = juce::jmax (peak, std::abs (input[i]));
    }
    if (peak > 0.99f) inputClip = true;
    inputPeak = juce::jmax (inputPeak.load(), peak);

    tuner.push (x, n);

    if (tunerMute.load())
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
        return;
    }

    // ---- gate ----
    if (p (pid::gateOn) > 0.5f)
    {
        gate.setParameters (p (pid::gateThresh), p (pid::gateRelease));
        gate.process (x, n);
    }

    // ---- compressor ----
    if (p (pid::compOn) > 0.5f)
    {
        compressor.setThreshold (p (pid::compThresh));
        compressor.setRatio (p (pid::compRatio));
        compressor.setAttack (p (pid::compAttack));
        compressor.setRelease (p (pid::compRelease));
        float* chans[] = { x };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
        compressor.process (juce::dsp::ProcessContextReplacing<float> (block));
        compMakeup.setTargetValue (juce::Decibels::decibelsToGain (p (pid::compLevel)));
        for (int i = 0; i < n; ++i) x[i] *= compMakeup.getNextValue();
    }

    // ---- drive pedal ----
    if (p (pid::driveOn) > 0.5f)
    {
        drive.setParameters ((DriveType) (int) p (pid::driveType), p (pid::driveAmount), p (pid::driveTone), p (pid::driveLevel));
        drive.process (x, n);
    }

    // ---- amp ----
    const auto model = (AmpType) (int) p (pid::ampModel);
    if (p (pid::ampOn) > 0.5f)
    {
        if (model == AmpType::namCapture)
        {
            amp.setParameters (AmpType::flatDi, 5.0f, p (pid::ampBass), p (pid::ampMid), p (pid::ampTreble), p (pid::ampPresence), p (pid::ampMaster));
            // Gain knob = input trim into the capture (-12..+12 dB around noon)
            const float trimDb = (p (pid::ampGain) - 5.0f) * 2.4f;
            if (nam.process (x, n, trimDb))
                amp.processToneOnly (x, n);
            else
                amp.process (x, n);   // no capture loaded yet: flat
        }
        else
        {
            amp.setParameters (model, p (pid::ampGain), p (pid::ampBass), p (pid::ampMid), p (pid::ampTreble), p (pid::ampPresence), p (pid::ampMaster));
            amp.process (x, n);
        }
    }

    // ---- cab ----
    if (p (pid::cabOn) > 0.5f)
    {
        cab.setParameters ((CabType) (int) p (pid::cabType), p (pid::cabLowCut), p (pid::cabHighCut));
        cab.process (x, n);
    }

    // ---- studio EQ ----
    if (p (pid::eqOn) > 0.5f)
    {
        const float gains[] = { p (pid::eqLow), p (pid::eqLowMid), p (pid::eqHighMid), p (pid::eqHigh) };
        using AC = juce::dsp::IIR::ArrayCoefficients<float>;
        for (int b = 0; b < 4; ++b)
        {
            if (gains[b] != eqCache[(size_t) b])
            {
                eqCache[(size_t) b] = gains[b];
                const float g = juce::Decibels::decibelsToGain (gains[b]);
                switch (b)
                {
                    case 0: *eq[0].coefficients = AC::makeLowShelf (sr, 100.0f, 0.7f, g); break;
                    case 1: *eq[1].coefficients = AC::makePeakFilter (sr, 400.0f, 0.9f, g); break;
                    case 2: *eq[2].coefficients = AC::makePeakFilter (sr, 2000.0f, 0.9f, g); break;
                    default:*eq[3].coefficients = AC::makeHighShelf (sr, 6000.0f, 0.7f, g); break;
                }
            }
        }
        for (int i = 0; i < n; ++i)
            x[i] = eq[3].processSample (eq[2].processSample (eq[1].processSample (eq[0].processSample (x[i]))));
    }

    // ---- to stereo ----
    std::copy (x, x + n, outL);
    std::copy (x, x + n, outR);

    // ---- chorus ----
    if (p (pid::chorusOn) > 0.5f)
    {
        chorus.setRate (p (pid::chorusRate));
        chorus.setDepth (p (pid::chorusDepth));
        chorus.setCentreDelay (7.0f);
        chorus.setFeedback (0.0f);
        chorus.setMix (p (pid::chorusMix));
        float* chans[] = { outL, outR };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // ---- delay ----
    if (p (pid::delayOn) > 0.5f)
    {
        delay.setParameters (p (pid::delayTime), p (pid::delayFeedback), p (pid::delayTone), p (pid::delayMix), p (pid::delayPingPong) > 0.5f);
        delay.process (outL, outR, n);
    }

    // ---- reverb (parallel send) ----
    reverbMixSm.setTargetValue (p (pid::reverbOn) > 0.5f ? p (pid::reverbMix) : 0.0f);
    if (p (pid::reverbOn) > 0.5f || reverbMixSm.isSmoothing())
    {
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.68f * p (pid::reverbSize);
        rp.damping  = p (pid::reverbDamp);
        rp.width    = p (pid::reverbWidth);
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        std::copy (outL, outL + n, wetL.begin());
        std::copy (outR, outR + n, wetR.begin());
        preDelay.setDelayMs (p (pid::reverbPreDelay));
        preDelay.process (wetL.data(), wetR.data(), n);

        float* chans[] = { wetL.data(), wetR.data() };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));

        for (int i = 0; i < n; ++i)
        {
            const float m = reverbMixSm.getNextValue();
            const float dry = std::cos (m * juce::MathConstants<float>::halfPi * 0.5f);   // gentle dry dip
            outL[i] = outL[i] * dry + wetL[(size_t) i] * m * 0.6f;
            outR[i] = outR[i] * dry + wetR[(size_t) i] * m * 0.6f;
        }
    }

    // ---- output ----
    outGain.setTargetValue (juce::Decibels::decibelsToGain (p (pid::outLevel)));
    float outPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        outL[i] = std::isfinite (outL[i]) ? outL[i] * g : 0.0f;
        outR[i] = std::isfinite (outR[i]) ? outR[i] * g : 0.0f;
        outPk = juce::jmax (outPk, std::abs (outL[i]), std::abs (outR[i]));
    }
    outputPeak = juce::jmax (outputPeak.load(), outPk);
}

// ---- files -------------------------------------------------------------------------------------------

juce::String RigProcessor::loadNamModel (const juce::File& f)
{
    auto err = nam.load (f);
    if (err.isEmpty())
        setParam (pid::ampModel, (float) (int) AmpType::namCapture);
    return err;
}

void RigProcessor::unloadNamModel()
{
    nam.unload();
}

bool RigProcessor::loadImpulseResponse (const juce::File& f)
{
    if (! cab.loadImpulseResponse (f))
        return false;
    setParam (pid::cabType, (float) (int) CabType::impulseResponse);
    setParam (pid::cabOn, 1.0f);
    return true;
}

void RigProcessor::setParam (const juce::String& id, float value)
{
    if (auto* param = apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// ---- state & presets -----------------------------------------------------------------------------

juce::ValueTree RigProcessor::createPresetState()
{
    auto state = apvts.copyState();
    state.setProperty ("namFile", nam.getFile().getFullPathName(), nullptr);
    state.setProperty ("irFile", cab.getImpulseResponseFile().getFullPathName(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    return state;
}

void RigProcessor::restorePresetState (const juce::ValueTree& state)
{
    if (! state.isValid() || state.getType() != apvts.state.getType())
        return;

    // Load files first so their "select me" side effects are overridden by the preset's own choices.
    const juce::File namFile (state.getProperty ("namFile").toString());
    if (namFile.existsAsFile() && namFile != nam.getFile())
        nam.load (namFile);

    const juce::File irFile (state.getProperty ("irFile").toString());
    if (irFile.existsAsFile() && irFile != cab.getImpulseResponseFile())
        cab.loadImpulseResponse (irFile);

    presetName = state.getProperty ("presetName").toString();
    apvts.replaceState (state.createCopy());
}

void RigProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createPresetState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RigProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        restorePresetState (juce::ValueTree::fromXml (*xml));
}

juce::File RigProcessor::userPresetDirectory()
{
    auto dir = ModelManager::appDataDirectory().getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::String RigProcessor::saveUserPreset (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return "Please enter a preset name.";

    auto file = userPresetDirectory().getChildFile (clean + ".wisrig");
    presetName = clean;
    auto xml = createPresetState().createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return "Couldn't save " + file.getFullPathName();
    return {};
}

juce::String RigProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return "Couldn't read " + file.getFileName();
    restorePresetState (juce::ValueTree::fromXml (*xml));
    presetName = file.getFileNameWithoutExtension();
    return {};
}

juce::StringArray RigProcessor::factoryPresetNames()
{
    return {
        "Guitar - Clean Shimmer",
        "Guitar - Blues Breakup",
        "Guitar - Classic Rock Crunch",
        "Guitar - Modern High Gain",
        "Guitar - Lead Solo",
        "Bass - Clean DI",
        "Bass - Vintage Tube",
        "Bass - Modern Grit",
        "Vocal Mic",
        "Acoustic / Keys DI"
    };
}

void RigProcessor::loadFactoryPreset (int index)
{
    // start from defaults
    for (auto* id : allParamIds)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->getDefaultValue());

    presetName = factoryPresetNames()[index];
    auto set = [this] (const char* id, float v) { setParam (id, v); };
    auto amp = [&] (AmpType t, float g, float b, float m, float tr, float pr, float ms)
    {
        set (pid::ampOn, 1); set (pid::ampModel, (float) (int) t);
        set (pid::ampGain, g); set (pid::ampBass, b); set (pid::ampMid, m); set (pid::ampTreble, tr);
        set (pid::ampPresence, pr); set (pid::ampMaster, ms);
    };
    auto cabSet = [&] (CabType t, float lo, float hi) { set (pid::cabOn, 1); set (pid::cabType, (float) (int) t); set (pid::cabLowCut, lo); set (pid::cabHighCut, hi); };
    auto comp = [&] (float th, float ratio, float att, float rel, float lvl)
    {
        set (pid::compOn, 1); set (pid::compThresh, th); set (pid::compRatio, ratio); set (pid::compAttack, att); set (pid::compRelease, rel); set (pid::compLevel, lvl);
    };
    auto rev = [&] (float size, float damp, float pre, float mix) { set (pid::reverbOn, mix > 0 ? 1.0f : 0.0f); set (pid::reverbSize, size); set (pid::reverbDamp, damp); set (pid::reverbPreDelay, pre); set (pid::reverbMix, mix); };

    switch (index)
    {
        case 0: // Clean Shimmer
            amp (AmpType::cleanCombo, 3.5f, 5.5f, 5.0f, 6.2f, 6.0f, 6.8f);
            cabSet (CabType::american2x12, 70, 11000);
            comp (-26, 3.5f, 12, 160, 5);
            set (pid::chorusOn, 1); set (pid::chorusRate, 0.6f); set (pid::chorusDepth, 0.3f); set (pid::chorusMix, 0.35f);
            rev (0.65f, 0.4f, 25, 0.26f);
            break;
        case 1: // Blues Breakup
            amp (AmpType::cleanCombo, 7.2f, 5.0f, 6.0f, 5.5f, 5.0f, 5.0f);
            set (pid::driveOn, 1); set (pid::driveType, 0); set (pid::driveAmount, 2.5f); set (pid::driveTone, 5.5f); set (pid::driveLevel, 6.5f);
            cabSet (CabType::openBack1x12, 80, 9000);
            rev (0.5f, 0.55f, 15, 0.18f);
            break;
        case 2: // Classic Rock Crunch
            amp (AmpType::britCrunch, 6.0f, 5.0f, 6.5f, 6.0f, 5.5f, 5.0f);
            cabSet (CabType::brit4x12, 80, 8500);
            set (pid::gateThresh, -62);
            rev (0.4f, 0.5f, 10, 0.12f);
            break;
        case 3: // Modern High Gain
            set (pid::driveOn, 1); set (pid::driveType, 0); set (pid::driveAmount, 0.8f); set (pid::driveTone, 6.5f); set (pid::driveLevel, 7.5f);
            amp (AmpType::hotLead, 6.5f, 5.5f, 4.2f, 6.0f, 6.0f, 5.3f);
            cabSet (CabType::brit4x12, 90, 8000);
            set (pid::gateThresh, -54); set (pid::gateRelease, 40);
            rev (0.35f, 0.6f, 8, 0.08f);
            break;
        case 4: // Lead Solo
            amp (AmpType::britCrunch, 8.0f, 5.0f, 7.0f, 6.0f, 6.0f, 5.0f);
            set (pid::driveOn, 1); set (pid::driveType, 0); set (pid::driveAmount, 4.0f); set (pid::driveTone, 6.0f); set (pid::driveLevel, 6.5f);
            cabSet (CabType::brit4x12, 85, 8500);
            set (pid::delayOn, 1); set (pid::delayTime, 420); set (pid::delayFeedback, 0.35f); set (pid::delayMix, 0.22f); set (pid::delayPingPong, 1);
            set (pid::gateThresh, -60);
            rev (0.6f, 0.5f, 20, 0.2f);
            break;
        case 5: // Bass Clean DI
            amp (AmpType::flatDi, 5.0f, 5.5f, 5.0f, 5.0f, 5.0f, 6.8f);
            set (pid::cabOn, 0);
            comp (-22, 4.0f, 15, 180, 4);
            set (pid::gateThresh, -70);
            rev (0.3f, 0.6f, 0, 0.0f);
            break;
        case 6: // Bass Vintage Tube
            amp (AmpType::bassTube, 5.0f, 6.0f, 5.5f, 4.5f, 4.5f, 5.8f);
            cabSet (CabType::bass8x10, 35, 6500);
            comp (-24, 3.0f, 20, 200, 4);
            set (pid::gateThresh, -70);
            rev (0.3f, 0.6f, 0, 0.0f);
            break;
        case 7: // Bass Modern Grit
            amp (AmpType::bassModern, 6.0f, 6.0f, 5.0f, 6.0f, 6.0f, 5.8f);
            cabSet (CabType::bass4x10, 40, 7000);
            comp (-26, 5.0f, 10, 150, 5);
            set (pid::gateThresh, -66);
            rev (0.3f, 0.6f, 0, 0.0f);
            break;
        case 8: // Vocal Mic
            set (pid::ampOn, 0); set (pid::cabOn, 0);
            set (pid::gateThresh, -56); set (pid::gateRelease, 150);
            comp (-24, 3.0f, 6, 120, 6);
            set (pid::eqOn, 1); set (pid::eqLow, -6); set (pid::eqLowMid, -2); set (pid::eqHighMid, 2); set (pid::eqHigh, 3);
            set (pid::delayOn, 1); set (pid::delayTime, 110); set (pid::delayFeedback, 0.1f); set (pid::delayMix, 0.08f);
            rev (0.55f, 0.35f, 30, 0.22f);
            set (pid::outLevel, 3.0f);
            break;
        case 9: // Acoustic / Keys DI
            amp (AmpType::flatDi, 5.0f, 5.0f, 5.0f, 5.5f, 5.5f, 7.2f);
            set (pid::cabOn, 0);
            comp (-20, 2.5f, 15, 200, 3);
            set (pid::gateThresh, -72);
            rev (0.5f, 0.4f, 15, 0.15f);
            break;
        default: break;
    }
}

} // namespace wis
