#pragma once

#include <animalaudio/ui/PillButton.h>

namespace monkeydyne::ui
{
/*
 * Monkeydyne's materials: frosted glass over brushed titanium, lit from above, so every raised surface has a
 * bright top edge and a shaded bottom one (and every recessed one the reverse). Light is the palette's `text`,
 * shade is black.
 */

/** Brushed-metal backdrop. Holds the streak texture, made once and stretched across whatever it fills. */
class BrushedMetal
{
public:
    void paint(juce::Graphics& g, juce::Rectangle<float> area, const animalaudio::ui::Palette& palette);

private:
    juce::Image tile;
};

/** One-pixel bevel: lit on top and shaded below when `raised`, the other way round when recessed. */
void drawBevel(juce::Graphics& g, juce::Rectangle<float> area, float radius, const animalaudio::ui::Palette& palette,
               bool raised);

/** Frosted glass slab: soft shadow, translucent body, gloss on its upper part, bevelled rim. */
void drawGlassPanel(juce::Graphics& g, juce::Rectangle<float> area, float radius,
                    const animalaudio::ui::Palette& palette);

/** Recessed smoked-glass well (meters, the overview strip). */
void drawGlassWell(juce::Graphics& g, juce::Rectangle<float> area, float radius,
                   const animalaudio::ui::Palette& palette);

/** Coloured glass filling `shape` (notes, lit buttons): deeper colour below, gloss above, a bright rim. */
void fillTintedGlass(juce::Graphics& g, const juce::Path& shape, juce::Colour tint,
                     const animalaudio::ui::Palette& palette, float opacity = 1.0f);

/** Glossy bead with a soft glow, e.g. the colour key of a panel. */
void drawGem(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour);

/** Text in polished chrome (the sky above the horizon, the ground below), left aligned and centred in `area`. */
void drawChromeText(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                    const animalaudio::ui::Palette& palette);

/** A PillButton in glass: clear when off, lit coloured glass when on. */
class GlassButton : public animalaudio::ui::PillButton
{
public:
    GlassButton(const juce::String& text, const animalaudio::ui::Palette& palette);

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    animalaudio::ui::Palette colours;
};
} // namespace monkeydyne::ui
