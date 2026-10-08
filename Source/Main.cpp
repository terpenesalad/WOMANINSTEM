#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include "UI/AppShell.h"
#include "UI/Scope/ScopeWindow.h"
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
            // WIS_UI_PROGRAM=<plugin id>:<preset> shows that preset in the snapshot
            const auto want = juce::SystemStats::getEnvironmentVariable ("WIS_UI_PROGRAM", {});
            if (want.upToFirstOccurrenceOf (":", false, false) == info.id) p->setCurrentProgram (want.fromFirstOccurrenceOf (":", false, false).getIntValue());
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
        failures += runScopeSelfTest();
        failures += runPedalboardSelfTest();
        std::cout << (failures == 0 ? "UI SELF-TEST PASSED" : "UI SELF-TEST FAILED") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    /** The rig panel with an edited pedalboard lays out and paints (WIS_UI_SNAPSHOTS saves rig-pedalboard.png). */
    static int runPedalboardSelfTest()
    {
        RigProcessor rig;
        rig.loadFactoryPreset (8);   // dream pop: chorus, delay, tape, reverb on
        rig.prepareToPlay (48000.0, 512);
        const auto wah = rig.addPedal ("wah", 0);
        rig.addPedal ("octaver", 1);
        rig.addPedal ("stompdrive");
        const auto phaser = rig.addPedal ("phaser");
        rig.moveBoardItem ((int) rig.getBoard().size() - 1, 8);   // reverb into the middle of the effects loop
        rig.setBoardItemOn (wah, false);
        int failures = rig.getBoard().size() == 13 ? 0 : 1;
        {
            RigPanel panel (rig, nullptr);
            for (auto size : { juce::Point<int> (1440, 378), { 1180, 378 }, { 1920, 420 } })
            {
                panel.setSize (size.x, size.y);
                juce::Image img (juce::Image::ARGB, size.x, size.y, true);
                juce::Graphics g (img);
                panel.paintEntireComponent (g, true);
                const auto snapDir = juce::SystemStats::getEnvironmentVariable ("WIS_UI_SNAPSHOTS", {});
                if (snapDir.isNotEmpty() && size.x == 1440)
                {
                    juce::FileOutputStream out (juce::File (snapDir).getChildFile ("rig-pedalboard.png"));
                    if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
                }
            }
            // a pedal's own editor opens and paints
            if (auto* p = rig.getPedalProcessor (phaser))
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                if (ed == nullptr) ++failures;
                else
                {
                    juce::Image img (juce::Image::ARGB, juce::jmax (1, ed->getWidth()), juce::jmax (1, ed->getHeight()), true);
                    juce::Graphics g (img);
                    ed->paintEntireComponent (g, true);
                    p->editorBeingDeleted (ed.get());
                }
            }
            rig.removePedal (phaser);
        }
        std::cout << (failures == 0 ? "  [ok]   " : "  [FAIL] ") << "Pedalboard: 13 blocks, panel lays out and paints, pedal editor opens" << std::endl;

        {
            // Play Along's KEYS panel with its default instrument
            StemPlayer player;
            Recorder recorder;
            AudioEngine engine (rig, player, recorder);
            auto propsFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wis_keys_selftest.settings");
            propsFile.deleteFile();
            {
                juce::PropertiesFile props (propsFile, juce::PropertiesFile::Options());
                KeysPanel keysPanel (engine, props);
                keysPanel.setSize (1440, 378);
                keysPanel.setVisible (true);
                keysPanel.ensureInstrument();
                const bool ok = engine.getKeysInstrument() != nullptr && engine.getKeysInstrument()->getName() == "Piano Room";
                if (! ok) ++failures;
                juce::Image img (juce::Image::ARGB, 1440, 378, true);
                juce::Graphics g (img);
                keysPanel.paintEntireComponent (g, true);
                const auto snapDir = juce::SystemStats::getEnvironmentVariable ("WIS_UI_SNAPSHOTS", {});
                if (snapDir.isNotEmpty())
                {
                    juce::FileOutputStream out (juce::File (snapDir).getChildFile ("keys-panel.png"));
                    if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
                }
                std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << "Play Along KEYS panel loads Piano Room and paints" << std::endl;
                keysPanel.saveSettings();
                engine.setKeysInstrument (nullptr);
            }
            propsFile.deleteFile();
        }
        return failures;
    }

    /** The oscilloscope: every shape and colour draws a sane picture from a test signal, and the window lays out
        and paints at small and large sizes. WIS_UI_SNAPSHOTS also saves scope-*.png. */
    static int runScopeSelfTest()
    {
        int failures = 0;
        const auto snapDir = juce::SystemStats::getEnvironmentVariable ("WIS_UI_SNAPSHOTS", {});
        auto save = [&] (const juce::Image& img, const juce::String& name)
        {
            if (snapDir.isEmpty()) return;
            juce::FileOutputStream out (juce::File (snapDir).getChildFile (name + ".png"));
            if (out.openedOk()) { out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
        };

        auto propsFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wis_scope_selftest.settings");
        propsFile.deleteFile();
        juce::PropertiesFile props (propsFile, juce::PropertiesFile::Options());
        ScopeFeed feed;
        ScopeView view (feed, props);

        for (int shape = 0; shape < ScopeSettings::numShapes; ++shape)
            for (int pal = 0; pal < ScopeSettings::numPalettes; ++pal)
            {
                ScopeSettings s;
                s.shape = shape;
                s.palette = pal;
                s.mirror = pal == ScopeSettings::rainbow;
                s.spin = pal == ScopeSettings::rainbow ? 0.3f : 0.0f;
                auto img = view.renderTestFrame (s, 480, 300, 2.0);
                // how much of the picture is lit, and is anything blown out to a white sheet?
                int lit = 0, white = 0;
                for (int y = 0; y < img.getHeight(); y += 2)
                    for (int x = 0; x < img.getWidth(); x += 2)
                    {
                        const auto c = img.getPixelAt (x, y);
                        const int m = juce::jmax (c.getRed(), c.getGreen(), c.getBlue());
                        if (m > 80) ++lit;
                        if (c.getRed() > 250 && c.getGreen() > 250 && c.getBlue() > 250) ++white;
                    }
                const int total = (img.getWidth() / 2) * (img.getHeight() / 2);
                const bool ok = img.isValid() && lit > total / 200 && white < total / 8;
                if (! ok) ++failures;
                const auto name = ScopeSettings::shapeNames()[shape] + " / " + ScopeSettings::paletteNames()[pal];
                std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << "Scope " << name << ": " << juce::String (100.0 * lit / total, 1)
                          << "% lit, " << juce::String (100.0 * white / total, 1) << "% white" << std::endl;
                if (pal == 0 || shape == 0)
                    save (img, "scope-" + ScopeSettings::shapeNames()[shape].toLowerCase() + "-" + juce::String (pal));
            }

        for (int mode : { 0, 1 })
        {
            view.setAppMode (mode);
            for (auto size : { juce::Point<int> (960, 640), { 520, 360 }, { 1920, 1080 } })
            {
                view.setSize (size.x, size.y);
                view.showTestPicture();
                juce::Image img (juce::Image::ARGB, size.x, size.y, true);
                juce::Graphics g (img);
                view.paintEntireComponent (g, true);
                if (size.x == 960) save (img, "scope-window-" + juce::String (mode));
            }
        }
        std::cout << "  [ok]   Scope window lays out and paints (Play Along + Studio)" << std::endl;

        {
            // speed: one full frame at the largest internal size (the window draws at 60 fps)
            ScopeRenderer r;
            r.setSize (960, 600);
            ScopeSettings s;
            s.mirror = true;
            std::vector<float> a (800), b (800);
            for (size_t i = 0; i < a.size(); ++i) { a[i] = 0.5f * std::sin ((float) i * 0.0072f); b[i] = 0.5f * std::sin ((float) i * 0.0151f); }
            juce::Image img;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int f = 0; f < 30; ++f)
            {
                r.beginFrame (1.0 / 60.0, s);
                r.addSamples (a.data(), b.data(), (int) a.size(), 48000.0, s);
                r.renderTo (img, s);
            }
            std::cout << "  [info] Scope frame at 960x600 (mirror on): " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 30.0, 2) << " ms" << std::endl;
        }
        propsFile.deleteFile();
        return failures;
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
