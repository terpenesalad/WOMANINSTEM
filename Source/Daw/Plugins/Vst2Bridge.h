#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace wis::daw::vst2bridge
{

/** True for a 32-bit Windows DLL (which a 64-bit app can't load directly). */
bool isWin32Dll (const juce::File& f);

/** wisbridge32.exe next to the app (or WIS_BRIDGE32), empty if it isn't installed. */
juce::File bridgeExecutable();

/** Whether this build can bridge 32-bit plugins at all (64-bit Windows). */
bool isAvailable();

/** Opens the plugin in the bridge just long enough to describe it. */
bool describe (const juce::File& dll, juce::PluginDescription& out, juce::String& error);

/** A running bridged plugin. */
std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

} // namespace wis::daw::vst2bridge
