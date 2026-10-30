#pragma once

#include <animalaudio/ui/Theme.h>

namespace parrot::ui
{
/** A feather pointing up from the origin (base) to (0, -length). */
juce::Path makeFeather(float length, float width);

/** Scarlet macaw: head in profile with a hooked beak, feather knob pointers, drifting feathers. */
class ParrotMotif : public animalaudio::ui::Motif
{
public:
    void drawMascot(juce::Graphics&, juce::Rectangle<float> area, const animalaudio::ui::Palette&) const override;
    void drawKnobCap(juce::Graphics&, juce::Point<float> centre, float radius, float angle,
                     const animalaudio::ui::Palette&) const override;
    void drawAmbient(juce::Graphics&, juce::Rectangle<float> area, double timeSeconds,
                     const animalaudio::ui::Palette&) const override;
};
} // namespace parrot::ui
