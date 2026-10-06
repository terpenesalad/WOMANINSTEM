#include "PluginEditors.h"
#include "EditorParts.h"
#include "Daw/Plugins/CreativeEffects.h"
#include "Daw/Plugins/BuiltinEffects.h"
#include "Daw/Instruments/SoundFontInstrument.h"
#include "Daw/Instruments/InstrumentRefs.h"
#include "UI/RigPanel.h"
#include "UI/LookAndFeel.h"

namespace wis::daw
{

// =====================================================================================================
//  EQ curve
// =====================================================================================================
class EqCurve : public juce::Component, private juce::Timer
{
public:
    explicit EqCurve (ChannelEq& e) : eq (e) { startTimerHz (15); }
    void timerCallback() override { repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 6.0f);

        auto xFor = [&] (double hz) { return r.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * r.getWidth(); };
        auto yFor = [&] (float db) { return r.getCentreY() - db / 24.0f * r.getHeight() * 0.5f; };

        g.setColour (theme::outline);
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            g.drawVerticalLine ((int) xFor (hz), r.getY(), r.getBottom());
        for (float db : { -12.0f, 0.0f, 12.0f })
            g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());
        g.setFont (uiFont (10.0f));
        g.setColour (theme::textFaint);
        for (auto [hz, label] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText (label, (int) xFor (hz) + 3, (int) r.getBottom() - 14, 30, 12, juce::Justification::left);

        eq.refreshDisplay();
        juce::Path p;
        for (int x = 0; x <= (int) r.getWidth(); x += 2)
        {
            const double hz = 20.0 * std::pow (1000.0, x / r.getWidth());
            const float y = juce::jlimit (r.getY(), r.getBottom(), yFor (eq.responseDb (hz)));
            if (x == 0) p.startNewSubPath (r.getX() + x, y); else p.lineTo (r.getX() + x, y);
        }
        juce::Path fill (p);
        fill.lineTo (r.getRight(), r.getCentreY());
        fill.lineTo (r.getX(), r.getCentreY());
        fill.closeSubPath();
        g.setColour (juce::Colour (0xffa3e635).withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (juce::Colour (0xffa3e635));
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

private:
    ChannelEq& eq;
};

class GainReductionMeter : public juce::Component, private juce::Timer
{
public:
    explicit GainReductionMeter (Compressor& c) : comp (c) { startTimerHz (30); }
    void timerCallback() override
    {
        const float gr = -comp.gainReductionDb.load();
        shown = gr > shown ? gr : shown * 0.85f + gr * 0.15f;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 4.0f);
        const float w = juce::jlimit (0.0f, 1.0f, shown / 24.0f) * r.getWidth();
        g.setColour (theme::warn);
        g.fillRoundedRectangle (r.withX (r.getRight() - w).withWidth (w), 4.0f);
        g.setColour (theme::text);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("Gain reduction  " + juce::String (shown, 1) + " dB", r.reduced (8, 0), juce::Justification::centredLeft);
    }
private:
    Compressor& comp;
    float shown = 0.0f;
};

// =====================================================================================================
//  Parameter panel + header (shared by the generic and the custom editors)
// =====================================================================================================
ParamPanel::ParamPanel (BuiltinProcessor& p, const juce::StringArray& onlyIds, juce::Colour accent, const juce::StringArray& excludeIds)
{
    auto add = [&] (juce::RangedAudioParameter* ranged)
    {
        const auto id = ranged->paramID;
        const auto name = ranged->getName (24);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged))
        {
            auto* cb = combos.add (new juce::ComboBox());
            cb->addItemList (choice->choices, 1);
            comboAttachments.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (p.state, id, *cb));
            auto* l = labels.add (new juce::Label ({}, name));
            l->setFont (uiFont (11.0f, true));
            l->setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (cb);
            addAndMakeVisible (l);
            switches.add (cb);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (ranged) != nullptr)
        {
            auto* tb = toggles.add (new juce::ToggleButton (name));
            buttonAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (p.state, id, *tb));
            addAndMakeVisible (tb);
            switches.add (tb);
        }
        else
        {
            auto* k = knobs.add (new wis::Knob (p.state, id, name));
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            addAndMakeVisible (k);
        }
    };

    if (onlyIds.isEmpty())
    {
        for (auto* param : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                if (! excludeIds.contains (ranged->paramID)) add (ranged);
    }
    else
    {
        for (auto& id : onlyIds)
            if (auto* ranged = p.state.getParameter (id)) add (ranged);
    }
}

int ParamPanel::layout (int width, bool apply)
{
    int x = 0, y = 0, rowH = 0;
    for (auto* c : switches)
    {
        const bool isCombo = dynamic_cast<juce::ComboBox*> (c) != nullptr;
        const int w = isCombo ? comboW : toggleW;
        if (x > 0 && x + w > width) { x = 0; y += rowH + 4; rowH = 0; }
        if (apply)
        {
            if (isCombo)
            {
                const int idx = combos.indexOf (static_cast<juce::ComboBox*> (c));
                labels[idx]->setBounds (x, y, w - 10, 16);
                c->setBounds (x, y + 16, w - 10, 24);
            }
            else c->setBounds (x, y + 16, w - 6, 24);
        }
        x += w;
        rowH = switchH;
    }
    if (rowH > 0) y += rowH + 6;
    const int cols = juce::jmax (1, width / knobW);
    for (int i = 0; i < knobs.size(); ++i)
        if (apply) knobs[i]->setBounds ((i % cols) * knobW, y + (i / cols) * knobH, knobW - 4, knobH - 4);
    if (! knobs.isEmpty()) y += ((knobs.size() + cols - 1) / cols) * knobH;
    return y;
}

int ParamPanel::heightFor (int width) const { return const_cast<ParamPanel*> (this)->layout (width, false); }
void ParamPanel::resized() { layout (getWidth(), true); }

EditorHeader::EditorHeader (BuiltinProcessor& p, const juce::String& titleText) : proc (p)
{
    title.setText (titleText.isNotEmpty() ? titleText : p.displayName, juce::dontSendNotification);
    title.setFont (uiFont (16.0f, true));
    addAndMakeVisible (title);
    const auto programs = p.getProgramNames();
    if (! programs.isEmpty())
    {
        presets.addItemList (programs, 1);
        presets.setSelectedItemIndex (p.getCurrentProgram(), juce::dontSendNotification);
        presets.setTextWhenNothingSelected ("Presets");
        presets.onChange = [this]
        {
            proc.setCurrentProgram (presets.getSelectedItemIndex());
            if (proc.instrument && proc.onDisplayNameChanged) proc.onDisplayNameChanged (presets.getText());
            if (onPresetChanged) onPresetChanged();
        };
        addAndMakeVisible (presets);
    }
}

void EditorHeader::resized()
{
    auto r = getLocalBounds();
    if (presets.isVisible()) presets.setBounds (r.removeFromRight (230).reduced (0, 4));
    for (int i = extras.size(); --i >= 0;)
    {
        r.removeFromRight (6);
        extras[i].first->setBounds (r.removeFromRight (extras[i].second).reduced (0, 4));
    }
    title.setBounds (r);
}

/** De-esser reduction meter. */
class DeEssMeter : public juce::Component, private juce::Timer
{
public:
    explicit DeEssMeter (DeEsser& d) : fx (d) { startTimerHz (30); }
    void timerCallback() override { const float v = fx.reductionDb.load(); shown = v > shown ? v : shown * 0.85f + v * 0.15f; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::bg); g.fillRoundedRectangle (r, 4.0f);
        const float w = juce::jlimit (0.0f, 1.0f, shown / 24.0f) * r.getWidth();
        g.setColour (theme::warn); g.fillRoundedRectangle (r.withX (r.getRight() - w).withWidth (w), 4.0f);
        g.setColour (theme::text); g.setFont (uiFont (11.0f, true));
        g.drawText ("Ess reduction  " + juce::String (shown, 1) + " dB", r.reduced (8, 0), juce::Justification::centredLeft);
    }
private:
    DeEsser& fx; float shown = 0.0f;
};

// =====================================================================================================
//  Generic editor: presets + a control for every parameter
// =====================================================================================================
class BuiltinEditor : public juce::AudioProcessorEditor
{
public:
    explicit BuiltinEditor (BuiltinProcessor& p)
        : AudioProcessorEditor (p), proc (p), header (p), params (p, {}, p.instrument ? theme::accent2 : p.isMidiFx() ? theme::warn : theme::accent)
    {
        addAndMakeVisible (header);
        if (auto* eq = dynamic_cast<ChannelEq*> (&p))     visual = std::make_unique<EqCurve> (*eq);
        if (auto* c = dynamic_cast<Compressor*> (&p))     visual = std::make_unique<GainReductionMeter> (*c);
        if (auto* d = dynamic_cast<DeEsser*> (&p))        { visual = std::make_unique<DeEssMeter> (*d); visualH = 28; }
        if (visual != nullptr) addAndMakeVisible (*visual);
        addAndMakeVisible (params);

        int width = 460;
        while (params.heightFor (width - 24) > 330 && width < 900) width += ParamPanel::knobW;
        setSize (width, 52 + (visual != nullptr ? visualH + 10 : 0) + params.heightFor (width - 24) + 12);
    }

    void paint (juce::Graphics& g) override
    {
        paintEditorBackground (g, getLocalBounds(), proc.instrument ? theme::accent2 : proc.isMidiFx() ? theme::warn : theme::accent);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        if (visual != nullptr) { visual->setBounds (r.removeFromTop (visualH)); r.removeFromTop (10); }
        params.setBounds (r);
    }

private:
    BuiltinProcessor& proc;
    EditorHeader header;
    ParamPanel params;
    std::unique_ptr<juce::Component> visual;
    int visualH = 120;
};

// =====================================================================================================
//  Sound Library editor: category + preset browser
// =====================================================================================================
class SoundFontEditor : public juce::AudioProcessorEditor, private juce::ListBoxModel
{
public:
    explicit SoundFontEditor (SoundFontInstrument& p) : AudioProcessorEditor (p), sf (p)
    {
        title.setFont (uiFont (16.0f, true));
        addAndMakeVisible (title);
        fileLabel.setFont (uiFont (11.5f));
        fileLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (fileLabel);

        loadButton.setTooltip ("Load any SoundFont (.sf2) - thousands of free instruments online");
        loadButton.onClick = [this] { chooseFile(); };
        addAndMakeVisible (loadButton);
        defaultButton.onClick = [this] { sf.loadSoundFont ({}); rebuild(); };
        addAndMakeVisible (defaultButton);

        categoryList.setModel (this);
        categoryList.setRowHeight (26);
        presetList.setRowHeight (24);
        presetModel.owner = this;
        presetList.setModel (&presetModel);
        addAndMakeVisible (categoryList);
        addAndMakeVisible (presetList);

        for (auto [id, name] : { std::pair<const char*, const char*> { "gain", "Volume" }, { "transpose", "Transpose" }, { "bendrange", "Bend Range" } })
        {
            auto* k = knobs.add (new wis::Knob (p.state, id, name));
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, theme::accent2);
            addAndMakeVisible (k);
        }

        rebuild();
        setSize (640, 520);
        setResizable (true, false);
    }

    void rebuild()
    {
        categories.clear();
        byCategory.clear();
        const auto families = gmFamilies();
        for (auto& p : sf.getPresets())
        {
            juce::String cat = p.bank == 128 || p.bank >= 120 ? juce::String ("Drum Kits")
                             : families[juce::jlimit (0, 127, p.program) / 8];
            if (! categories.contains (cat)) categories.add (cat);
            byCategory[cat].add (p);
        }
        // keep GM order, drums last
        juce::StringArray ordered;
        for (auto& f : families) if (categories.contains (f)) ordered.add (f);
        if (categories.contains ("Drum Kits")) ordered.add ("Drum Kits");
        categories = ordered;

        categoryList.updateContent();
        // select the category of the current preset
        for (int i = 0; i < categories.size(); ++i)
            for (auto& p : byCategory[categories[i]])
                if (p.bank == sf.getBank() && p.program == sf.getProgram())
                    categoryList.selectRow (i);
        if (categoryList.getSelectedRow() < 0 && ! categories.isEmpty()) categoryList.selectRow (0);

        title.setText ("Sound Library  -  " + sf.getPresetName(), juce::dontSendNotification);
        fileLabel.setText (sf.isLoaded() ? (sf.getSoundFontFile().existsAsFile() ? sf.getSoundFontFile().getFileName() : juce::String ("GeneralUser GS (built in)"))
                                         : juce::String ("Sound Library not installed"), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::accent2);
        g.fillRect (0, 0, getWidth(), 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (30);
        defaultButton.setBounds (top.removeFromRight (90).reduced (0, 3));
        top.removeFromRight (6);
        loadButton.setBounds (top.removeFromRight (120).reduced (0, 3));
        title.setBounds (top);
        fileLabel.setBounds (r.removeFromTop (18));
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (96);
        for (auto* k : knobs) k->setBounds (bottom.removeFromLeft (90));
        categoryList.setBounds (r.removeFromLeft (200));
        r.removeFromLeft (8);
        presetList.setBounds (r);
    }

    // categories ListBoxModel
    int getNumRows() override { return categories.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (sel) { g.setColour (theme::accent2.withAlpha (0.35f)); g.fillRect (0, 0, w, h); }
        g.setColour (theme::text);
        g.setFont (uiFont (13.5f, sel));
        g.drawText (categories[row], 10, 0, w - 12, h, juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int) override { presetList.updateContent(); presetList.repaint(); }

    struct PresetModel : public juce::ListBoxModel
    {
        SoundFontEditor* owner = nullptr;
        juce::Array<SoundFontCache::PresetInfo> current() const
        {
            if (owner == nullptr) return {};
            const int row = owner->categoryList.getSelectedRow();
            return row >= 0 ? owner->byCategory[owner->categories[row]] : juce::Array<SoundFontCache::PresetInfo>();
        }
        int getNumRows() override { return current().size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
        {
            auto list = current();
            if (! juce::isPositiveAndBelow (row, list.size())) return;
            auto& p = list.getReference (row);
            const bool active = p.bank == owner->sf.getBank() && p.program == owner->sf.getProgram();
            if (active) { g.setColour (theme::accent.withAlpha (0.3f)); g.fillRect (0, 0, w, h); }
            g.setColour (active ? juce::Colours::white : theme::text);
            g.setFont (uiFont (13.0f, active));
            g.drawText (p.name, 10, 0, w - 60, h, juce::Justification::centredLeft);
            g.setColour (theme::textFaint);
            g.setFont (uiFont (10.5f));
            g.drawText (juce::String (p.bank) + ":" + juce::String (p.program), w - 56, 0, 50, h, juce::Justification::centredRight);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            auto list = current();
            if (! juce::isPositiveAndBelow (row, list.size())) return;
            auto& p = list.getReference (row);
            owner->sf.setPreset (p.bank, p.program);
            if (owner->sf.onDisplayNameChanged) owner->sf.onDisplayNameChanged (p.name);
            owner->title.setText ("Sound Library  -  " + p.name, juce::dontSendNotification);
            owner->presetList.repaint();
        }
    };

private:
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a SoundFont", juce::File(), "*.sf2");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            auto err = sf.loadSoundFont (f);
            if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SoundFont", err);
            sf.setPreset (0, 0);
            rebuild();
        });
    }

    SoundFontInstrument& sf;
    juce::Label title, fileLabel;
    juce::TextButton loadButton { "Load .sf2..." }, defaultButton { "Built-in" };
    juce::ListBox categoryList, presetList;
    PresetModel presetModel;
    juce::StringArray categories;
    std::map<juce::String, juce::Array<SoundFontCache::PresetInfo>> byCategory;
    juce::OwnedArray<wis::Knob> knobs;
    std::unique_ptr<juce::FileChooser> chooser;
};

// =====================================================================================================
//  Amp & Pedals editor: the play-along pedalboard
// =====================================================================================================
class AmpRigEditor : public juce::AudioProcessorEditor
{
public:
    explicit AmpRigEditor (AmpRigFx& p) : AudioProcessorEditor (p), panel (p.rig, nullptr)
    {
        addAndMakeVisible (panel);
        setSize (1240, 340);
        setResizable (true, false);
    }
    void resized() override { panel.setBounds (getLocalBounds()); }
private:
    wis::RigPanel panel;
};

void installBuiltinEditors()
{
    BuiltinProcessor::genericEditorFactory = [] (BuiltinProcessor& p) -> juce::AudioProcessorEditor* { return new BuiltinEditor (p); };
    SoundFontInstrument::editorFactory = [] (SoundFontInstrument& p) -> juce::AudioProcessorEditor* { return new SoundFontEditor (p); };
    AmpRigFx::editorFactory = [] (AmpRigFx& p) -> juce::AudioProcessorEditor* { return new AmpRigEditor (p); };
    installInstrumentEditors();
}

// =====================================================================================================
//  PluginWindow
// =====================================================================================================
PluginWindow::PluginWindow (const juce::String& title, juce::AudioProcessor& p, std::function<void()> onClose)
    : DocumentWindow (title, theme::panel, DocumentWindow::closeButton | DocumentWindow::minimiseButton), processor (p), onCloseCallback (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = p.createEditorIfNeeded();
    if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (p);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();   // the editor tells its processor it's going away
}

void PluginWindow::closeButtonPressed()
{
    if (onCloseCallback) onCloseCallback();   // deletes this window
}

} // namespace wis::daw
