#pragma once

#include <animalaudio/ui/Theme.h>

namespace monkeydyne::ui
{
/** A banana pointing up from the origin to its stem at y = -length, bowing sideways by `bow`. */
juce::Path makeBanana(float length, float bow);

/**
 * The monkey in polished bronze: a round head with big ears, a heart-shaped face, a hair tuft and a grin; a banana
 * as the pointer on a spun-metal knob cap; silver filigree vines and glinting gold dust behind the notes.
 */
class MonkeydyneMotif : public animalaudio::ui::Motif
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
} // namespace monkeydyne::ui
