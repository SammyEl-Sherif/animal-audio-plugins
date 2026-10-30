#pragma once

#include "Theme.h"

#include <functional>

namespace animalaudio::ui
{
/** Header band: dark gradient, accent stripe on top, the mascot, the plugin name and its tagline. */
void drawTitleBar(juce::Graphics& g, juce::Rectangle<float> header, const Theme& theme);

/** Raised rounded panel (drop shadow, hairline outline) with a coloured key dot before its small-caps title. */
void drawPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, juce::Colour key,
               const Palette& palette);

/** Recessed screen: drop shadow, ambient fill with the motif's background pattern, a soft top shade, outline. */
void drawScreen(juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme, double timeSeconds = 0.0);

/**
 * Piano-roll rows from MIDI `lowest` to `highest` inside `area`: sharps shaded, a line under every C and, with
 * `labels`, its name at the left. `yOf` maps a MIDI value to a y coordinate; `rowHeight` is one semitone.
 */
void drawSemitoneRows(juce::Graphics& g, juce::Rectangle<float> area, const std::function<float(float)>& yOf,
                      int lowest, int highest, float rowHeight, const Palette& palette, bool labels);
} // namespace animalaudio::ui
