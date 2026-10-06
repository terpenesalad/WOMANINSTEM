#include "EditorParts.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "Daw/Instruments/Sampler.h"
#include "Daw/Instruments/HomeKeys.h"
#include "Daw/Plugins/Looper.h"
#include "Daw/Plugins/VocalTune.h"
#include "Daw/Plugins/MidiEffects.h"

namespace wis::daw
{

static bool isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a");
}

static juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

// =====================================================================================================
//  Sampler
// =====================================================================================================
class SampleView : public juce::Component, private juce::Timer
{
public:
    explicit SampleView (Sampler& s) : sampler (s) { startTimerHz (30); }

    void timerCallback() override { repaint(); }

    float xFor (float frac) const { return 4.0f + frac * (getWidth() - 8.0f); }
    float fracFor (float x) const { return juce::jlimit (0.0f, 1.0f, (x - 4.0f) / (getWidth() - 8.0f)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 6.0f);
        auto* data = sampler.getSample();
        if (data == nullptr || data->peaks.empty())
        {
            g.setColour (theme::textFaint);
            g.setFont (uiFont (14.0f));
            g.drawText ("Drop an audio file here, or click Load Sample...", r, juce::Justification::centred);
            return;
        }
        const int mode = (int) sampler.param ("mode");
        const float start = sampler.param ("start"), end = sampler.param ("end");
        const float ls = sampler.param ("loopStart"), le = sampler.param ("loopEnd");
        const bool loop = sampler.param ("loop") > 0.5f && mode == Sampler::classic;

        if (loop)
        {
            g.setColour (theme::good.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (ls), r.getY(), xFor (le), r.getBottom()));
        }

        // waveform
        const auto& pk = data->peaks;
        const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        for (int x = 4; x < getWidth() - 4; ++x)
        {
            const float f0 = fracFor ((float) x), f1 = fracFor ((float) x + 1.0f);
            const size_t a = (size_t) (f0 * (pk.size() - 1)), b = juce::jmax (a + 1, (size_t) (f1 * (pk.size() - 1)));
            float v = 0.0f;
            for (size_t i = a; i < juce::jmin (b, pk.size()); ++i) v = juce::jmax (v, pk[i]);
            const bool inside = f0 >= start && f0 <= end;
            g.setColour (inside ? theme::accent2.brighter (0.2f) : theme::textFaint.withAlpha (0.5f));
            g.drawVerticalLine (x, mid - v * half, mid + v * half + 1.0f);
        }

        // slices
        if (mode == Sampler::slice)
        {
            const auto slices = sampler.currentSlices();
            g.setFont (uiFont (10.0f, true));
            for (size_t i = 0; i < slices.size(); ++i)
            {
                const float x = xFor ((float) slices[i] / (float) data->length());
                g.setColour (theme::warn.withAlpha (0.8f));
                g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                if (i + 1 < slices.size() || slices.size() == 1)
                {
                    g.setColour (theme::text);
                    g.drawText (noteName (Sampler::firstSliceNote + (int) i), (int) x + 3, 4, 40, 12, juce::Justification::left);
                }
            }
        }

        auto marker = [&] (float frac, juce::Colour c, const char* label)
        {
            const float x = xFor (frac);
            g.setColour (c);
            g.drawLine (x, r.getY(), x, r.getBottom(), 2.0f);
            g.fillRect (x - 1.0f, r.getBottom() - 14.0f, 30.0f, 14.0f);
            g.setColour (juce::Colours::black);
            g.setFont (uiFont (9.5f, true));
            g.drawText (label, (int) x + 1, (int) r.getBottom() - 14, 28, 14, juce::Justification::centred);
        };
        marker (start, theme::good, "S");
        marker (end, theme::bad, "E");
        if (loop) { marker (ls, juce::Colours::yellow, "L1"); marker (le, juce::Colours::yellow, "L2"); }

        const float pos = sampler.playPosition.load();
        if (pos >= 0.0f)
        {
            g.setColour (juce::Colours::white);
            g.drawVerticalLine ((int) xFor (pos), r.getY(), r.getBottom());
        }
        g.setColour (theme::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText (data->name + "   " + juce::String (data->length() / data->sampleRate, 2) + " s", r.reduced (8, 4), juce::Justification::topRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = {};
        float best = 10.0f;
        const bool loop = sampler.param ("loop") > 0.5f;
        for (auto id : { "start", "end", "loopStart", "loopEnd" })
        {
            if (! loop && juce::String (id).startsWith ("loop")) continue;
            const float d = std::abs (xFor (sampler.param (id)) - (float) e.x);
            if (d < best) { best = d; dragging = id; }
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging.isNotEmpty()) sampler.setParam (dragging, fracFor ((float) e.x));
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = {}; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        bool near = false;
        for (auto id : { "start", "end", "loopStart", "loopEnd" })
            near = near || std::abs (xFor (sampler.param (id)) - (float) e.x) < 10.0f;
        setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

private:
    Sampler& sampler;
    juce::String dragging;
};

class SamplerEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit SamplerEditor (Sampler& s) : AudioProcessorEditor (s), sampler (s), header (s), view (s), params (s, {}, theme::accent2)
    {
        header.addButton (loadButton, 120);
        loadButton.onClick = [this] { choose(); };
        updateTitle();
        addAndMakeVisible (header);
        addAndMakeVisible (view);
        addAndMakeVisible (params);
        hint.setText ("Classic: play it across the keys (root key = original pitch).  One Shot: plays to the end.  "
                      "Slice: chops it up, one slice per key from C2 - great for loops and breaks. Drag the S / E / L markers.",
                      juce::dontSendNotification);
        hint.setFont (uiFont (11.0f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (hint);
        setSize (860, 52 + 190 + 36 + params.heightFor (860 - 24) + 12);
    }

    void paint (juce::Graphics& g) override { paintEditorBackground (g, getLocalBounds(), theme::accent2); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        view.setBounds (r.removeFromTop (180));
        hint.setBounds (r.removeFromTop (32));
        r.removeFromTop (4);
        params.setBounds (r);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() > 0 && isAudioFile (files[0]); }
    void filesDropped (const juce::StringArray& files, int, int) override { load (juce::File (files[0])); }

private:
    void choose()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult()); });
    }
    void load (const juce::File& f)
    {
        auto err = sampler.loadFile (f);
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sampler", err);
        updateTitle();
    }
    void updateTitle()
    {
        auto* d = sampler.getSample();
        header.setTitle (d != nullptr ? "Sampler  -  " + d->name : juce::String ("Sampler"));
        if (d != nullptr && sampler.onDisplayNameChanged) sampler.onDisplayNameChanged (d->name);
    }

    Sampler& sampler;
    EditorHeader header;
    SampleView view;
    ParamPanel params;
    juce::Label hint;
    juce::TextButton loadButton { "Load Sample..." };
    std::unique_ptr<juce::FileChooser> chooser;
};

// =====================================================================================================
//  Drum Pads
// =====================================================================================================
class DrumPadsEditor;

class PadButton : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    PadButton (DrumPadsEditor& o, int p) : owner (o), pad (p) {}
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() > 0 && isAudioFile (files[0]); }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    float flash = 0.0f;
private:
    DrumPadsEditor& owner;
    int pad;
    bool dragOver = false;
};

class DrumPadsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DrumPadsEditor (DrumPads& d) : AudioProcessorEditor (d), pads (d), header (d, "Drum Pads"), volume (d, { "volume" }, theme::accent2)
    {
        header.onPresetChanged = [this] { for (auto* b : buttons) b->repaint(); };
        addAndMakeVisible (header);
        for (int i = 0; i < DrumPads::numPads; ++i) addAndMakeVisible (buttons.add (new PadButton (*this, i)));
        padTitle.setFont (uiFont (15.0f, true));
        addAndMakeVisible (padTitle);
        loadButton.onClick = [this] { choose (selected); };
        addAndMakeVisible (loadButton);
        addAndMakeVisible (volume);
        hint.setText ("Click a pad to hear it and edit it. Drop audio files onto pads (or right-click) to load your own samples. "
                      "Pads play MIDI notes C2 to D#3 (the standard drum notes), so drum grooves play them too.", juce::dontSendNotification);
        hint.setFont (uiFont (11.0f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);
        select (0);
        setSize (840, 520);
        startTimerHz (30);
    }

    void select (int p)
    {
        selected = p;
        padTitle.setText ("Pad " + juce::String (p + 1) + "  (" + noteName (DrumPads::firstNote + p) + ")  -  " + pads.padName (p), juce::dontSendNotification);
        juce::StringArray ids;
        for (auto n : { "tune", "gain", "pan", "decay", "cutoff", "reverse", "choke" }) ids.add (DrumPads::padParam (p, n));
        padParams = std::make_unique<ParamPanel> (pads, ids, theme::accent2);
        addAndMakeVisible (*padParams);
        for (auto* b : buttons) b->repaint();
        resized();
    }

    void choose (int pad)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample onto pad " + juce::String (pad + 1),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, pad] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (pad, fc.getResult()); });
    }
    void loadFile (int pad, const juce::File& f)
    {
        auto err = pads.loadPadFile (pad, f);
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Drum Pads", err);
        select (pad);
    }

    void paint (juce::Graphics& g) override { paintEditorBackground (g, getLocalBounds(), theme::accent2); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        auto grid = r.removeFromLeft (440);
        const int cell = juce::jmin (grid.getWidth(), grid.getHeight()) / 4;
        for (int i = 0; i < DrumPads::numPads; ++i)
        {
            const int row = 3 - i / 4, col = i % 4;   // pad 1 bottom-left, like hardware
            buttons[i]->setBounds (grid.getX() + col * cell, grid.getY() + row * cell, cell, cell);
        }
        r.removeFromLeft (16);
        auto top = r.removeFromTop (28);
        loadButton.setBounds (top.removeFromRight (120));
        padTitle.setBounds (top);
        r.removeFromTop (6);
        if (padParams != nullptr) padParams->setBounds (r.removeFromTop (padParams->heightFor (r.getWidth())).withWidth (r.getWidth()));
        r.removeFromTop (6);
        volume.setBounds (r.removeFromTop (ParamPanel::knobH));
        hint.setBounds (r);
    }

    DrumPads& pads;
    int selected = 0;

private:
    void timerCallback() override
    {
        const int hit = pads.lastHit.exchange (-1);
        if (juce::isPositiveAndBelow (hit, DrumPads::numPads)) buttons[hit]->flash = 1.0f;
        for (auto* b : buttons)
            if (b->flash > 0.0f) { b->flash = juce::jmax (0.0f, b->flash - 0.12f); b->repaint(); }
    }

    EditorHeader header;
    juce::OwnedArray<PadButton> buttons;
    juce::Label padTitle, hint;
    juce::TextButton loadButton { "Load Sample..." };
    std::unique_ptr<ParamPanel> padParams;
    ParamPanel volume;
    std::unique_ptr<juce::FileChooser> chooser;
};

void PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (5.0f);
    const bool sel = owner.selected == pad;
    const auto base = juce::Colour::fromHSV ((float) (pad % 8) / 8.0f * 0.8f + 0.55f, 0.35f, 0.32f, 1.0f);
    g.setColour (base.interpolatedWith (juce::Colours::white, flash * 0.6f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (dragOver ? theme::good : sel ? theme::accent2 : theme::outline);
    g.drawRoundedRectangle (r, 8.0f, sel || dragOver ? 2.5f : 1.0f);
    g.setColour (theme::text);
    g.setFont (uiFont (12.0f, true));
    g.drawFittedText (owner.pads.padName (pad), r.reduced (6).toNearestInt(), juce::Justification::centred, 2);
    g.setColour (theme::textFaint);
    g.setFont (uiFont (10.0f));
    g.drawText (juce::String (pad + 1), r.reduced (6, 4), juce::Justification::topLeft);
    g.drawText (noteName (DrumPads::firstNote + pad), r.reduced (6, 4), juce::Justification::bottomRight);
}

void PadButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Load Sample...");
        juce::PopupMenu kits;
        auto names = owner.pads.getProgramNames();
        for (int i = 0; i < names.size(); ++i) kits.addItem (10 + i, names[i]);
        m.addSubMenu ("Reset All Pads to Kit", kits);
        juce::Component::SafePointer<PadButton> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            auto& o = safe->owner;
            if (r == 1) o.choose (safe->pad);
            else if (r >= 10) { o.pads.setCurrentProgram (r - 10); o.select (o.selected); }
        });
        return;
    }
    owner.pads.previewPad (pad);
    owner.select (pad);
}

void PadButton::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    owner.loadFile (pad, juce::File (files[0]));
}

// =====================================================================================================
//  HomeKeys 20: an 80s home keyboard panel
// =====================================================================================================
namespace retro
{
    const juce::Colour body { 0xff26252a }, panel { 0xff34333a }, silver { 0xffb9bcc2 }, lcd { 0xff9fb38a }, lcdText { 0xff1d2618 },
                       red { 0xffe0483e }, orange { 0xffe8a33c }, blue { 0xff4f8fd6 }, cream { 0xffe9e2cf };
}

class RetroButton : public juce::Component
{
public:
    RetroButton (juce::String num, juce::String label, juce::Colour c) : number (std::move (num)), text (std::move (label)), colour (c) {}
    std::function<void()> onClick;
    bool lit = false;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto cap = r.removeFromTop (juce::jmin (22.0f, r.getHeight() * 0.5f)).reduced (3.0f, 1.0f);
        g.setColour (colour.darker (isMouseButtonDown() ? 0.4f : 0.0f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        // LED
        g.setColour (lit ? retro::red : juce::Colour (0xff4a2020));
        g.fillEllipse (cap.getRight() - 9.0f, cap.getY() + 3.0f, 5.0f, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.setFont (uiFont (10.0f, true));
        g.drawText (number, cap.reduced (4, 0), juce::Justification::centredLeft);
        g.setColour (lit ? juce::Colours::white : retro::cream.withAlpha (0.75f));
        g.setFont (uiFont (9.5f, lit));
        g.drawFittedText (text.toUpperCase(), r.toNearestInt().reduced (1, 0), juce::Justification::centredTop, 2, 0.8f);
    }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { repaint(); if (contains (e.getPosition()) && onClick) onClick(); }
private:
    juce::String number, text;
    juce::Colour colour;
};

class HomeKeysEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HomeKeysEditor (HomeKeys& k)
        : AudioProcessorEditor (k), keys (k), header (k, "HomeKeys 20"),
          knobs (k, { "volume", "tempo", "rhythmVol", "bassVol", "chordVol", "vintage", "bright", "split" }, retro::orange),
          switches (k, { "abc", "kit", "chordTone", "ensemble", "vibrato", "sustain", "rhythmOn" }, retro::orange)
    {
        addAndMakeVisible (header);
        const auto tones = HomeKeys::toneNames();
        for (int i = 0; i < tones.size(); ++i)
        {
            auto* b = toneButtons.add (new RetroButton (juce::String (i + 1).paddedLeft ('0', 2), tones[i], retro::silver));
            b->onClick = [this, i] { keys.setParam ("tone", (float) i); };
            addAndMakeVisible (b);
        }
        const auto rhythms = HomeKeys::rhythmNames();
        for (int i = 0; i < rhythms.size(); ++i)
        {
            auto* b = rhythmButtons.add (new RetroButton (juce::String (i + 1).paddedLeft ('0', 2), rhythms[i], retro::blue.withSaturation (0.35f).brighter (0.3f)));
            b->onClick = [this, i] { keys.setParam ("rhythm", (float) i); };
            addAndMakeVisible (b);
        }
        startButton.onClick = [this] { keys.startStop(); };
        syncButton.onClick = [this] { keys.setParam ("syncStart", keys.param ("syncStart") > 0.5f ? 0.0f : 1.0f); };
        fillButton.onClick = [this] { keys.requestFill(); };
        for (auto* b : { &startButton, &syncButton, &fillButton }) addAndMakeVisible (b);
        addAndMakeVisible (knobs);
        addAndMakeVisible (switches);
        setSize (1000, 640);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (retro::body);
        auto r = getLocalBounds().toFloat();
        // speaker grilles
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (auto area : { grilleL, grilleR })
            for (float y = area.getY() + 4; y < area.getBottom() - 2; y += 6)
                for (float x = area.getX() + 4; x < area.getRight() - 2; x += 6)
                    g.fillEllipse (x, y, 3.0f, 3.0f);
        // silver stripe
        g.setColour (retro::silver.withAlpha (0.6f));
        g.fillRect (r.withY (46.0f).withHeight (2.0f));
        g.setColour (retro::red);
        g.fillRect (r.withY (49.0f).withHeight (1.0f));

        // LCD
        auto l = lcdArea.toFloat();
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (l.expanded (4.0f), 6.0f);
        g.setColour (retro::lcd);
        g.fillRoundedRectangle (l, 3.0f);
        g.setColour (retro::lcdText);
        juce::Font mono (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
        g.setFont (mono);
        const int tone = (int) keys.param ("tone"), rhythm = (int) keys.param ("rhythm");
        auto line = [&] (int i, const juce::String& s) { g.drawText (s, l.reduced (10, 6).withHeight (22).translated (0, i * 22.0f), juce::Justification::centredLeft); };
        line (0, "TONE   " + juce::String (tone + 1).paddedLeft ('0', 2) + "  " + HomeKeys::toneNames()[tone].toUpperCase());
        line (1, "RHYTHM " + juce::String (rhythm + 1).paddedLeft ('0', 2) + "  " + HomeKeys::rhythmNames()[rhythm].toUpperCase());
        const auto chord = HomeKeys::chordName (keys.chordRoot.load(), keys.chordType.load());
        line (2, "TEMPO  " + juce::String ((int) keys.param ("tempo")) + "     ACC " + (keys.param ("abc") > 0.5f ? (chord.isNotEmpty() ? chord : juce::String ("--")) : juce::String ("OFF")));
        // beat lights
        const int step = keys.currentStep.load();
        auto lights = l.reduced (10, 6).withTop (l.getBottom() - 22.0f);
        for (int i = 0; i < 4; ++i)
        {
            const bool on = step >= 0 && (step / 4) % 4 == i && keys.isRhythmRunning();
            g.setColour (on ? retro::lcdText : retro::lcdText.withAlpha (0.18f));
            g.fillRect (lights.getX() + i * 26.0f, lights.getY() + 4.0f, 18.0f, 8.0f);
        }
        g.setFont (mono.withHeight (12.0f));
        g.setColour (retro::lcdText);
        g.drawText (keys.isRhythmRunning() ? "PLAY" : keys.param ("syncStart") > 0.5f ? "SYNC" : "", lights.withTrimmedLeft (120), juce::Justification::centredLeft);

        g.setColour (retro::cream.withAlpha (0.8f));
        g.setFont (uiFont (11.0f, true));
        g.drawText ("TONE SELECT", toneLabel, juce::Justification::centredLeft);
        g.drawText ("RHYTHM SELECT", rhythmLabel, juce::Justification::centredLeft);
        g.drawText ("HomeKeys 20  -  20 RHYTHMS  -  AUTO ACCOMPANIMENT", brandArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (14);
        auto top = r.removeFromTop (110);
        grilleL = top.removeFromLeft (110).toFloat().reduced (4.0f);
        grilleR = top.removeFromRight (110).toFloat().reduced (4.0f);
        lcdArea = top.removeFromLeft (440).reduced (10, 6);
        auto transport = top.reduced (10, 10);
        const int bw = transport.getWidth() / 3;
        startButton.setBounds (transport.removeFromLeft (bw).reduced (4));
        syncButton.setBounds (transport.removeFromLeft (bw).reduced (4));
        fillButton.setBounds (transport.reduced (4));
        r.removeFromTop (6);

        toneLabel = r.removeFromTop (16);
        auto toneArea = r.removeFromTop (88);
        layoutGrid (toneButtons, toneArea, 8);
        rhythmLabel = r.removeFromTop (16);
        auto rhythmArea = r.removeFromTop (88);
        layoutGrid (rhythmButtons, rhythmArea, 10);
        r.removeFromTop (6);
        brandArea = r.removeFromBottom (16);
        switches.setBounds (r.removeFromTop (switches.heightFor (r.getWidth())));
        knobs.setBounds (r);
    }

private:
    static void layoutGrid (juce::OwnedArray<RetroButton>& buttons, juce::Rectangle<int> area, int perRow)
    {
        const int rows = (buttons.size() + perRow - 1) / perRow;
        const int w = area.getWidth() / perRow, h = area.getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (area.getX() + (i % perRow) * w, area.getY() + (i / perRow) * h, w, h);
    }

    void timerCallback() override
    {
        const int tone = (int) keys.param ("tone"), rhythm = (int) keys.param ("rhythm");
        for (int i = 0; i < toneButtons.size(); ++i) if (toneButtons[i]->lit != (i == tone)) { toneButtons[i]->lit = i == tone; toneButtons[i]->repaint(); }
        for (int i = 0; i < rhythmButtons.size(); ++i) if (rhythmButtons[i]->lit != (i == rhythm)) { rhythmButtons[i]->lit = i == rhythm; rhythmButtons[i]->repaint(); }
        startButton.setButtonText (keys.isRhythmRunning() ? "STOP" : "START");
        syncButton.setToggleState (keys.param ("syncStart") > 0.5f, juce::dontSendNotification);
        repaint (lcdArea.expanded (6));
    }

    HomeKeys& keys;
    EditorHeader header;
    juce::OwnedArray<RetroButton> toneButtons, rhythmButtons;
    juce::TextButton startButton { "START" }, syncButton { "SYNC START" }, fillButton { "FILL IN" };
    ParamPanel knobs, switches;
    juce::Rectangle<int> lcdArea, toneLabel, rhythmLabel, brandArea;
    juce::Rectangle<float> grilleL, grilleR;
};

// =====================================================================================================
//  Loop Station
// =====================================================================================================
class LooperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LooperEditor (Looper& l) : AudioProcessorEditor (l), looper (l), header (l, "Loop Station"), params (l, {}, theme::accent)
    {
        addAndMakeVisible (header);
        stopButton.onClick = [this] { looper.pressStop(); };
        undoButton.onClick = [this] { looper.pressUndo(); };
        redoButton.onClick = [this] { looper.pressRedo(); };
        clearButton.onClick = [this] { looper.pressClear(); };
        toTrack.onClick = [this] { putOnTrack(); };
        toTrack.setTooltip ("Puts the loop on this track in the song, at the playhead, as an audio clip");
        for (auto* b : { &stopButton, &undoButton, &redoButton, &clearButton, &toTrack }) addAndMakeVisible (b);
        addAndMakeVisible (params);
        hint.setText ("Big button: record, then play, then overdub layers. Set 'On the Bar' to start and stop exactly on the beat while the song plays. "
                      "Put the track's input on (arm / monitor) to loop your instrument or voice.", juce::dontSendNotification);
        hint.setFont (uiFont (11.0f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);
        setSize (720, 300 + params.heightFor (720 - 24));
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        paintEditorBackground (g, getLocalBounds(), theme::accent);
        auto c = pedal.toFloat();
        const auto st = looper.getState();
        const juce::Colour col = st == Looper::recording ? theme::bad : st == Looper::overdubbing ? theme::warn
                               : st == Looper::playing ? theme::good : theme::textFaint;
        // overview ring
        const auto ov = looper.getOverview();
        const float cx = c.getCentreX(), cy = c.getCentreY(), radius = c.getWidth() * 0.5f;
        g.setColour (theme::bg);
        g.fillEllipse (c);
        for (int i = 0; i < 64; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * i / 64.0f - juce::MathConstants<float>::halfPi;
            const float len = 4.0f + ov[(size_t) i] * radius * 0.3f;
            g.setColour (theme::accent.withAlpha (0.7f));
            g.drawLine (cx + std::cos (a) * (radius - 4.0f), cy + std::sin (a) * (radius - 4.0f),
                        cx + std::cos (a) * (radius - 4.0f - len), cy + std::sin (a) * (radius - 4.0f - len), 2.0f);
        }
        // position
        if (looper.getLoopSeconds() > 0.0)
        {
            juce::Path arc;
            arc.addCentredArc (cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * looper.getPosition(), true);
            g.setColour (col);
            g.strokePath (arc, juce::PathStrokeType (4.0f));
        }
        auto inner = c.reduced (radius * 0.42f);
        g.setColour (col.withAlpha (mainDown ? 1.0f : 0.85f));
        g.fillEllipse (inner);
        g.setColour (juce::Colours::black);
        g.setFont (uiFont (13.0f, true));
        const char* next = st == Looper::empty ? "REC" : st == Looper::recording ? "PLAY" : st == Looper::playing ? "DUB" : "PLAY";
        g.drawText (next, inner, juce::Justification::centred);

        auto info = infoArea.toFloat();
        g.setColour (theme::text);
        g.setFont (uiFont (22.0f, true));
        const char* names[] = { "Empty", "Recording", "Playing", "Overdubbing", "Stopped" };
        g.drawText (names[(int) st], info.removeFromTop (30), juce::Justification::centredLeft);
        g.setFont (uiFont (14.0f));
        g.setColour (theme::textDim);
        g.drawText (looper.getLoopSeconds() > 0 ? juce::String (looper.getLoopSeconds(), 2) + " s loop,  " + juce::String (looper.getLayers()) + " layer" + (looper.getLayers() == 1 ? "" : "s")
                                                : juce::String ("Press the big button to record"), info.removeFromTop (22), juce::Justification::centredLeft);
        if (looper.isWaitingForBar())
        {
            g.setColour (theme::warn);
            g.drawText ("Waiting for the next bar...", info.removeFromTop (22), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        auto top = r.removeFromTop (190);
        pedal = top.removeFromLeft (190).reduced (6);
        top.removeFromLeft (16);
        infoArea = top.removeFromTop (80);
        auto buttons = top.removeFromTop (34);
        const int bw = buttons.getWidth() / 4;
        for (auto* b : { &stopButton, &undoButton, &redoButton, &clearButton }) b->setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
        top.removeFromTop (8);
        toTrack.setBounds (top.removeFromTop (30).withWidth (220));
        hint.setBounds (top.reduced (0, 4));
        r.removeFromTop (8);
        params.setBounds (r);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (pedal.toFloat().contains (e.position) && pedal.getCentre().toFloat().getDistanceFrom (e.position) < pedal.getWidth() * 0.5f)
        {
            mainDown = true;
            looper.pressMain();
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent&) override { mainDown = false; repaint(); }

private:
    void timerCallback() override
    {
        undoButton.setEnabled (looper.getLayers() > 1 || looper.getState() == Looper::overdubbing);
        redoButton.setEnabled (looper.canRedo());
        toTrack.setEnabled (looper.getLoopSeconds() > 0.0 && BuiltinProcessor::onAudioToTrack != nullptr);
        repaint();
    }

    void putOnTrack()
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getNonexistentChildFile ("Loop " + juce::Time::getCurrentTime().formatted ("%H-%M-%S"), ".wav");
        auto err = looper.exportLoop (f);
        if (err.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Loop Station", err); return; }
        if (BuiltinProcessor::onAudioToTrack) BuiltinProcessor::onAudioToTrack (looper, f);
    }

    Looper& looper;
    EditorHeader header;
    ParamPanel params;
    juce::TextButton stopButton { "Stop" }, undoButton { "Undo" }, redoButton { "Redo" }, clearButton { "Clear" }, toTrack { "Put Loop on Track" };
    juce::Label hint;
    juce::Rectangle<int> pedal, infoArea;
    bool mainDown = false;
};

// =====================================================================================================
//  Vocal Tune
// =====================================================================================================
class PitchGraph : public juce::Component, private juce::Timer
{
public:
    explicit PitchGraph (VocalTune& v) : tune (v) { history.fill ({ -1.0f, -1.0f }); startTimerHz (30); }

    void timerCallback() override
    {
        history[(size_t) pos] = { tune.detectedNote.load(), tune.targetNote.load() };
        pos = (pos + 1) % (int) history.size();
        // follow the voice: centre on the recent average
        float sum = 0; int n = 0;
        for (auto& h : history) if (h.first > 0) { sum += h.first; ++n; }
        if (n > 0) centre += (sum / n - centre) * 0.05f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 6.0f);
        const float span = 14.0f;
        const float lo = std::round (centre) - span * 0.5f;
        auto yFor = [&] (float note) { return r.getBottom() - (note - lo) / span * r.getHeight(); };
        const int keyParam = (int) tune.param ("key");
        const int key = keyParam == 0 ? BuiltinProcessor::songKey.load() : keyParam - 1;
        const int scale = keyParam == 0 && (int) tune.param ("scale") == 0 ? BuiltinProcessor::songScale.load() : (int) tune.param ("scale");
        const auto& steps = MidiFxBase::scaleSteps (scale);
        g.setFont (uiFont (10.0f));
        for (int n = (int) std::floor (lo); n <= (int) std::ceil (lo + span); ++n)
        {
            const bool inScale = std::find (steps.begin(), steps.end(), ((n - key) % 12 + 12) % 12) != steps.end();
            const float y = yFor ((float) n);
            g.setColour (inScale ? theme::outline.brighter (0.3f) : theme::outline.withAlpha (0.3f));
            g.drawHorizontalLine ((int) y, r.getX() + 36.0f, r.getRight());
            if (inScale)
            {
                g.setColour (theme::textDim);
                g.drawText (noteName (n), (int) r.getX() + 4, (int) y - 6, 32, 12, juce::Justification::left);
            }
        }
        auto drawLine = [&] (bool target, juce::Colour c, float thick)
        {
            juce::Path p;
            bool started = false;
            const int N = (int) history.size();
            for (int i = 0; i < N; ++i)
            {
                auto h = history[(size_t) ((pos + i) % N)];
                const float v = target ? h.second : h.first;
                const float x = r.getX() + 36.0f + (r.getWidth() - 40.0f) * i / (N - 1);
                if (v <= 0) { started = false; continue; }
                if (! started) { p.startNewSubPath (x, yFor (v)); started = true; }
                else p.lineTo (x, yFor (v));
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thick));
        };
        drawLine (true, theme::accent2.withAlpha (0.9f), 3.0f);
        drawLine (false, juce::Colours::white.withAlpha (0.85f), 1.5f);

        const float det = tune.detectedNote.load(), tgt = tune.targetNote.load();
        g.setFont (uiFont (18.0f, true));
        g.setColour (theme::text);
        if (det > 0)
        {
            const int nearest = (int) std::round (det);
            const int cents = (int) std::round ((det - nearest) * 100.0f);
            g.drawText (noteName (nearest) + juce::String (cents >= 0 ? "  +" : "  ") + juce::String (cents) + " ct" + (tgt > 0 ? "   ->   " + noteName ((int) std::round (tgt)) : juce::String()),
                        r.reduced (12, 8), juce::Justification::topRight);
        }
        else
        {
            g.setColour (theme::textFaint);
            g.drawText ("Sing or play a single note...", r.reduced (12, 8), juce::Justification::topRight);
        }
        g.setFont (uiFont (10.5f));
        g.setColour (theme::textFaint);
        g.drawText ("white = your pitch, purple = where it's tuned to", r.reduced (12, 6), juce::Justification::bottomRight);
    }

private:
    VocalTune& tune;
    std::array<std::pair<float, float>, 180> history;
    int pos = 0;
    float centre = 60.0f;
};

class VocalTuneEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalTuneEditor (VocalTune& v) : AudioProcessorEditor (v), header (v), graph (v), params (v, {}, theme::accent2)
    {
        addAndMakeVisible (header);
        addAndMakeVisible (graph);
        addAndMakeVisible (params);
        hint.setText ("Retune Speed 0 ms = the hard, robotic effect. 20-60 ms = natural. 'Song Key' follows the key set in the control bar.",
                      juce::dontSendNotification);
        hint.setFont (uiFont (11.0f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (hint);
        setSize (760, 52 + 220 + 30 + params.heightFor (760 - 24) + 12);
    }
    void paint (juce::Graphics& g) override { paintEditorBackground (g, getLocalBounds(), theme::accent2); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        graph.setBounds (r.removeFromTop (210));
        hint.setBounds (r.removeFromTop (28));
        r.removeFromTop (4);
        params.setBounds (r);
    }
private:
    EditorHeader header;
    PitchGraph graph;
    ParamPanel params;
    juce::Label hint;
};

void installInstrumentEditors()
{
    Sampler::editorFactory = [] (Sampler& s) -> juce::AudioProcessorEditor* { return new SamplerEditor (s); };
    DrumPads::editorFactory = [] (DrumPads& d) -> juce::AudioProcessorEditor* { return new DrumPadsEditor (d); };
    HomeKeys::editorFactory = [] (HomeKeys& k) -> juce::AudioProcessorEditor* { return new HomeKeysEditor (k); };
    Looper::editorFactory = [] (Looper& l) -> juce::AudioProcessorEditor* { return new LooperEditor (l); };
    VocalTune::editorFactory = [] (VocalTune& v) -> juce::AudioProcessorEditor* { return new VocalTuneEditor (v); };
}

} // namespace wis::daw
