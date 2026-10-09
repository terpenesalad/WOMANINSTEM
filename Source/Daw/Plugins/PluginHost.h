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

    // ---- WOMANINSTEM's own plugin folder ----
    /** %APPDATA%\WOMANINSTEM\Plugins (created if needed): plugins kept with the app. */
    static juce::File userPluginFolder();
    /** The user folder plus a "Plugins" folder next to the exe (if there is one). */
    static juce::Array<juce::File> pluginFolders();
    /** Copies a plugin file (or .vst3 bundle) into the user folder and returns the copy; the original if it's
        already in a plugin folder or the copy fails. */
    static juce::File keepCopy (const juce::File& pluginFile);
    /** The format that loads this file ("VST", "VST3", "CLAP"), or null. */
    juce::AudioPluginFormat* formatForFile (const juce::File& f);
    /** Scans new or changed plugins in the plugin folders on a background thread (each in a child process);
        `onDone` runs on the message thread with the number of plugins added. */
    void scanPluginFoldersAsync (std::function<void (int added)> onDone);

private:
    class FolderScanThread;
    std::unique_ptr<FolderScanThread> folderScan;
    bool outOfProcessInstalled = false;
    struct ChangeSaver;
    std::unique_ptr<ChangeSaver> saver;
};

} // namespace wis::daw
