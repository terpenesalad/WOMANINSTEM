#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Daw/Model/Project.h"

namespace wis::daw
{

/** Base for every built-in instrument and effect. Parameters live in an APVTS so the generic knob
    editor, presets and project save/load all work the same way as for VST3 plugins. */
class BuiltinProcessor : public juce::AudioProcessor
{
public:
    BuiltinProcessor (const juce::String& builtinId, const juce::String& displayName, bool isInstrument,
                      juce::AudioProcessorValueTreeState::ParameterLayout layout);

    const juce::String builtinId;
    const juce::String displayName;
    const bool instrument;
    juce::AudioProcessorValueTreeState state;

    // AudioProcessor
    const juce::String getName() const override        { return displayName; }
    bool acceptsMidi() const override                   { return instrument; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 2.0; }
    int getNumPrograms() override                       { return juce::jmax (1, getProgramNames().size()); }
    int getCurrentProgram() override                    { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return getProgramNames()[index]; }
    void changeProgramName (int, const juce::String&) override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void releaseResources() override {}
    bool hasEditor() const override                     { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Factory presets (optional). */
    virtual juce::StringArray getProgramNames() { return {}; }
    virtual void loadProgram (int) {}

    /** Extra non-parameter state (e.g. the SoundFont file). */
    virtual void saveExtraState (juce::ValueTree&) {}
    virtual void loadExtraState (const juce::ValueTree&) {}

    /** Optional custom editor (otherwise a knob panel is generated from the parameters). */
    virtual juce::AudioProcessorEditor* createCustomEditor() { return nullptr; }

    /** Set by the UI layer: builds the generic knob editor. */
    static std::function<juce::AudioProcessorEditor* (BuiltinProcessor&)> genericEditorFactory;

    float param (const char* id) const { return state.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float value);

    /** Set by the Studio: called when the plugin's display name changes (e.g. a new preset). */
    std::function<void (const juce::String&)> onDisplayNameChanged;

    /** Tempo from the host's playhead (falls back to 120). */
    double hostTempo() const;

protected:
    int currentProgram = 0;

private:
    static BusesProperties busesFor (bool instrument);
};

/** Description of a built-in plugin for the browser. */
struct BuiltinInfo
{
    juce::String id, name, category, description;
    bool instrument = false;
    std::function<std::unique_ptr<BuiltinProcessor>()> create;
};

const juce::Array<BuiltinInfo>& builtinPlugins();
const BuiltinInfo* findBuiltin (const juce::String& id);
std::unique_ptr<BuiltinProcessor> createBuiltin (const juce::String& id);
PluginRef builtinRef (const juce::String& id);
/** Encodes a processor's current state for a PLUGIN node. */
juce::String encodeState (juce::AudioProcessor& p);
void decodeState (juce::AudioProcessor& p, const juce::String& base64);

// Parameter helpers ---------------------------------------------------------------------------------------
namespace prm
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    void addFloat (Layout&, const juce::String& id, const juce::String& name, float min, float max, float def,
                   const juce::String& unit = {}, float skewCentre = 0.0f, int decimals = 1);
    void addHz (Layout&, const juce::String& id, const juce::String& name, float min, float max, float def);
    void addPercent (Layout&, const juce::String& id, const juce::String& name, float def);
    void addDb (Layout&, const juce::String& id, const juce::String& name, float min, float max, float def);
    void addMs (Layout&, const juce::String& id, const juce::String& name, float min, float max, float def);
    void addChoice (Layout&, const juce::String& id, const juce::String& name, const juce::StringArray& choices, int def);
    void addBool (Layout&, const juce::String& id, const juce::String& name, bool def);
}

} // namespace wis::daw
