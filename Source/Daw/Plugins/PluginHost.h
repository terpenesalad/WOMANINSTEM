#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BuiltinProcessor.h"

namespace wis::daw
{

/** Everything about third-party plugins: format manager (VST3), the list of scanned plugins,
    crash-safe out-of-process scanning, and creating processors for PLUGIN nodes (built-in or external). */
class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    juce::File listFile() const;
    void saveList();

    /** Creates (but doesn't prepare) the processor for a PLUGIN node, restoring its saved state. */
    std::unique_ptr<juce::AudioProcessor> create (const juce::ValueTree& pluginNode, double sampleRate, int blockSize,
                                                  bool asInstrument, juce::String& error);

    static PluginRef refFor (const juce::PluginDescription& d);

    juce::Array<juce::PluginDescription> externalInstruments() const;
    juce::Array<juce::PluginDescription> externalEffects() const;

    /** Installs the crash-safe scanner (each plugin file is opened in a child process). */
    void useOutOfProcessScanning();

    /** Entry point for the child process: `WOMANINSTEM --scan-plugin <format> <file> <out.xml>` */
    static int runScanChild (const juce::StringArray& args);

    static juce::FileSearchPath defaultSearchPath (juce::AudioPluginFormat&);

private:
    struct ChangeSaver;
    std::unique_ptr<ChangeSaver> saver;
};

} // namespace wis::daw
