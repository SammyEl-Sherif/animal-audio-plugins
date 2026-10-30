#pragma once

#include <animalaudio/ui/Theme.h>

namespace whale::ui
{
/** Whale's tail fluke pointing up (notch at the top), stock ending at the origin; `length` tall. */
juce::Path makeFluke(float length, float span);

/**
 * The humpback: a chubby whale with knobbly head, long pale pectoral fin, throat grooves and a spout; a tail fluke
 * as the knob pointer; bubbles rising through faint light shafts as the screen background.
 */
class WhaleMotif : public animalaudio::ui::Motif
{
public:
    void drawMascot(juce::Graphics&, juce::Rectangle<float> area, const animalaudio::ui::Palette&) const override;
    void drawKnobCap(juce::Graphics&, juce::Point<float> centre, float radius, float angle,
                     const animalaudio::ui::Palette&) const override;
    void drawAmbient(juce::Graphics&, juce::Rectangle<float> area, double timeSeconds,
                     const animalaudio::ui::Palette&) const override;
    void drawDecoration(juce::Graphics&, juce::Rectangle<float> panelArea,
                        const animalaudio::ui::Palette&) const override;
};
} // namespace whale::ui
