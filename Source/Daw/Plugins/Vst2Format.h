#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace wis::daw
{

/** Hosts VST 2.x plugins (.dll on Windows, .so on Linux) through our own implementation of the VST2 binary
    interface. Shell plugins (several plugins in one file) are supported. */
class Vst2PluginFormat final : public juce::AudioPluginFormat
{
public:
    static juce::String formatName() { return "VST"; }

    juce::String getName() const override { return formatName(); }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool allowAsync = false) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

    /** Synchronous creation (used by the tests). */
    static std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

protected:
    void createPluginInstance (const juce::PluginDescription&, double sampleRate, int blockSize, PluginCreationCallback) override;
};

} // namespace wis::daw
