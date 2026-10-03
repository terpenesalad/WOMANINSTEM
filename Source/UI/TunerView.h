#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Rig/Tuner.h"

namespace wis
{

/** Strobe-style tuner readout: big note name, cents needle, frequency. */
class TunerView : public juce::Component
{
public:
    void setReading (const Tuner::Reading& r);
    void paint (juce::Graphics&) override;

private:
    Tuner::Reading reading;
    float needle = 0.0f;
    int framesSinceValid = 100;
};

} // namespace wis
