#pragma once

#include <animalaudio/dsp/ShapeCurve.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace animalaudio::ui
{
/** Layered sine ripples across `area` (water/grass/wind motif). `time` animates them. */
void drawRipples(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, double time);

/** Polyline of a curve inside `area` (y = 1 at the top edge). */
juce::Path makeCurvePath(const dsp::ShapeCurve& curve, juce::Rectangle<float> area, int steps = 48);

/** Solid triangle pointing left or right, centred in `area` (stepper buttons, "becomes" arrows). */
void drawArrowGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool right);
} // namespace animalaudio::ui
