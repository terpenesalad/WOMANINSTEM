#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

/** Registers the editor factories for the built-in plugins (generic knob panels + custom editors). */
void installBuiltinEditors();

/** A floating window that shows any plugin's editor (built-in or VST3). */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& title, juce::AudioProcessor& processor, std::function<void()> onClose);
    ~PluginWindow() override;
    void closeButtonPressed() override;
    juce::AudioProcessor& processor;

private:
    std::function<void()> onCloseCallback;
};

} // namespace wis::daw
