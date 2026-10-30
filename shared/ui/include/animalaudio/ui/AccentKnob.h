#pragma once

#include "RotaryKnob.h"
#include "Theme.h"

namespace animalaudio::ui
{
/**
 * Rotary knob drawn in its own colour (one colour per control), with the Motif's knob cap painted in that colour.
 * A bipolar knob draws its arc out from 12 o'clock, where a detent dot marks the centre value.
 */
class AccentKnob : public RotaryKnob
{
public:
    AccentKnob(const Theme& theme, juce::Colour colour, bool bipolar);

    void paint(juce::Graphics& g) override;

    void setArcColour(juce::Colour newColour);

private:
    Theme theme;
    juce::Colour colour;
    bool bipolar;
};
} // namespace animalaudio::ui
