#include "BuiltinProcessor.h"

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (BuiltinProcessor&)> BuiltinProcessor::genericEditorFactory;
std::function<juce::String (const juce::File&)> BuiltinProcessor::fileToRef;
std::function<juce::File (const juce::String&)> BuiltinProcessor::refToFile;
std::function<void (BuiltinProcessor&, const juce::File&)> BuiltinProcessor::onAudioToTrack;
std::atomic<int> BuiltinProcessor::songKey { 0 }, BuiltinProcessor::songScale { 0 };

juce::AudioProcessor::BusesProperties BuiltinProcessor::busesFor (bool instrument, bool midiOnly)
{
    auto b = BusesProperties();
    if (midiOnly) return b;
    if (! instrument)
        b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    return b.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

BuiltinProcessor::BuiltinProcessor (const juce::String& id, const juce::String& name, bool isInstrument,
                                    juce::AudioProcessorValueTreeState::ParameterLayout layout, bool midiOnly)
    : AudioProcessor (busesFor (isInstrument, midiOnly)),
      builtinId (id), displayName (name), instrument (isInstrument),
      state (*this, nullptr, juce::Identifier ("STATE"), std::move (layout))
{
}

bool BuiltinProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (isMidiFx()) return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (! instrument && layouts.getMainInputChannelSet() != out)
        return false;
    return true;
}

void BuiltinProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getProgramNames().size()))
    {
        currentProgram = index;
        loadProgram (index);
    }
}

juce::AudioProcessorEditor* BuiltinProcessor::createEditor()
{
    if (auto* custom = createCustomEditor())
        return custom;
    return genericEditorFactory ? genericEditorFactory (*this) : new juce::GenericAudioProcessorEditor (*this);
}

void BuiltinProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    tree.setProperty ("program", currentProgram, nullptr);
    auto extra = juce::ValueTree ("EXTRA");
    saveExtraState (extra);
    tree.removeChild (tree.getChildWithName ("EXTRA"), nullptr);
    tree.appendChild (extra, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, dest);
}

void BuiltinProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (state.state.getType()))
        {
            currentProgram = (int) tree.getProperty ("program", 0);
            auto extra = tree.getChildWithName ("EXTRA");
            loadExtraState (extra);                      // before params: may (re)load files
            tree.removeChild (extra, nullptr);
            state.replaceState (tree);
        }
    }
}

void BuiltinProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

double BuiltinProcessor::hostTempo() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                return *bpm;
    return 120.0;
}

// ---- helpers ------------------------------------------------------------------------------------------

juce::String encodeState (juce::AudioProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb.toBase64Encoding();
}

void decodeState (juce::AudioProcessor& p, const juce::String& base64)
{
    if (base64.isEmpty()) return;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (base64) && mb.getSize() > 0)
        p.setStateInformation (mb.getData(), (int) mb.getSize());
}

const BuiltinInfo* findBuiltin (const juce::String& id)
{
    for (auto& b : builtinPlugins())
        if (b.id == id)
            return &b;
    return nullptr;
}

std::unique_ptr<BuiltinProcessor> createBuiltin (const juce::String& id)
{
    if (auto* info = findBuiltin (id))
        return info->create();
    return {};
}

PluginRef builtinRef (const juce::String& id)
{
    PluginRef r;
    r.type = "builtin";
    r.uid = id;
    if (auto* info = findBuiltin (id))
        r.name = info->name;
    return r;
}

namespace prm
{
    static juce::AudioParameterFloatAttributes attrs (const juce::String& unit, int decimals)
    {
        return juce::AudioParameterFloatAttributes().withLabel (unit)
            .withStringFromValueFunction ([decimals, unit] (float v, int)
            {
                if (unit == "Hz")
                    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz";
                if (unit == "%")
                    return juce::String ((int) std::round (v * 100.0f)) + "%";
                return juce::String (v, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
            });
    }

    void addFloat (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def,
                   const juce::String& unit, float skewCentre, int decimals)
    {
        juce::NormalisableRange<float> r (min, max, 0.0f);
        if (skewCentre > min && skewCentre < max) r.setSkewForCentre (skewCentre);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs (unit, decimals)));
    }

    void addHz (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def)
    {
        addFloat (l, id, name, min, max, def, "Hz", std::sqrt (min * max), 0);
    }

    void addPercent (Layout& l, const juce::String& id, const juce::String& name, float def)
    {
        addFloat (l, id, name, 0.0f, 1.0f, def, "%");
    }

    void addDb (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def)
    {
        addFloat (l, id, name, min, max, def, "dB", 0.0f, 1);
    }

    void addMs (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def)
    {
        addFloat (l, id, name, min, max, def, "ms", std::sqrt (juce::jmax (0.1f, min) * max), max >= 100.0f ? 0 : 1);
    }

    void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& choices, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
    }

    void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }
}

} // namespace wis::daw
