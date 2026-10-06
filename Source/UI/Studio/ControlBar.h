#pragma once

#include "StudioContext.h"

namespace wis::daw
{

class TransportButton : public juce::Button
{
public:
    enum class Kind { toStart, rewind, forward, stop, play, record, cycle, metronome, countIn, typing };
    TransportButton (Kind k, const juce::String& tip) : juce::Button (tip), kind (k) { setTooltip (tip); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    Kind kind;
    bool lit = false;
};

/** The LCD: position in bars/beats and time, tempo (drag / double-click / tap), time signature. */
class Lcd : public juce::Component, private juce::Timer
{
public:
    explicit Lcd (StudioContext& ctx);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> keyArea() const;
    juce::Rectangle<int> tempoArea() const;
    juce::Rectangle<int> sigArea() const;
    StudioContext& ctx;
    double dragStartTempo = 120;
    bool draggingTempo = false;
    juce::TextButton tap { "TAP" };
    juce::Array<double> taps;
};

class ControlBar : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    explicit ControlBar (StudioContext& ctx);
    ~ControlBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onProjectMenu, onToggleBrowser, onToggleEditor, onToggleMixer, onToggleKeys, onToggleTyping;
    void setPanelStates (bool browser, bool editor, bool mixer, bool keys, bool typing);

    juce::TextButton projectButton { "Project" };
    std::function<double()> cpuUsage;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { syncToggles(); }
    void syncToggles();

    StudioContext& ctx;
    TransportButton toStart { TransportButton::Kind::toStart, "Go to start (Enter)" },
                    rewind  { TransportButton::Kind::rewind, "Back one bar (,)" },
                    forward { TransportButton::Kind::forward, "Forward one bar (.)" },
                    stopB   { TransportButton::Kind::stop, "Stop (Space)" },
                    playB   { TransportButton::Kind::play, "Play (Space)" },
                    recB    { TransportButton::Kind::record, "Record (R)" },
                    cycleB  { TransportButton::Kind::cycle, "Cycle / loop (C). Drag in the ruler's top strip to set it" },
                    metroB  { TransportButton::Kind::metronome, "Metronome (K)" },
                    countB  { TransportButton::Kind::countIn, "Count-in: 1 bar of clicks before recording" },
                    typingB { TransportButton::Kind::typing, "Musical Typing: play notes with your computer keyboard (Ctrl+K)" };
    Lcd lcd;
    juce::TextButton browserB { "Library" }, editorB { "Editor" }, mixerB { "Mixer" }, keysB { "Keys" };
    juce::Slider master { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    LevelMeter meterL { false }, meterR { false };
    juce::Label cpu;
    int tick = 0;
};

} // namespace wis::daw
