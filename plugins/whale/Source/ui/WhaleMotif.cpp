#include "WhaleMotif.h"

#include <cmath>

namespace whale::ui
{
namespace
{
juce::Rectangle<float> disc(juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}

juce::Colour skin(const animalaudio::ui::Palette& palette)
{
    return palette.accent.interpolatedWith(palette.detail, 0.5f).darker(0.9f);
}

juce::Colour belly(const animalaudio::ui::Palette& palette)
{
    return palette.text.interpolatedWith(palette.accent, 0.18f);
}

float fract(float x)
{
    return x - std::floor(x);
}
} // namespace

juce::Path makeFluke(float length, float span)
{
    const float half = span * 0.5f;
    const float stock = span * 0.07f;
    juce::Path path;
    path.startNewSubPath(-stock, 0.0f);
    path.cubicTo(-stock, -length * 0.45f, -half * 0.55f, -length * 0.72f, -half, -length * 0.97f);
    path.cubicTo(-half * 0.6f, -length * 0.93f, -half * 0.2f, -length * 0.92f, 0.0f, -length * 0.8f);
    path.cubicTo(half * 0.2f, -length * 0.92f, half * 0.6f, -length * 0.93f, half, -length * 0.97f);
    path.cubicTo(half * 0.55f, -length * 0.72f, stock, -length * 0.45f, stock, 0.0f);
    path.closeSubPath();
    return path;
}

void WhaleMotif::drawMascot(juce::Graphics& g, juce::Rectangle<float> area,
                            const animalaudio::ui::Palette& palette) const
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
    const auto at = [&](float x, float y) { return juce::Point<float>(box.getX() + x * size, box.getY() + y * size); };
    const float line = std::max(0.7f, size * 0.016f);

    // Spout first, so the head sits over its base.
    juce::Path spout;
    spout.startNewSubPath(at(0.69f, 0.27f));
    spout.cubicTo(at(0.69f, 0.18f), at(0.66f, 0.12f), at(0.58f, 0.11f));
    spout.startNewSubPath(at(0.69f, 0.27f));
    spout.cubicTo(at(0.69f, 0.18f), at(0.72f, 0.12f), at(0.80f, 0.11f));
    spout.startNewSubPath(at(0.69f, 0.27f));
    spout.lineTo(at(0.69f, 0.07f));
    g.setColour(palette.accent.withAlpha(0.85f));
    g.strokePath(spout, juce::PathStrokeType(std::max(1.0f, size * 0.03f), juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    for (const auto& drop : {at(0.55f, 0.15f), at(0.83f, 0.15f), at(0.69f, 0.03f)})
        g.fillEllipse(disc(drop, size * 0.018f));

    juce::Path body;
    body.startNewSubPath(at(0.78f, 0.29f));
    body.cubicTo(at(0.60f, 0.21f), at(0.38f, 0.30f), at(0.25f, 0.43f));
    body.quadraticTo(at(0.19f, 0.48f), at(0.15f, 0.41f));
    body.lineTo(at(0.12f, 0.47f));
    body.quadraticTo(at(0.20f, 0.60f), at(0.36f, 0.67f));
    body.cubicTo(at(0.52f, 0.76f), at(0.80f, 0.74f), at(0.92f, 0.60f));
    body.cubicTo(at(0.99f, 0.50f), at(0.93f, 0.33f), at(0.78f, 0.29f));
    body.closeSubPath();

    const auto tailRoot = at(0.135f, 0.44f);
    const auto tail = juce::AffineTransform::rotation(-0.75f).translated(tailRoot);
    juce::Path fluke = makeFluke(size * 0.24f, size * 0.30f);
    g.setColour(skin(palette).darker(0.15f));
    g.fillPath(fluke, tail);

    g.setGradientFill(juce::ColourGradient(skin(palette).brighter(0.35f), at(0.6f, 0.22f).x, at(0.6f, 0.22f).y,
                                           skin(palette).darker(0.3f), at(0.6f, 0.75f).x, at(0.6f, 0.75f).y, false));
    g.fillPath(body);

    // Pale throat with its grooves, kept inside the body outline.
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(body);
        const auto throat = juce::Rectangle<float>(size * 0.66f, size * 0.30f).withCentre(at(0.70f, 0.80f));
        g.setColour(belly(palette));
        g.fillEllipse(throat);
        g.setColour(skin(palette).withAlpha(0.55f));
        for (int i = 0; i < 3; ++i)
        {
            const float y = 0.69f + 0.035f * static_cast<float>(i);
            juce::Path groove;
            groove.startNewSubPath(at(0.50f, y + 0.02f));
            groove.quadraticTo(at(0.72f, y + 0.015f), at(0.93f, y - 0.09f));
            g.strokePath(groove, juce::PathStrokeType(line * 0.8f));
        }
    }

    // The humpback's long, pale pectoral fin.
    juce::Path fin;
    fin.startNewSubPath(at(0.53f, 0.61f));
    fin.cubicTo(at(0.50f, 0.71f), at(0.42f, 0.82f), at(0.31f, 0.89f));
    fin.cubicTo(at(0.43f, 0.86f), at(0.56f, 0.76f), at(0.61f, 0.64f));
    fin.closeSubPath();
    g.setGradientFill(juce::ColourGradient(belly(palette), at(0.56f, 0.62f).x, at(0.56f, 0.62f).y,
                                           belly(palette).darker(0.4f), at(0.32f, 0.88f).x, at(0.32f, 0.88f).y, false));
    g.fillPath(fin);

    g.setColour(skin(palette).brighter(0.6f).withAlpha(0.7f));
    for (const auto& knob : {at(0.82f, 0.345f), at(0.875f, 0.385f), at(0.76f, 0.32f)})
        g.fillEllipse(disc(knob, size * 0.016f));

    g.setColour(palette.onAccent);
    g.fillEllipse(disc(at(0.80f, 0.49f), size * 0.034f));
    g.setColour(palette.text);
    g.fillEllipse(disc(at(0.81f, 0.48f), size * 0.011f));
    juce::Path smile;
    smile.startNewSubPath(at(0.83f, 0.585f));
    smile.quadraticTo(at(0.88f, 0.61f), at(0.93f, 0.56f));
    g.setColour(palette.onAccent.withAlpha(0.8f));
    g.strokePath(smile, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(palette.background.withAlpha(0.4f));
    g.strokePath(body, juce::PathStrokeType(line * 0.7f));
}

void WhaleMotif::drawKnobCap(juce::Graphics& g, juce::Point<float> centre, float radius, float angle,
                             const animalaudio::ui::Palette& palette) const
{
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(disc(centre, radius).translated(0.0f, radius * 0.08f));
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.3f), centre.x, centre.y - radius,
                                           palette.background.darker(0.2f), centre.x, centre.y + radius, false));
    g.fillEllipse(disc(centre, radius));
    g.setColour(palette.text.withAlpha(0.09f));
    g.drawEllipse(disc(centre, radius - 0.5f), 1.0f);

    const float face = radius * 0.84f;
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.06f), centre.x, centre.y - face,
                                           palette.panel.darker(0.15f), centre.x, centre.y + face, false));
    g.fillEllipse(disc(centre, face));

    // The pointer is a tail fluke in the knob's colour, the stock rising from the hub.
    const float length = face * 0.92f;
    const auto place = juce::AffineTransform::rotation(angle).translated(centre);
    const auto tip = juce::Point<float>(0.0f, -length).transformedBy(place);
    g.setGradientFill(juce::ColourGradient(palette.accent.brighter(0.3f), tip.x, tip.y, palette.accent.darker(0.5f),
                                           centre.x, centre.y, false));
    g.fillPath(makeFluke(length, face * 0.95f), place);

    g.setColour(palette.background);
    g.fillEllipse(disc(centre, radius * 0.13f));
    g.setColour(palette.text.withAlpha(0.18f));
    g.drawEllipse(disc(centre, radius * 0.13f), 1.0f);
}

void WhaleMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                             const animalaudio::ui::Palette& palette) const
{
    g.setGradientFill(juce::ColourGradient(palette.ambient.brighter(0.35f), 0.0f, area.getY(), palette.ambient, 0.0f,
                                           area.getBottom(), false));
    g.fillRect(area);

    // Light shafts slanting down from the surface.
    static constexpr float shafts[][2] = {{0.12f, 0.06f}, {0.38f, 0.10f}, {0.71f, 0.05f}};
    for (const auto& shaft : shafts)
    {
        const float x = area.getX() + area.getWidth() * shaft[0];
        const float w = area.getWidth() * shaft[1];
        juce::Path beam;
        beam.startNewSubPath(x, area.getY());
        beam.lineTo(x + w, area.getY());
        beam.lineTo(x + w * 2.6f + area.getHeight() * 0.25f, area.getBottom());
        beam.lineTo(x + w * 0.4f + area.getHeight() * 0.25f, area.getBottom());
        beam.closeSubPath();
        g.setGradientFill(juce::ColourGradient(palette.accent.withAlpha(0.035f), 0.0f, area.getY(),
                                               juce::Colours::transparentBlack, 0.0f, area.getBottom(), false));
        g.fillPath(beam);
    }

    // Bubbles rise with the clock the caller passes (the host playhead), so a still transport is a still sea.
    constexpr int bubbles = 18;
    for (int i = 0; i < bubbles; ++i)
    {
        const float seed = static_cast<float>(i) * 0.61803f;
        const float speed = 0.03f + 0.025f * fract(seed * 7.3f);
        const float rise = fract(seed * 3.1f + static_cast<float>(timeSeconds) * speed);
        const float r = 1.5f + 3.0f * fract(seed * 5.7f);
        const float wobble = 4.0f * static_cast<float>(std::sin(timeSeconds * 0.9 + static_cast<double>(seed) * 11.0));
        const float x = area.getX() + area.getWidth() * fract(seed) + wobble;
        const float y = area.getBottom() + r - rise * (area.getHeight() + 2.0f * r);
        g.setColour(palette.accent.withAlpha(0.07f));
        g.drawEllipse(disc({x, y}, r), 1.0f);
        g.setColour(palette.text.withAlpha(0.06f));
        g.fillEllipse(disc({x - r * 0.35f, y - r * 0.35f}, r * 0.3f));
    }
}

void WhaleMotif::drawDecoration(juce::Graphics& g, juce::Rectangle<float> panelArea,
                                const animalaudio::ui::Palette& palette) const
{
    static constexpr float spots[][3] = {
        {0.08f, 0.82f, 3.5f}, {0.13f, 0.70f, 2.0f}, {0.90f, 0.20f, 2.5f}, {0.94f, 0.32f, 1.5f}, {0.55f, 0.90f, 2.0f}};
    for (const auto& spot : spots)
    {
        const auto c = juce::Point<float>(panelArea.getX() + panelArea.getWidth() * spot[0],
                                          panelArea.getY() + panelArea.getHeight() * spot[1]);
        g.setColour(palette.accent.withAlpha(0.06f));
        g.drawEllipse(disc(c, spot[2]), 1.0f);
    }
}
} // namespace whale::ui
