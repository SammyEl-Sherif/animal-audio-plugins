#pragma once

#include <animalaudio/ui/RotaryKnob.h>
#include <animalaudio/ui/Theme.h>

namespace parrot::ui
{
/**
 * Rotary knob in one of the macaw's colours with a feather pointer (the theme's knob cap). A bipolar knob
 * (PITCH, FORMANT) draws its arc out from 12 o'clock, where a detent dot marks zero.
 */
class ParrotKnob : public animalaudio::ui::RotaryKnob
{
public:
    ParrotKnob(const animalaudio::ui::Theme& theme, juce::Colour colour, bool bipolar);

    void paint(juce::Graphics& g) override;

private:
    animalaudio::ui::Theme theme;
    juce::Colour colour;
    bool bipolar;
};
} // namespace parrot::ui
