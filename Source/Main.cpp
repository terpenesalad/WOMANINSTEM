#include <juce_gui_extra/juce_gui_extra.h>
#include "UI/MainComponent.h"
#include "UI/LookAndFeel.h"

namespace wis
{

class WomanInStemApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "WOMANINSTEM"; }
    const juce::String getApplicationVersion() override { return WIS_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "WOMANINSTEM";
        opts.filenameSuffix = "settings";
        opts.folderName = "WOMANINSTEM";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        properties.setStorageParameters (opts);

        lookAndFeel = std::make_unique<WisLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow> (*properties.getUserSettings());

        // "Open with WOMANINSTEM" / drag a file onto the exe
        auto arg = commandLine.unquoted().trim();
        if (arg.isNotEmpty() && juce::File::isAbsolutePath (arg) && juce::File (arg).existsAsFile())
            mainWindow->content->openFile (juce::File (arg));
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->saveBounds();
        mainWindow = nullptr;
        properties.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

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
            content = new MainComponent (settings);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1120, 720, 10000, 10000);

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

        MainComponent* content = nullptr;

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
