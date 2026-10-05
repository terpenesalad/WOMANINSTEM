#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include "UI/AppShell.h"
#include "UI/LookAndFeel.h"
#include "UI/Studio/PluginEditors.h"
#include "Daw/Plugins/PluginHost.h"
#include "Daw/Instruments/SoundFontInstrument.h"

namespace wis
{

class WomanInStemApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "WOMANINSTEM"; }
    const juce::String getApplicationVersion() override { return WIS_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        // the plugin scanner runs as a child process of the app
        const auto args = getCommandLineParameters();
        return args.contains ("--scan-plugin") || args.contains ("--selftest-ui");
    }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--scan-plugin"))
        {
            setApplicationReturnValue (daw::PluginHost::runScanChild (getCommandLineParameterArray()));
            quit();
            return;
        }

        juce::PropertiesFile::Options opts;
        opts.applicationName = "WOMANINSTEM";
        opts.filenameSuffix = "settings";
        opts.folderName = "WOMANINSTEM";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        properties.setStorageParameters (opts);

        lookAndFeel = std::make_unique<WisLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        daw::installBuiltinEditors();

        if (commandLine.contains ("--selftest-ui"))
        {
            setApplicationReturnValue (runUiSelfTest());
            quit();
            return;
        }
        daw::SoundFontCache::get().preloadDefault();   // the General MIDI sound library loads in the background

        mainWindow = std::make_unique<MainWindow> (*properties.getUserSettings());

        // "Open with WOMANINSTEM" / drag a file onto the exe
        auto arg = commandLine.unquoted().trim();
        if (arg.isNotEmpty() && juce::File::isAbsolutePath (arg) && juce::File (arg).existsAsFile())
            mainWindow->content->openFile (juce::File (arg));
    }

    /** Opens every built-in plugin's editor, lays it out at a few sizes and paints it (catches UI crashes). */
    static int runUiSelfTest()
    {
        int failures = 0;
        for (auto& info : daw::builtinPlugins())
        {
            auto p = daw::createBuiltin (info.id);
            if (p == nullptr) { ++failures; std::cout << "FAIL create " << info.id << std::endl; continue; }
            p->setPlayConfigDetails (info.instrument ? 0 : 2, 2, 48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            for (int i = 0; i < p->getProgramNames().size(); ++i) p->setCurrentProgram (i);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            if (ed == nullptr) { ++failures; std::cout << "FAIL editor " << info.id << std::endl; continue; }
            for (auto size : { juce::Point<int> (ed->getWidth(), ed->getHeight()), { 300, 200 }, { 1400, 900 } })
            {
                ed->setSize (size.x, size.y);
                juce::Image img (juce::Image::ARGB, juce::jmax (1, ed->getWidth()), juce::jmax (1, ed->getHeight()), true);
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
                // WIS_UI_SNAPSHOTS=<folder> saves a PNG of each editor at its default size (for eyeballing layouts)
                const auto snapDir = juce::SystemStats::getEnvironmentVariable ("WIS_UI_SNAPSHOTS", {});
                if (snapDir.isNotEmpty() && size == juce::Point<int> (img.getWidth(), img.getHeight()) && size.x != 300 && size.x != 1400)
                {
                    juce::FileOutputStream out (juce::File (snapDir).getChildFile (info.id + ".png"));
                    if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
                }
            }
            p->editorBeingDeleted (ed.get());
            ed.reset();
            p->releaseResources();
            std::cout << "  [ok]   " << info.name << " editor" << std::endl;
        }
        std::cout << (failures == 0 ? "UI SELF-TEST PASSED" : "UI SELF-TEST FAILED") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->saveBounds();
        mainWindow = nullptr;
        properties.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr) { quit(); return; }
        mainWindow->content->requestQuit ([] { juce::JUCEApplication::quit(); });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        auto arg = commandLine.unquoted().trim();
        if (mainWindow != nullptr && arg.isNotEmpty() && juce::File::isAbsolutePath (arg) && juce::File (arg).existsAsFile())
            mainWindow->content->openFile (juce::File (arg));
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::PropertiesFile& s)
            : DocumentWindow ("WOMANINSTEM", theme::bg, DocumentWindow::allButtons), settings (s)
        {
            setUsingNativeTitleBar (true);
            content = new AppShell (settings);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1180, 740, 10000, 10000);

            const auto saved = settings.getValue ("windowBounds");
            if (saved.isNotEmpty())
                restoreWindowStateFromString (saved);
            else
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
            content->grabKeyboardFocus();
        }

        void saveBounds()
        {
            settings.setValue ("windowBounds", getWindowStateAsString());
            content->saveState();
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        AppShell* content = nullptr;

    private:
        juce::PropertiesFile& settings;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<WisLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace wis

START_JUCE_APPLICATION (wis::WomanInStemApplication)
