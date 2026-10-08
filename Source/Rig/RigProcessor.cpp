#include "RigProcessor.h"
#include "Separation/ModelManager.h"
#include "Daw/Plugins/BuiltinProcessor.h"

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
    pid::outLevel,
    pid::charType, pid::charAmount, pid::cabMic, pid::cabMicPos, pid::cabRoom, pid::cabDiBlend, pid::tapeOn, pid::tapeDrive
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

    l.add (std::make_unique<APC> (juce::ParameterID { pid::charType, 2 }, "Strings & Pickups", characterTypeNames(), 0));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::charAmount, 2 }, "Amount", knob, 7.0f));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::cabMic, 2 }, "Mic", micTypeNames(), 0));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::cabMicPos, 2 }, "Mic Position", knob, 3.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::cabRoom, 2 }, "Room", knob, 1.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::cabDiBlend, 2 }, "DI Blend", pct, 0.0f));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::tapeOn, 2 }, "Console & Tape", false));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::tapeDrive, 2 }, "Tape Drive", knob, 4.0f));

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
    resetBoard();
}

RigProcessor::~RigProcessor()
{
    const juce::SpinLock::ScopedLockType sl (chainLock);
    audioChain = nullptr;
    chain = nullptr;
}

void RigProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (16, samplesPerBlock);

    mono.assign ((size_t) maxBlock, 0.0f);
    wetL.assign ((size_t) maxBlock, 0.0f);
    wetR.assign ((size_t) maxBlock, 0.0f);

    character.prepare (sr);
    tape.prepare (sr);
    diCopy.assign ((size_t) maxBlock, 0.0f);
    diLine.assign (4096, 0.0f);
    diWrite = 0;
    tapeR.prepare (sr);
    gate.prepare (sr);
    gateR.prepare (sr);
    compressor.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    drive.prepare (sr, maxBlock);
    amp.prepare (sr, maxBlock);
    nam.prepare (sr, maxBlock);
    cab.setParameters ((CabType) (int) p (pid::cabType), p (pid::cabLowCut), p (pid::cabHighCut),
                       (MicType) (int) p (pid::cabMic), p (pid::cabMicPos) / 10.0f, p (pid::cabRoom) / 10.0f);
    cab.prepare (sr, maxBlock);
    for (auto& f : eq) { f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
    for (size_t b = 0; b < eq.size(); ++b) { eqR[b].coefficients = eq[b].coefficients; eqR[b].reset(); }   // shared coefficients
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

    pedalMidi.ensureSize (256);
    for (auto& e : copyEntries())
        if (e.pedal != nullptr && e.pedal->proc != nullptr)
            preparePedal (*e.pedal->proc);
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

    // ---- strings & pickups ----
    character.setParameters ((CharacterType) (int) p (pid::charType), p (pid::charAmount));
    character.process (x, n);

    // ---- the pedalboard, in the order the player chose ----
    {
        const juce::SpinLock::ScopedTryLockType sl (chainLock);
        if (sl.isLocked() && audioChain != chain)
            audioChain = chain;   // the old chain stays alive in the graveyard: nothing is freed on this thread
    }

    // Mono until something stereo (a pedal, chorus, delay, reverb); a stereo signal going into the drive or the
    // amp is summed to mono, like plugging a stereo pedal into an amp's single input.
    bool stereo = false;
    auto toStereo = [&] { if (! stereo) { std::copy (x, x + n, outL); std::copy (x, x + n, outR); stereo = true; } };
    auto toMono = [&] { if (stereo) { for (int i = 0; i < n; ++i) x[i] = 0.5f * (outL[i] + outR[i]); stereo = false; } };
    auto on = [this] (const char* id) { return p (id) > 0.5f; };

    // DI tap: a studio DI box just before the drive / amp (whichever comes first), blended back in after the cab
    const float diBlend = p (pid::cabDiBlend);
    bool diTapped = false;
    float diLatency = 0.0f;
    auto tapDi = [&] { if (diBlend > 0.001f && ! diTapped) { std::copy (x, x + n, diCopy.begin()); diTapped = true; diLatency = 0.0f; } };

    if (audioChain != nullptr)
    {
        for (auto& e : audioChain->entries)
        {
            if (e.pedal != nullptr)
            {
                auto* proc = e.pedal->proc.get();
                if (proc == nullptr || e.pedal->bypass.load()) continue;
                toStereo();
                float* chans[] = { outL, outR };
                juce::AudioBuffer<float> buf (chans, 2, n);
                pedalMidi.clear();
                proc->setPlayHead (getPlayHead());
                proc->processBlock (buf, pedalMidi);
                if (diTapped) diLatency += (float) proc->getLatencySamples();
                continue;
            }

            const auto block = (RigBlock) e.block;
            switch (block)
            {
                case RigBlock::gate:
                case RigBlock::comp:
                case RigBlock::eq:
                case RigBlock::tape:
                    if (on (rigBlockPowerParam (block)))
                    {
                        if (stereo) { float* chans[] = { outL, outR }; processCoreBlock (block, chans, 2, n); }
                        else        { float* chans[] = { x };          processCoreBlock (block, chans, 1, n); }
                    }
                    break;

                case RigBlock::drive:
                    if (on (pid::driveOn))
                    {
                        toMono();
                        tapDi();
                        drive.setParameters ((DriveType) (int) p (pid::driveType), p (pid::driveAmount), p (pid::driveTone), p (pid::driveLevel));
                        drive.process (x, n);
                        if (diTapped) diLatency += drive.getLatencySamples();
                    }
                    break;

                case RigBlock::ampCab:
                {
                    const bool ampOn = on (pid::ampOn), cabOn = on (pid::cabOn);
                    if (! ampOn && ! cabOn && diBlend <= 0.001f) break;
                    toMono();
                    tapDi();
                    const auto model = (AmpType) (int) p (pid::ampModel);
                    if (ampOn)
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
                    if (cabOn)
                    {
                        cab.setParameters ((CabType) (int) p (pid::cabType), p (pid::cabLowCut), p (pid::cabHighCut),
                                           (MicType) (int) p (pid::cabMic), p (pid::cabMicPos) / 10.0f, p (pid::cabRoom) / 10.0f);
                        cab.process (x, n);
                    }
                    if (diTapped)
                    {
                        // blend the clean DI under the miked amp, like a studio bass recording. The DI is delayed to arrive
                        // together with the amp + cabinet (otherwise the two comb-filter each other), and level matched.
                        float delaySamples = diLatency + (cabOn ? (float) cab.getArrivalSamples() : 0.0f);
                        if (ampOn && model != AmpType::namCapture) delaySamples += amp.getLatencySamples();
                        delaySamples = juce::jlimit (0.0f, (float) diLine.size() - 2.0f, delaySamples);
                        const float wet = std::cos (diBlend * juce::MathConstants<float>::halfPi), dry = std::sin (diBlend * juce::MathConstants<float>::halfPi) * 1.4f;
                        const int mask = (int) diLine.size() - 1;
                        const int d0 = (int) std::floor (delaySamples);
                        const float frac = delaySamples - (float) d0;
                        for (int i = 0; i < n; ++i)
                        {
                            diLine[(size_t) diWrite] = diCopy[(size_t) i];
                            const float a = diLine[(size_t) ((diWrite - d0) & mask)], b = diLine[(size_t) ((diWrite - d0 - 1) & mask)];
                            diWrite = (diWrite + 1) & mask;
                            x[i] = x[i] * wet + (a + (b - a) * frac) * dry;
                        }
                        diTapped = false;
                    }
                    break;
                }

                case RigBlock::chorus:
                    if (on (pid::chorusOn))
                    {
                        toStereo();
                        chorus.setRate (p (pid::chorusRate));
                        chorus.setDepth (p (pid::chorusDepth));
                        chorus.setCentreDelay (7.0f);
                        chorus.setFeedback (0.0f);
                        chorus.setMix (p (pid::chorusMix));
                        float* chans[] = { outL, outR };
                        juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
                        chorus.process (juce::dsp::ProcessContextReplacing<float> (blk));
                    }
                    break;

                case RigBlock::delay:
                    if (on (pid::delayOn))
                    {
                        toStereo();
                        delay.setParameters (p (pid::delayTime), p (pid::delayFeedback), p (pid::delayTone), p (pid::delayMix), p (pid::delayPingPong) > 0.5f);
                        delay.process (outL, outR, n);
                    }
                    break;

                case RigBlock::reverb:
                {
                    // parallel send
                    reverbMixSm.setTargetValue (on (pid::reverbOn) ? p (pid::reverbMix) : 0.0f);
                    if (on (pid::reverbOn) || reverbMixSm.isSmoothing())
                    {
                        toStereo();
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
                        juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
                        reverb.process (juce::dsp::ProcessContextReplacing<float> (blk));

                        for (int i = 0; i < n; ++i)
                        {
                            const float m = reverbMixSm.getNextValue();
                            const float dry = std::cos (m * juce::MathConstants<float>::halfPi * 0.5f);   // gentle dry dip
                            outL[i] = outL[i] * dry + wetL[(size_t) i] * m * 0.6f;
                            outR[i] = outR[i] * dry + wetR[(size_t) i] * m * 0.6f;
                        }
                    }
                    break;
                }

                case RigBlock::count:
                default: break;
            }
        }
    }
    toStereo();

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

void RigProcessor::processCoreBlock (RigBlock b, float* const* chans, int numCh, int n)
{
    switch (b)
    {
        case RigBlock::gate:
            gate.setParameters (p (pid::gateThresh), p (pid::gateRelease));
            gate.process (chans[0], n);
            if (numCh > 1) { gateR.setParameters (p (pid::gateThresh), p (pid::gateRelease)); gateR.process (chans[1], n); }
            break;

        case RigBlock::comp:
        {
            compressor.setThreshold (p (pid::compThresh));
            compressor.setRatio (p (pid::compRatio));
            compressor.setAttack (p (pid::compAttack));
            compressor.setRelease (p (pid::compRelease));
            juce::dsp::AudioBlock<float> block (chans, (size_t) numCh, (size_t) n);
            compressor.process (juce::dsp::ProcessContextReplacing<float> (block));
            compMakeup.setTargetValue (juce::Decibels::decibelsToGain (p (pid::compLevel)));
            for (int i = 0; i < n; ++i)
            {
                const float g = compMakeup.getNextValue();
                for (int c = 0; c < numCh; ++c) chans[c][i] *= g;
            }
            break;
        }

        case RigBlock::eq:
        {
            const float gains[] = { p (pid::eqLow), p (pid::eqLowMid), p (pid::eqHighMid), p (pid::eqHigh) };
            using AC = juce::dsp::IIR::ArrayCoefficients<float>;
            for (int k = 0; k < 4; ++k)
            {
                if (gains[k] != eqCache[(size_t) k])
                {
                    eqCache[(size_t) k] = gains[k];
                    const float g = juce::Decibels::decibelsToGain (gains[k]);
                    switch (k)
                    {
                        case 0: *eq[0].coefficients = AC::makeLowShelf (sr, 100.0f, 0.7f, g); break;
                        case 1: *eq[1].coefficients = AC::makePeakFilter (sr, 400.0f, 0.9f, g); break;
                        case 2: *eq[2].coefficients = AC::makePeakFilter (sr, 2000.0f, 0.9f, g); break;
                        default:*eq[3].coefficients = AC::makeHighShelf (sr, 6000.0f, 0.7f, g); break;
                    }
                }
            }
            for (int c = 0; c < numCh; ++c)
            {
                auto& f = c == 0 ? eq : eqR;
                float* d = chans[c];
                for (int i = 0; i < n; ++i)
                    d[i] = f[3].processSample (f[2].processSample (f[1].processSample (f[0].processSample (d[i]))));
            }
            break;
        }

        case RigBlock::tape:
            tape.setDrive (p (pid::tapeDrive));
            tape.process (chans[0], n);
            if (numCh > 1) { tapeR.setDrive (p (pid::tapeDrive)); tapeR.process (chans[1], n); }
            break;

        default: break;
    }
}

// ---- pedalboard ----------------------------------------------------------------------------------------

const char* rigBlockKey (RigBlock b)
{
    static const char* keys[] = { "gate", "comp", "drive", "ampcab", "eq", "tape", "chorus", "delay", "reverb" };
    return juce::isPositiveAndBelow ((int) b, (int) RigBlock::count) ? keys[(int) b] : "";
}

juce::String rigBlockName (RigBlock b)
{
    static const char* names[] = { "Gate", "Comp", "Drive", "Amp + Cab", "EQ", "Tape", "Chorus", "Delay", "Reverb" };
    return juce::isPositiveAndBelow ((int) b, (int) RigBlock::count) ? names[(int) b] : "";
}

const char* rigBlockPowerParam (RigBlock b)
{
    switch (b)
    {
        case RigBlock::gate:   return pid::gateOn;
        case RigBlock::comp:   return pid::compOn;
        case RigBlock::drive:  return pid::driveOn;
        case RigBlock::ampCab: return pid::ampOn;
        case RigBlock::eq:     return pid::eqOn;
        case RigBlock::tape:   return pid::tapeOn;
        case RigBlock::chorus: return pid::chorusOn;
        case RigBlock::delay:  return pid::delayOn;
        case RigBlock::reverb: return pid::reverbOn;
        default:               return pid::ampOn;
    }
}

static int blockForKey (const juce::String& key)
{
    for (int b = 0; b < (int) RigBlock::count; ++b)
        if (key == rigBlockKey ((RigBlock) b)) return b;
    return -1;
}

juce::StringArray RigProcessor::pedalIds()
{
    // every built-in audio effect except the rig itself and the ones that need the Studio (side-chain, transport)
    static const juce::StringArray excluded { "amprig", "vocoder", "beatrepeat" };
    juce::StringArray ids;
    for (auto& b : daw::builtinPlugins())
        if (! b.instrument && ! b.midiFx && b.category != "Hidden" && ! excluded.contains (b.id))
            ids.add (b.id);
    return ids;
}

std::vector<RigChain::Entry> RigProcessor::copyEntries() const
{
    const juce::SpinLock::ScopedLockType sl (chainLock);
    return chain != nullptr ? chain->entries : std::vector<RigChain::Entry> {};
}

void RigProcessor::publishChain (std::vector<RigChain::Entry> entries)
{
    // pedals leaving the board: their editors must close before the processors can go
    for (auto& old : copyEntries())
    {
        if (old.pedal == nullptr) continue;
        const bool kept = std::any_of (entries.begin(), entries.end(), [&] (const RigChain::Entry& e) { return e.pedal == old.pedal; });
        if (! kept)
        {
            const auto uid = old.pedal->uid;
            boardListeners.call ([&] (BoardListener& l) { l.pedalRemoved (uid); });
        }
    }

    RigChain::Ptr c = new RigChain();
    c->entries = std::move (entries);
    {
        const juce::SpinLock::ScopedLockType sl (chainLock);
        chain = c;
    }
    chainGraveyard.add (c);
    // free the chains (and removed pedals) the audio thread no longer uses
    for (int i = chainGraveyard.size(); --i >= 0;)
        if (chainGraveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            chainGraveyard.remove (i);
    boardChanged.sendChangeMessage();
}

void RigProcessor::preparePedal (juce::AudioProcessor& proc)
{
    proc.setPlayConfigDetails (2, 2, sr, maxBlock);
    proc.prepareToPlay (sr, maxBlock);
}

std::vector<RigProcessor::BoardItem> RigProcessor::getBoard() const
{
    std::vector<BoardItem> items;
    for (auto& e : copyEntries())
    {
        BoardItem it;
        if (e.pedal != nullptr)
        {
            it.key = e.pedal->uid;
            it.name = e.pedal->name;
            it.builtinId = e.pedal->builtinId;
            it.isPedal = true;
            it.on = ! e.pedal->bypass.load();
        }
        else
        {
            it.block = e.block;
            it.key = rigBlockKey ((RigBlock) e.block);
            it.name = rigBlockName ((RigBlock) e.block);
            it.on = isBoardItemOn (it.key);
        }
        items.push_back (it);
    }
    return items;
}

void RigProcessor::moveBoardItem (int from, int to)
{
    auto entries = copyEntries();
    if (! juce::isPositiveAndBelow (from, (int) entries.size())) return;
    to = juce::jlimit (0, (int) entries.size() - 1, to);
    if (from == to) return;
    auto e = entries[(size_t) from];
    entries.erase (entries.begin() + from);
    entries.insert (entries.begin() + to, e);
    publishChain (std::move (entries));
}

juce::String RigProcessor::addPedal (const juce::String& builtinId, int insertAt, const juce::String& stateBase64)
{
    auto entries = copyEntries();
    int pedals = 0;
    for (auto& e : entries) if (e.pedal != nullptr) ++pedals;
    if (pedals >= maxPedals || ! pedalIds().contains (builtinId)) return {};

    auto proc = daw::createBuiltin (builtinId);
    if (proc == nullptr) return {};
    RigPedal::Ptr pedal = new RigPedal();
    pedal->builtinId = builtinId;
    pedal->uid = "p" + juce::String (++pedalCounter) + "_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()).substring (0, 4);
    if (auto* info = daw::findBuiltin (builtinId)) pedal->name = info->name;
    preparePedal (*proc);
    if (stateBase64.isNotEmpty()) daw::decodeState (*proc, stateBase64);
    pedal->proc = std::move (proc);

    if (insertAt < 0)
    {
        // by default a new pedal goes in front of the amp, where stomp boxes live (after the drive if it's there)
        insertAt = (int) entries.size();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].pedal == nullptr && entries[i].block == (int) RigBlock::ampCab) { insertAt = (int) i; break; }
    }
    insertAt = juce::jlimit (0, (int) entries.size(), insertAt);
    entries.insert (entries.begin() + insertAt, RigChain::Entry { -1, pedal });
    publishChain (std::move (entries));
    return pedal->uid;
}

void RigProcessor::removePedal (const juce::String& uid)
{
    auto entries = copyEntries();
    const auto before = entries.size();
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const RigChain::Entry& e) { return e.pedal != nullptr && e.pedal->uid == uid; }),
                   entries.end());
    if (entries.size() != before) publishChain (std::move (entries));
}

juce::AudioProcessor* RigProcessor::getPedalProcessor (const juce::String& uid) const
{
    for (auto& e : copyEntries())
        if (e.pedal != nullptr && e.pedal->uid == uid) return e.pedal->proc.get();
    return nullptr;
}

void RigProcessor::setBoardItemOn (const juce::String& key, bool shouldBeOn)
{
    const int b = blockForKey (key);
    if (b >= 0)
    {
        setParam (rigBlockPowerParam ((RigBlock) b), shouldBeOn ? 1.0f : 0.0f);
        if ((RigBlock) b == RigBlock::ampCab) setParam (pid::cabOn, shouldBeOn ? 1.0f : 0.0f);
        return;
    }
    for (auto& e : copyEntries())
        if (e.pedal != nullptr && e.pedal->uid == key) e.pedal->bypass = ! shouldBeOn;
}

bool RigProcessor::isBoardItemOn (const juce::String& key) const
{
    const int b = blockForKey (key);
    if (b >= 0)
    {
        if ((RigBlock) b == RigBlock::ampCab) return p (pid::ampOn) > 0.5f || p (pid::cabOn) > 0.5f;
        return p (rigBlockPowerParam ((RigBlock) b)) > 0.5f;
    }
    for (auto& e : copyEntries())
        if (e.pedal != nullptr && e.pedal->uid == key) return ! e.pedal->bypass.load();
    return false;
}

void RigProcessor::resetBoard()
{
    std::vector<RigChain::Entry> entries;
    for (int b = 0; b < (int) RigBlock::count; ++b)
        entries.push_back ({ b, nullptr });
    publishChain (std::move (entries));
}

juce::ValueTree RigProcessor::saveBoard() const
{
    juce::ValueTree board ("PEDALBOARD");
    for (auto& e : copyEntries())
    {
        juce::ValueTree item ("ITEM");
        if (e.pedal != nullptr)
        {
            item.setProperty ("pedal", e.pedal->builtinId, nullptr);
            item.setProperty ("bypass", e.pedal->bypass.load(), nullptr);
            if (e.pedal->proc != nullptr) item.setProperty ("state", daw::encodeState (*e.pedal->proc), nullptr);
        }
        else
        {
            item.setProperty ("block", rigBlockKey ((RigBlock) e.block), nullptr);
        }
        board.appendChild (item, nullptr);
    }
    return board;
}

void RigProcessor::loadBoard (const juce::ValueTree& board)
{
    std::vector<RigChain::Entry> entries;
    bool seen[(int) RigBlock::count] = {};
    int pedals = 0;
    if (board.isValid())
    {
        for (auto item : board)
        {
            if (item.hasProperty ("block"))
            {
                const int b = blockForKey (item["block"].toString());
                if (b >= 0 && ! seen[b]) { seen[b] = true; entries.push_back ({ b, nullptr }); }
            }
            else if (item.hasProperty ("pedal") && pedals < maxPedals)
            {
                const auto id = item["pedal"].toString();
                if (! pedalIds().contains (id)) continue;
                auto proc = daw::createBuiltin (id);
                if (proc == nullptr) continue;
                RigPedal::Ptr pedal = new RigPedal();
                pedal->builtinId = id;
                pedal->uid = "p" + juce::String (++pedalCounter) + "_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()).substring (0, 4);
                if (auto* info = daw::findBuiltin (id)) pedal->name = info->name;
                preparePedal (*proc);
                if (item.hasProperty ("state")) daw::decodeState (*proc, item["state"].toString());
                pedal->proc = std::move (proc);
                pedal->bypass = (bool) item.getProperty ("bypass", false);
                entries.push_back ({ -1, pedal });
                ++pedals;
            }
        }
    }
    // presets saved before 3.3 (or missing blocks): the standard order
    for (int b = 0; b < (int) RigBlock::count; ++b)
    {
        if (seen[b]) continue;
        // keep the standard order: insert before the first later standard block that's already there
        auto pos = entries.end();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->pedal == nullptr && it->block > b) { pos = it; break; }
        entries.insert (pos, { b, nullptr });
    }
    publishChain (std::move (entries));
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
    state.removeChild (state.getChildWithName ("PEDALBOARD"), nullptr);
    state.appendChild (saveBoard(), nullptr);
    state.setProperty ("namFile", nam.getFile().getFullPathName(), nullptr);
    state.setProperty ("irFile", cab.getImpulseResponseFile().getFullPathName(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("rigVersion", 2, nullptr);
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
    auto copy = state.createCopy();
    if ((int) copy.getProperty ("rigVersion", 1) < 2)
    {
        // saved before 3.1: the amp and cabinet lists have grown and been reordered
        for (auto child : copy)
        {
            const auto id = child.getProperty ("id").toString();
            if (id == pid::ampModel) child.setProperty ("value", migrateAmpTypeV1 ((int) child.getProperty ("value")), nullptr);
            if (id == pid::cabType)  child.setProperty ("value", migrateCabTypeV1 ((int) child.getProperty ("value")), nullptr);
        }
        copy.setProperty ("rigVersion", 2, nullptr);
    }
    const auto board = copy.getChildWithName ("PEDALBOARD").createCopy();
    copy.removeChild (copy.getChildWithName ("PEDALBOARD"), nullptr);
    apvts.replaceState (copy);
    loadBoard (board);
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
        "Guitar - American Clean",
        "Guitar - Classic Rock Crunch",
        "Guitar - Blues Breakup",
        "Guitar - 60s Merseybeat Jangle",
        "Guitar - Modern High Gain",
        "Guitar - Lead Solo",
        "Guitar - Smooth Blues Lead",
        "Guitar - Dream Pop Wash",
        "Guitar - Garage Fuzz",
        "Bass - 60s Merseybeat (violin bass)",
        "Bass - Late 60s Studio (DI + amp)",
        "Bass - Motown Flatwound",
        "Bass - Classic Rock 8x10",
        "Bass - Modern Growl",
        "Bass - Modern Clean Hi-Fi",
        "Bass - Punk Pick",
        "Bass - Dub Deep",
        "Bass - Fuzz Bass",
        "Bass - Clean DI",
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
    resetBoard();
    auto set = [this] (const char* id, float v) { setParam (id, v); };
    auto amp = [&] (AmpType t, float g, float b, float m, float tr, float pr, float ms)
    {
        set (pid::ampOn, 1); set (pid::ampModel, (float) (int) t);
        set (pid::ampGain, g); set (pid::ampBass, b); set (pid::ampMid, m); set (pid::ampTreble, tr);
        set (pid::ampPresence, pr); set (pid::ampMaster, ms);
    };
    auto cabSet = [&] (CabType t, MicType mic, float pos, float room, float lo, float hi)
    {
        set (pid::cabOn, 1); set (pid::cabType, (float) (int) t); set (pid::cabMic, (float) (int) mic);
        set (pid::cabMicPos, pos); set (pid::cabRoom, room); set (pid::cabLowCut, lo); set (pid::cabHighCut, hi);
    };
    auto comp = [&] (float th, float ratio, float att, float rel, float lvl)
    {
        set (pid::compOn, 1); set (pid::compThresh, th); set (pid::compRatio, ratio); set (pid::compAttack, att); set (pid::compRelease, rel); set (pid::compLevel, lvl);
    };
    auto driveSet = [&] (DriveType t, float d, float tone, float lvl) { set (pid::driveOn, 1); set (pid::driveType, (float) (int) t); set (pid::driveAmount, d); set (pid::driveTone, tone); set (pid::driveLevel, lvl); };
    auto chr = [&] (CharacterType t, float amt) { set (pid::charType, (float) (int) t); set (pid::charAmount, amt); };
    auto tapeSet = [&] (float drv) { set (pid::tapeOn, 1); set (pid::tapeDrive, drv); };
    auto rev = [&] (float size, float damp, float pre, float mix) { set (pid::reverbOn, mix > 0 ? 1.0f : 0.0f); set (pid::reverbSize, size); set (pid::reverbDamp, damp); set (pid::reverbPreDelay, pre); set (pid::reverbMix, mix); };
    auto bassBasics = [&] { set (pid::gateThresh, -70); rev (0.3f, 0.6f, 0, 0.0f); };

    switch (index)
    {
        case 0: // Clean Shimmer
            amp (AmpType::americanClean, 3.5f, 5.5f, 5.0f, 6.2f, 6.0f, 6.8f);
            cabSet (CabType::american2x12, MicType::condenser, 3, 2, 70, 12000);
            comp (-26, 3.5f, 12, 160, 5);
            set (pid::chorusOn, 1); set (pid::chorusRate, 0.6f); set (pid::chorusDepth, 0.3f); set (pid::chorusMix, 0.35f);
            rev (0.65f, 0.4f, 25, 0.26f);
            set (pid::outLevel, 0.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 1: // American Clean
            amp (AmpType::americanClean, 4.5f, 5.5f, 4.5f, 6.0f, 5.5f, 6.8f);
            cabSet (CabType::american2x12, MicType::dynamic, 3, 1, 70, 11000);
            rev (0.55f, 0.45f, 18, 0.2f);
            set (pid::outLevel, -6.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 2: // Classic Rock Crunch
            amp (AmpType::britishCrunch, 6.0f, 5.0f, 6.5f, 6.0f, 5.5f, 5.0f);
            cabSet (CabType::brit4x12, MicType::dynamic, 3, 1, 80, 9000);
            set (pid::gateThresh, -62);
            rev (0.4f, 0.5f, 10, 0.12f);
            set (pid::outLevel, -1.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 3: // Blues Breakup
            amp (AmpType::tweedBreakup, 5.5f, 5.0f, 6.0f, 5.5f, 5.0f, 5.5f);
            cabSet (CabType::tweed1x12, MicType::dynamicPlusRibbon, 4, 2, 80, 9000);
            rev (0.5f, 0.55f, 15, 0.18f);
            set (pid::outLevel, -0.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 4: // 60s Merseybeat Jangle: chimey class-A combo, blue speakers, a bit of tape
            amp (AmpType::britishChime, 4.5f, 4.5f, 5.0f, 6.0f, 5.5f, 6.0f);
            cabSet (CabType::britBlue2x12, MicType::dynamic, 3, 3, 90, 11000);
            chr (CharacterType::singleCoils, 5);
            comp (-22, 2.5f, 15, 150, 3);
            tapeSet (4);
            rev (0.45f, 0.5f, 12, 0.16f);
            set (pid::outLevel, -2.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 5: // Modern High Gain
            driveSet (DriveType::overdrive, 0.8f, 6.5f, 7.5f);
            amp (AmpType::modernHighGain, 6.5f, 5.5f, 4.5f, 6.0f, 6.0f, 5.3f);
            cabSet (CabType::modern4x12, MicType::dynamic, 2, 0, 90, 8500);
            set (pid::gateThresh, -54); set (pid::gateRelease, 40);
            rev (0.35f, 0.6f, 8, 0.08f);
            set (pid::outLevel, -0.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 6: // Lead Solo
            amp (AmpType::britishLead, 6.5f, 5.0f, 6.5f, 6.0f, 6.0f, 5.0f);
            cabSet (CabType::brit4x12, MicType::dynamicPlusRibbon, 3, 2, 85, 9000);
            set (pid::delayOn, 1); set (pid::delayTime, 420); set (pid::delayFeedback, 0.35f); set (pid::delayMix, 0.22f); set (pid::delayPingPong, 1);
            set (pid::gateThresh, -60);
            rev (0.6f, 0.5f, 20, 0.2f);
            set (pid::outLevel, -1.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 7: // Smooth Blues Lead
            amp (AmpType::smoothOverdrive, 6.0f, 5.0f, 6.0f, 5.0f, 5.0f, 5.5f);
            cabSet (CabType::openBack1x12, MicType::ribbon, 3, 2, 80, 9000);
            chr (CharacterType::humbuckers, 4);
            set (pid::delayOn, 1); set (pid::delayTime, 380); set (pid::delayFeedback, 0.25f); set (pid::delayMix, 0.15f);
            rev (0.55f, 0.5f, 20, 0.18f);
            set (pid::outLevel, -2.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 8: // Dream Pop Wash
            amp (AmpType::americanClean, 5.0f, 5.0f, 5.0f, 5.5f, 5.0f, 6.5f);
            cabSet (CabType::american2x12, MicType::ribbon, 5, 4, 90, 9000);
            set (pid::chorusOn, 1); set (pid::chorusRate, 0.35f); set (pid::chorusDepth, 0.6f); set (pid::chorusMix, 0.5f);
            set (pid::delayOn, 1); set (pid::delayTime, 520); set (pid::delayFeedback, 0.45f); set (pid::delayTone, 2500); set (pid::delayMix, 0.3f); set (pid::delayPingPong, 1);
            tapeSet (5);
            rev (0.9f, 0.4f, 40, 0.42f);
            set (pid::outLevel, -1.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 9: // Garage Fuzz
            driveSet (DriveType::fuzz, 7.0f, 5.5f, 6.0f);
            amp (AmpType::britishChime, 5.0f, 5.0f, 5.0f, 5.5f, 5.0f, 5.5f);
            cabSet (CabType::britBlue2x12, MicType::dynamic, 2, 3, 90, 9000);
            rev (0.35f, 0.5f, 8, 0.12f);
            set (pid::outLevel, 0.5f);   // loudness-matched (rigtest --calibrate)
            break;

        case 10: // 60s Merseybeat bass: hollow-body violin bass with flats into a British valve amp and a 2x15, on tape
            chr (CharacterType::violinBass, 9);
            amp (AmpType::bassSixties, 4.5f, 6.0f, 5.5f, 4.0f, 4.0f, 6.0f);
            cabSet (CabType::bass2x15Sixties, MicType::dynamic, 5, 3, 40, 6000);
            comp (-24, 3.0f, 25, 220, 4);
            tapeSet (5);
            bassBasics();
            set (pid::outLevel, -2.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 11: // Late 60s studio: a bright pick bass, DI'd into the console with a little valve amp underneath
            chr (CharacterType::roundwoundsBright, 4);
            amp (AmpType::bassSixties, 3.5f, 5.5f, 6.0f, 5.5f, 5.0f, 6.0f);
            cabSet (CabType::bass1x15Vintage, MicType::condenser, 4, 2, 35, 8000);
            set (pid::cabDiBlend, 0.55f);
            comp (-24, 4.0f, 10, 160, 5);
            tapeSet (4);
            bassBasics();
            set (pid::outLevel, 0.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 12: // Motown flatwound: flats with a foam mute, flip-top 1x15, warm and fat
            chr (CharacterType::foamMute, 8);
            amp (AmpType::bassFlipTop, 5.0f, 6.0f, 5.0f, 4.5f, 4.5f, 6.0f);
            cabSet (CabType::bass1x15Vintage, MicType::ribbon, 5, 2, 35, 5500);
            comp (-26, 3.0f, 20, 200, 4);
            tapeSet (3);
            bassBasics();
            set (pid::outLevel, -3.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 13: // Classic Rock 8x10
            chr (CharacterType::precisionPickup, 5);
            amp (AmpType::bassClassicTube, 5.0f, 6.0f, 5.5f, 5.0f, 5.0f, 5.8f);
            cabSet (CabType::bass8x10, MicType::dynamic, 3, 1, 35, 7000);
            set (pid::cabDiBlend, 0.3f);
            comp (-24, 3.0f, 20, 200, 4);
            bassBasics();
            set (pid::outLevel, -2.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 14: // Modern Growl
            chr (CharacterType::roundwoundsBright, 5);
            amp (AmpType::bassModernGrowl, 6.0f, 6.0f, 5.5f, 6.0f, 6.0f, 5.8f);
            cabSet (CabType::bass4x10Horn, MicType::dynamic, 3, 0, 40, 9000);
            comp (-26, 5.0f, 10, 150, 5);
            set (pid::gateThresh, -66);
            rev (0.3f, 0.6f, 0, 0.0f);
            set (pid::outLevel, 2.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 15: // Modern Clean Hi-Fi: tight, punchy, piano-like (slap, fingerstyle, pop)
            chr (CharacterType::roundwoundsBright, 7);
            amp (AmpType::bassStudioDi, 5.0f, 5.5f, 4.5f, 6.0f, 6.0f, 6.5f);
            cabSet (CabType::bass2x10Modern, MicType::condenser, 3, 0, 30, 14000);
            set (pid::cabDiBlend, 0.5f);
            comp (-28, 6.0f, 6, 120, 6);
            bassBasics();
            set (pid::outLevel, 6.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 16: // Punk Pick: driven tube bass, mids up
            chr (CharacterType::precisionPickup, 6);
            amp (AmpType::bassClassicTube, 7.5f, 5.5f, 7.0f, 6.0f, 6.0f, 5.0f);
            cabSet (CabType::bass8x10, MicType::dynamic, 2, 1, 45, 7000);
            comp (-22, 4.0f, 8, 120, 4);
            bassBasics();
            set (pid::outLevel, 1.5f);   // loudness-matched (rigtest --calibrate)
            break;
        case 17: // Dub Deep: round, sub-heavy, no top
            chr (CharacterType::flatwounds, 8);
            amp (AmpType::bassFlipTop, 4.0f, 7.5f, 4.0f, 3.0f, 3.5f, 6.2f);
            cabSet (CabType::bass1x15Vintage, MicType::ribbon, 7, 2, 30, 3500);
            comp (-24, 3.0f, 30, 250, 4);
            bassBasics();
            set (pid::outLevel, -8.0f);   // loudness-matched (rigtest --calibrate)
            break;
        case 18: // Fuzz Bass
            driveSet (DriveType::bassDrive, 7.0f, 5.5f, 6.0f);
            amp (AmpType::bassClassicTube, 5.0f, 5.5f, 6.0f, 5.5f, 5.5f, 5.5f);
            cabSet (CabType::bass4x10Horn, MicType::dynamic, 3, 1, 40, 8000);
            set (pid::cabDiBlend, 0.25f);
            bassBasics();
            break;
        case 19: // Bass Clean DI
            amp (AmpType::flatDi, 5.0f, 5.5f, 5.0f, 5.0f, 5.0f, 6.8f);
            set (pid::cabOn, 0);
            comp (-22, 4.0f, 15, 180, 4);
            bassBasics();
            break;
        case 20: // Vocal Mic
            set (pid::ampOn, 0); set (pid::cabOn, 0);
            set (pid::gateThresh, -56); set (pid::gateRelease, 150);
            comp (-24, 3.0f, 6, 120, 6);
            set (pid::eqOn, 1); set (pid::eqLow, -6); set (pid::eqLowMid, -2); set (pid::eqHighMid, 2); set (pid::eqHigh, 3);
            set (pid::delayOn, 1); set (pid::delayTime, 110); set (pid::delayFeedback, 0.1f); set (pid::delayMix, 0.08f);
            rev (0.55f, 0.35f, 30, 0.22f);
            set (pid::outLevel, 3.0f);
            break;
        case 21: // Acoustic / Keys DI
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
