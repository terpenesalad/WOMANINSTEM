#include "PluginHost.h"
#include "Separation/ModelManager.h"
#include "Vst2Format.h"
#include "ClapFormat.h"

namespace wis::daw
{

struct PluginHost::ChangeSaver : public juce::ChangeListener
{
    explicit ChangeSaver (PluginHost& h) : host (h) { host.known.addChangeListener (this); }
    ~ChangeSaver() override { host.known.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { host.saveList(); }
    PluginHost& host;
};

PluginHost::PluginHost()
{
   #if JUCE_PLUGINHOST_VST3
    formats.addFormat (new juce::VST3PluginFormat());
   #endif
    formats.addFormat (new ClapPluginFormat());
    formats.addFormat (new Vst2PluginFormat());
   #if JUCE_PLUGINHOST_LV2
    formats.addFormat (new juce::LV2PluginFormat());
   #endif

    if (auto xml = juce::XmlDocument::parse (listFile()))
        known.recreateFromXml (*xml);

    saver = std::make_unique<ChangeSaver> (*this);
}

PluginHost::~PluginHost()
{
    folderScan.reset();
    saver.reset();
}

// ---- the plugins folder ------------------------------------------------------------------------------------

juce::File PluginHost::userPluginFolder()
{
    auto dir = ModelManager::appDataDirectory().getChildFile ("Plugins");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PluginHost::pluginFolders()
{
    juce::Array<juce::File> dirs { userPluginFolder() };
    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (auto name : { "Plugins", "plugins" })
        if (auto d = exeDir.getChildFile (name); d.isDirectory() && ! dirs.contains (d)) { dirs.add (d); break; }
    return dirs;
}

juce::File PluginHost::keepCopy (const juce::File& f)
{
    for (auto& dir : pluginFolders())
        if (f.isAChildOf (dir)) return f;
    const auto dest = userPluginFolder().getChildFile (f.getFileName());
    if (dest == f) return f;
    if (f.isDirectory())   // a .vst3 bundle
    {
        dest.deleteRecursively();
        return f.copyDirectoryTo (dest) ? dest : f;
    }
    if (dest.existsAsFile() && dest.getSize() == f.getSize() && dest.hasIdenticalContentTo (f)) return dest;
    return f.copyFileTo (dest) ? dest : f;
}

juce::AudioPluginFormat* PluginHost::formatForFile (const juce::File& f)
{
    const juce::String name = f.hasFileExtension (".vst3") ? "VST3" : f.hasFileExtension (".clap") ? "CLAP"
                            : (f.hasFileExtension (".dll") || f.hasFileExtension (".so")) ? "VST" : juce::String();
    for (auto* format : formats.getFormats())
        if (format->getName() == name) return format;
    return nullptr;
}

class PluginHost::FolderScanThread : public juce::Thread
{
public:
    FolderScanThread (PluginHost& h, std::function<void (int)> done) : juce::Thread ("Plugin folder scan"), host (h), onDone (std::move (done)) {}
    ~FolderScanThread() override { stopThread (15000); }

    void run() override
    {
        juce::Array<juce::File> files;
        for (auto& dir : pluginFolders())
        {
            for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.dll;*.clap;*.so"))
                if (! f.getFullPathName().containsIgnoreCase (".vst3")) files.add (f);   // not a binary inside a VST3 bundle
            for (auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, true, "*.vst3"))
                if (! f.getParentDirectory().getFullPathName().containsIgnoreCase (".vst3")) files.add (f);
        }
        int added = 0;
        for (auto& f : files)
        {
            if (threadShouldExit()) return;
            auto* format = host.formatForFile (f);
            if (format == nullptr) continue;
            const auto id = f.getFullPathName();
            if (host.known.getBlacklistedFiles().contains (id) || host.known.isListingUpToDate (id, *format)) continue;
            juce::OwnedArray<juce::PluginDescription> found;
            host.known.scanAndAddFile (id, false, found, *format);
            added += found.size();
        }
        if (! threadShouldExit())
            juce::MessageManager::callAsync ([cb = onDone, added] { if (cb) cb (added); });
    }

private:
    PluginHost& host;
    std::function<void (int)> onDone;
};

void PluginHost::scanPluginFoldersAsync (std::function<void (int)> onDone)
{
    useOutOfProcessScanning();
    folderScan.reset();
    folderScan = std::make_unique<FolderScanThread> (*this, std::move (onDone));
    folderScan->startThread (juce::Thread::Priority::low);
}

juce::File PluginHost::listFile() const
{
    return ModelManager::appDataDirectory().getChildFile ("plugins.xml");
}

void PluginHost::saveList()
{
    if (auto xml = known.createXml())
        xml->writeTo (listFile());
}

PluginRef PluginHost::refFor (const juce::PluginDescription& d)
{
    PluginRef r;
    r.type = "external";
    r.uid = d.createIdentifierString();
    r.name = d.name;
    if (auto xml = d.createXml())
        r.descXml = xml->toString (juce::XmlElement::TextFormat().singleLine());
    return r;
}

juce::Array<juce::PluginDescription> PluginHost::externalInstruments() const
{
    juce::Array<juce::PluginDescription> r;
    for (auto& d : known.getTypes()) if (d.isInstrument) r.add (d);
    return r;
}

juce::Array<juce::PluginDescription> PluginHost::externalEffects() const
{
    juce::Array<juce::PluginDescription> r;
    for (auto& d : known.getTypes()) if (! d.isInstrument) r.add (d);
    return r;
}

std::unique_ptr<juce::AudioProcessor> PluginHost::create (const juce::ValueTree& node, double sr, int block,
                                                         bool asInstrument, juce::String& error)
{
    const auto type = node["type"].toString();
    const auto uid = node["uid"].toString();
    std::unique_ptr<juce::AudioProcessor> proc;

    if (type == "builtin")
    {
        auto b = createBuiltin (uid);
        if (b == nullptr) { error = "Unknown built-in plugin '" + uid + "'"; return {}; }
        proc = std::move (b);
    }
    else
    {
        juce::PluginDescription desc;
        bool found = false;
        if (auto known1 = known.getTypeForIdentifierString (uid)) { desc = *known1; found = true; }
        else if (auto xml = juce::XmlDocument::parse (node["desc"].toString()))
            found = desc.loadFromXml (*xml);

        if (! found) { error = "Plugin not found: " + node["name"].toString(); return {}; }

        juce::String err;
        auto inst = formats.createPluginInstance (desc, sr, block, err);
        if (inst == nullptr)
        {
            error = "Couldn't load " + desc.name + (err.isNotEmpty() ? ": " + err : juce::String());
            return {};
        }

        // Ask for a plain stereo layout; fall back to whatever the plugin offers.
        auto layout = inst->getBusesLayout();
        if (layout.outputBuses.size() > 0) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! asInstrument && layout.inputBuses.size() > 0) layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! inst->setBusesLayout (layout))
            inst->enableAllBuses();

        proc = std::move (inst);
    }

    decodeState (*proc, node["state"].toString());
    return proc;
}

juce::FileSearchPath PluginHost::defaultSearchPath (juce::AudioPluginFormat& f)
{
    return f.getDefaultLocationsToSearch();
}

// ---- crash-safe scanning ---------------------------------------------------------------------------------

namespace
{
    class OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            auto out = juce::File::createTempFile (".xml");
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

            juce::ChildProcess child;
            juce::StringArray args { exe.getFullPathName(), "--scan-plugin", format.getName(), fileOrIdentifier, out.getFullPathName() };
            if (! child.start (args, 0))
                return false;

            // Up to 60 s per plugin (some installers' plugins are slow to initialise).
            for (int waited = 0; child.isRunning() && waited < 60000; waited += 100)
            {
                if (shouldExit()) { child.kill(); return true; }
                juce::Thread::sleep (100);
            }
            if (child.isRunning()) { child.kill(); out.deleteFile(); return false; }

            const bool ok = child.getExitCode() == 0;
            if (auto xml = juce::XmlDocument::parse (out))
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e)) result.add (d.release());
                }
            out.deleteFile();
            return ok;
        }
    };
}

void PluginHost::useOutOfProcessScanning()
{
    // installed once: replacing it could pull the scanner out from under a scan running on another thread
    if (outOfProcessInstalled) return;
    outOfProcessInstalled = true;
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
}

int PluginHost::runScanChild (const juce::StringArray& args)
{
    // args: --scan-plugin <format> <file> <out.xml>
    const int i = args.indexOf ("--scan-plugin");
    if (i < 0 || i + 3 >= args.size()) return 2;

    PluginHost host;
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : host.formats.getFormats())
        if (f->getName() == args[i + 1])
            f->findAllTypesForFile (found, args[i + 2]);

    juce::XmlElement root ("PLUGINS");
    for (auto* d : found)
        root.addChildElement (d->createXml().release());
    root.writeTo (juce::File (args[i + 3]));
    return 0;
}

} // namespace wis::daw
