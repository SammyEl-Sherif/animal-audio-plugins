#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace animalaudio::ui
{
inline constexpr const char* kCompanyName = "Animal Audio";
inline constexpr const char* kCompanyNameUpper = "ANIMAL AUDIO";

/** Colour set every Animal Audio widget draws from. Each plugin supplies its own. */
struct Palette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour panelRaised;
    juce::Colour outline;
    juce::Colour text;
    juce::Colour textDim;
    juce::Colour accent;    // primary highlight
    juce::Colour accentAlt; // secondary highlight / gradient end
    juce::Colour onAccent;  // text drawn on top of `accent`
    juce::Colour detail;    // inactive curve handles, small ornaments (Duck: bill orange)
    juce::Colour ambient;   // animated background motif (Duck: water blue)
};

/**
 * Derives a complete palette from one hue (degrees, 0..360): a dark, hue-tinted base with a bright accent
 * and a warmer/cooler secondary. Used by the plugin template so every new plugin starts with its own
 * colour scheme; hand-tune the result (or write the struct out by hand, like Duck) once the design settles.
 */
inline Palette makePaletteFromHue(float hueDegrees)
{
    const float h = std::fmod(std::fmod(hueDegrees, 360.0f) + 360.0f, 360.0f) / 360.0f;
    const auto shift = [h](float d) { return std::fmod(h + d / 360.0f + 1.0f, 1.0f); };

    Palette p;
    p.background = juce::Colour::fromHSV(h, 0.30f, 0.10f, 1.0f);
    p.panel = juce::Colour::fromHSV(h, 0.30f, 0.15f, 1.0f);
    p.panelRaised = juce::Colour::fromHSV(h, 0.28f, 0.22f, 1.0f);
    p.outline = juce::Colour::fromHSV(h, 0.26f, 0.30f, 1.0f);
    p.text = juce::Colour::fromHSV(h, 0.10f, 0.97f, 1.0f);
    p.textDim = juce::Colour::fromHSV(h, 0.18f, 0.66f, 1.0f);
    p.accent = juce::Colour::fromHSV(h, 0.88f, 1.0f, 1.0f);
    p.accentAlt = juce::Colour::fromHSV(shift(-22.0f), 0.90f, 1.0f, 1.0f);
    p.onAccent = juce::Colour::fromHSV(h, 0.60f, 0.12f, 1.0f);
    p.detail = juce::Colour::fromHSV(shift(-35.0f), 0.92f, 1.0f, 1.0f);
    p.ambient = juce::Colour::fromHSV(shift(180.0f), 0.55f, 0.85f, 1.0f);
    return p;
}

/** Bold UI font; falls back to the platform default sans-serif. `kerning` adds letter spacing (0.2 = 20%). */
inline juce::Font makeFont(float height, bool bold = true, float kerning = 0.0f)
{
    return juce::Font(
        juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain).withKerningFactor(kerning));
}
} // namespace animalaudio::ui
