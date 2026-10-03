#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace wis
{

/** Palette - dark studio theme with a hot-pink accent. */
namespace theme
{
    inline const juce::Colour bg          { 0xff0e1014 };
    inline const juce::Colour panel       { 0xff161920 };
    inline const juce::Colour panelRaised { 0xff1d212a };
    inline const juce::Colour outline     { 0xff2a2f3b };
    inline const juce::Colour text        { 0xffe8eaf0 };
    inline const juce::Colour textDim     { 0xff8a92a6 };
    inline const juce::Colour textFaint   { 0xff5a6175 };
    inline const juce::Colour accent      { 0xffff4d8d };
    inline const juce::Colour accent2     { 0xff7c5cff };
    inline const juce::Colour good        { 0xff34d399 };
    inline const juce::Colour warn        { 0xfffbbf24 };
    inline const juce::Colour bad         { 0xffef4444 };
    inline const juce::Colour ledOff      { 0xff3a3f4c };
}

juce::Font uiFont (float height, bool bold = false);

class WisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WisLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String& textToShow) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override { return 32; }
    juce::Font getAlertWindowTitleFont() override { return uiFont (17.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return uiFont (14.5f); }
    juce::Font getAlertWindowFont() override { return uiFont (14.0f); }
};

/** A small round "LED" power switch used on every effect module. */
class PowerButton : public juce::ToggleButton
{
public:
    PowerButton() { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool down) override;
    juce::Colour onColour = theme::accent;
};

/** Horizontal or vertical peak meter with hold. Call setLevel() from a timer. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (bool vertical = false) : isVertical (vertical) {}
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;
    juce::Colour colour = theme::good;

private:
    bool isVertical;
    float level = 0.0f, hold = 0.0f;
    int holdFrames = 0;
};

} // namespace wis
