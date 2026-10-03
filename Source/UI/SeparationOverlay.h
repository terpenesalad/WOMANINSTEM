#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wis
{

/** Full-window overlay shown while a song is being separated (or a model downloaded). */
class SeparationOverlay : public juce::Component
{
public:
    SeparationOverlay();

    void start (const juce::String& songTitle);
    void update (float progress01, const juce::String& stage);
    void setDetail (const juce::String& text) { detail = text; repaint(); }

    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks

private:
    juce::String title, stage, detail;
    float progress = 0.0f;
    double startTime = 0.0;
    float rateEstimate = 0.0f;   // progress per second, smoothed
    float lastProgress = 0.0f;
    double lastTime = 0.0;
    juce::TextButton cancel { "Cancel" };
    juce::Rectangle<float> card;
};

} // namespace wis
