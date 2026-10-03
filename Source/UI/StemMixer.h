#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/StemPlayer.h"
#include "LookAndFeel.h"

namespace wis
{

/** One mixer row: colour tag, name, mute/solo, volume fader, balance, activity meter. */
class StemStrip : public juce::Component
{
public:
    StemStrip (StemPlayer& player, StemId id);

    void setPresent (bool isPresent);
    bool isPresent() const { return present; }
    void refreshMeter();
    void syncFromPlayer();

    void paint (juce::Graphics&) override;
    void resized() override;

    StemId getId() const { return id; }
    std::function<void()> onChange;

private:
    StemPlayer& player;
    StemId id;
    bool present = true;

    juce::Label name;
    juce::TextButton mute { "M" }, solo { "S" };
    juce::Slider gain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider balance { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    LevelMeter meter { false };
};

/** The column of stem strips that sits to the left of the waveform lanes. */
class StemMixer : public juce::Component
{
public:
    explicit StemMixer (StemPlayer& player);

    void setSong (PlayableSong::Ptr song);
    void refresh();                       // timer: meters
    void layoutRows (int rulerHeight, int mixHeight, int top, int rowHeight);

    /** Mutes the stem(s) for the part you're about to play. */
    void soloOut (StemId id, bool mutePart);
    void resetAll();
    void sync();                          // re-read mute/solo/gain from the player

    std::array<bool, numStemIds> audibleStems() const;

    void paint (juce::Graphics&) override;
    void resized() override {}

    std::function<void()> onMixChanged;

private:
    StemPlayer& player;
    juce::OwnedArray<StemStrip> strips;
    juce::Label mixLabel;
    int rulerH = 22, mixH = 44;
};

} // namespace wis
