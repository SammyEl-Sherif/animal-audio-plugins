#include "Glass.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace monkeydyne::ui
{
namespace
{
using animalaudio::ui::Palette;

/** Rows of noise smeared sideways into streaks: white where the metal catches the light, black where it doesn't. */
juce::Image makeBrushedTile()
{
    constexpr int width = 1024;
    constexpr int height = 256;
    constexpr int reach = 48;
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
    juce::Random random(0x4d4b4431); // fixed, so the metal is the same every time
    std::vector<float> noise(static_cast<std::size_t>(width));
    const auto at = [&](int x) { return noise[static_cast<std::size_t>((x + width) % width)]; };
    float row = 0.0f;
    for (int y = 0; y < height; ++y)
    {
        for (auto& n : noise)
            n = random.nextFloat() * 2.0f - 1.0f;
        row = 0.8f * row + 0.2f * (random.nextFloat() * 2.0f - 1.0f);
        float sum = 0.0f;
        for (int i = -reach; i <= reach; ++i)
            sum += at(i);
        for (int x = 0; x < width; ++x)
        {
            const float streak = 4.0f * sum / static_cast<float>(2 * reach + 1) + 0.6f * row + 0.25f * at(x);
            sum += at(x + reach + 1) - at(x - reach);
            const float alpha = std::min(0.12f, std::abs(streak) * 0.06f);
            pixels.setPixelColour(x, y, (streak > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha(alpha));
        }
    }
    return image;
}

juce::Rectangle<float> disc(juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}
} // namespace

void BrushedMetal::paint(juce::Graphics& g, juce::Rectangle<float> area, const Palette& palette)
{
    g.setGradientFill(juce::ColourGradient(palette.panel.brighter(0.1f), 0.0f, area.getY(), palette.background, 0.0f,
                                           area.getBottom(), false));
    g.fillRect(area);

    // A broad reflection runs diagonally across the plate.
    const auto light = palette.text;
    juce::ColourGradient sheen(light.withAlpha(0.0f), area.getX(), area.getY(), light.withAlpha(0.0f), area.getRight(),
                               area.getBottom(), false);
    sheen.addColour(0.22, light.withAlpha(0.05f));
    sheen.addColour(0.38, light.withAlpha(0.0f));
    sheen.addColour(0.62, light.withAlpha(0.025f));
    g.setGradientFill(sheen);
    g.fillRect(area);

    if (!tile.isValid())
        tile = makeBrushedTile();
    // One texture row per physical pixel row, so the streaks stay fine on high-density screens.
    const float scale = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float tileHeight = static_cast<float>(tile.getHeight()) / scale;
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area.getSmallestIntegerContainer());
    for (float y = area.getY(); y < area.getBottom(); y += tileHeight)
        g.drawImage(tile, {area.getX(), y, area.getWidth(), tileHeight}, juce::RectanglePlacement::stretchToFit);
}

void drawBevel(juce::Graphics& g, juce::Rectangle<float> area, float radius, const Palette& palette, bool raised)
{
    const auto light = palette.text;
    const auto shade = juce::Colours::black;
    juce::ColourGradient rim(raised ? light.withAlpha(0.34f) : shade.withAlpha(0.6f), 0.0f, area.getY(),
                             raised ? shade.withAlpha(0.5f) : light.withAlpha(0.16f), 0.0f, area.getBottom(), false);
    rim.addColour(0.3, raised ? light.withAlpha(0.08f) : shade.withAlpha(0.3f));
    rim.addColour(0.7, raised ? light.withAlpha(0.03f) : light.withAlpha(0.03f));
    g.setGradientFill(rim);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);

    // A raised edge catches the light hardest a third of the way along its top.
    const float straight = area.getWidth() - 2.0f * radius;
    if (raised && straight > 0.0f)
    {
        juce::ColourGradient glint(light.withAlpha(0.0f), area.getX() + radius, 0.0f, light.withAlpha(0.05f),
                                   area.getRight() - radius, 0.0f, false);
        glint.addColour(0.3, light.withAlpha(0.4f));
        g.setGradientFill(glint);
        g.fillRect(area.getX() + radius, area.getY(), straight, 1.0f);
    }
}

void drawGlassPanel(juce::Graphics& g, juce::Rectangle<float> area, float radius, const Palette& palette)
{
    for (int i = 3; i >= 1; --i)
    {
        const float spread = 1.6f * static_cast<float>(i);
        g.setColour(juce::Colours::black.withAlpha(0.11f));
        g.fillRoundedRectangle(area.expanded(spread * 0.5f).translated(0.0f, spread + 1.0f), radius + spread * 0.5f);
    }

    const auto light = palette.text;
    g.setGradientFill(juce::ColourGradient(light.withAlpha(0.085f), 0.0f, area.getY(), light.withAlpha(0.03f), 0.0f,
                                           area.getBottom(), false));
    g.fillRoundedRectangle(area, radius);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(area, radius);
        g.reduceClipRegion(clip);
        const auto upper = area.withHeight(std::min(area.getHeight() * 0.4f, 48.0f));
        g.setGradientFill(juce::ColourGradient(light.withAlpha(0.07f), 0.0f, upper.getY(), light.withAlpha(0.0f), 0.0f,
                                               upper.getBottom(), false));
        g.fillRect(upper);
    }
    drawBevel(g, area, radius, palette, true);
}

void drawGlassWell(juce::Graphics& g, juce::Rectangle<float> area, float radius, const Palette& palette)
{
    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillRoundedRectangle(area, radius);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(area, radius);
        g.reduceClipRegion(clip);
        const float depth = std::min(6.0f, area.getHeight() * 0.5f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), 0.0f, area.getY(),
                                               juce::Colours::transparentBlack, 0.0f, area.getY() + depth, false));
        g.fillRect(area.withHeight(depth));
    }
    drawBevel(g, area, radius, palette, false);
}

void fillTintedGlass(juce::Graphics& g, const juce::Path& shape, juce::Colour tint, const Palette& palette,
                     float opacity)
{
    const auto box = shape.getBounds();
    juce::ColourGradient body(tint.brighter(0.15f).withMultipliedAlpha(0.6f * opacity), 0.0f, box.getY(),
                              tint.darker(0.1f).withMultipliedAlpha(0.9f * opacity), 0.0f, box.getBottom(), false);
    body.addColour(0.55, tint.withMultipliedAlpha(0.62f * opacity));
    g.setGradientFill(body);
    g.fillPath(shape);

    const auto light = palette.text;
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(shape);
        // Gloss across the upper half, held in from the ends like a reflection on a rounded surface.
        const float inset = std::min(box.getHeight() * 0.35f, box.getWidth() * 0.2f);
        const auto gloss = box.withHeight(box.getHeight() * 0.48f).reduced(inset, 0.0f).withTrimmedTop(1.0f);
        g.setGradientFill(juce::ColourGradient(light.withAlpha(0.5f * opacity), 0.0f, gloss.getY(),
                                               light.withAlpha(0.06f * opacity), 0.0f, gloss.getBottom(), false));
        g.fillRoundedRectangle(gloss, gloss.getHeight() * 0.5f);
        // Light gathered along the bottom edge.
        const auto caustic = box.withTrimmedTop(box.getHeight() * 0.7f);
        g.setGradientFill(juce::ColourGradient(light.withAlpha(0.0f), 0.0f, caustic.getY(),
                                               tint.brighter(0.6f).withAlpha(0.35f * opacity), 0.0f,
                                               caustic.getBottom(), false));
        g.fillRect(caustic);
    }
    g.setGradientFill(juce::ColourGradient(light.withAlpha(0.85f * opacity), 0.0f, box.getY(),
                                           tint.withMultipliedAlpha(0.75f * opacity), 0.0f, box.getBottom(), false));
    g.strokePath(shape, juce::PathStrokeType(1.0f));
}

void drawGem(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour)
{
    g.setColour(colour.withAlpha(0.22f));
    g.fillEllipse(disc(centre, radius * 2.0f));
    g.setGradientFill(juce::ColourGradient(colour.brighter(0.7f), centre.x - radius * 0.35f, centre.y - radius * 0.45f,
                                           colour.darker(0.35f), centre.x + radius * 0.7f, centre.y + radius, true));
    g.fillEllipse(disc(centre, radius));
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.fillEllipse(disc(centre.translated(-radius * 0.3f, -radius * 0.35f), radius * 0.32f));
}

void drawChromeText(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                    const Palette& palette)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, area.getX(), 0.0f);
    juce::Path letters;
    glyphs.createPath(letters);
    letters.applyTransform(
        juce::AffineTransform::translation(0.0f, area.getCentreY() - letters.getBounds().getCentreY()));
    const auto ink = letters.getBounds();

    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillPath(letters, juce::AffineTransform::translation(0.0f, 1.5f));
    // Sky fading toward a sharp dark horizon a little below the middle, then the ground, warmed by the accent.
    juce::ColourGradient chrome(palette.text, 0.0f, ink.getY(), palette.text.interpolatedWith(palette.accent, 0.45f),
                                0.0f, ink.getBottom(), false);
    chrome.addColour(0.5, palette.textDim.interpolatedWith(palette.text, 0.35f));
    chrome.addColour(0.53, palette.outline.brighter(0.15f));
    chrome.addColour(0.66, palette.textDim.darker(0.15f));
    g.setGradientFill(chrome);
    g.fillPath(letters);
    g.setColour(palette.text.withAlpha(0.25f));
    g.strokePath(letters, juce::PathStrokeType(0.5f));
}

GlassButton::GlassButton(const juce::String& text, const Palette& palette)
    : PillButton(text, palette)
    , colours(palette)
{
}

void GlassButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const float radius = bounds.getHeight() * 0.5f;
    const bool on = getToggleState();
    juce::Path shape;
    shape.addRoundedRectangle(bounds, radius);

    if (on)
    {
        g.setColour(colours.accent.withAlpha(0.16f));
        g.fillRoundedRectangle(bounds.expanded(1.0f), radius + 1.0f);
        fillTintedGlass(g, shape, isButtonDown ? colours.accent.darker(0.2f) : colours.accent, colours);
    }
    else
    {
        const float lift = isButtonDown ? 0.0f : isMouseOver && isEnabled() ? 0.05f : 0.02f;
        g.setGradientFill(juce::ColourGradient(colours.text.withAlpha(0.07f + lift), 0.0f, bounds.getY(),
                                               colours.text.withAlpha(0.02f), 0.0f, bounds.getBottom(), false));
        g.fillPath(shape);
        drawBevel(g, bounds, radius, colours, !isButtonDown);
    }

    const auto textColour = (on ? colours.onAccent : colours.text).withMultipliedAlpha(isEnabled() ? 1.0f : 0.38f);
    if (glyph)
    {
        glyph(g, bounds.reduced(bounds.getHeight() * 0.3f), textColour);
    }
    else
    {
        g.setColour(textColour);
        g.setFont(animalaudio::ui::makeFont(bounds.getHeight() * 0.5f));
        g.drawText(getButtonText(), bounds, juce::Justification::centred, false);
    }
}
} // namespace monkeydyne::ui
