#pragma once

#include "Daw/Model/Project.h"

namespace wis::daw
{

/** Instrument slot reference for the built-in Sound Library (SoundFont) on the given GM bank/program. */
PluginRef soundFontRef (int bank, int program, const juce::String& sf2Path = {});
/** Studio Synth with one of its factory presets. */
PluginRef synthRef (int presetIndex);

juce::String gmProgramName (int program);          // 0-127
juce::String gmFamilyName (int program);           // "Piano", "Bass", ...
juce::StringArray gmFamilies();
juce::String gmDrumName (int note);                // GM percussion key map (35-81), empty otherwise

} // namespace wis::daw
