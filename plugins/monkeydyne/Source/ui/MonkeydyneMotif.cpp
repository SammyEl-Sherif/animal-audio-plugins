#include "MonkeydyneMotif.h"

#include <cmath>

namespace monkeydyne::ui
{
namespace
{
constexpr float kPi = 3.14159265f;

juce::Rectangle<float> disc(juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}

/** The banana yellow turned toward orange: warm, before it is darkened into fur and lightened into the face. */
juce::Colour tan(const animalaudio::ui::Palette& palette)
{
    return palette.accent.withRotatedHue(-0.055f);
}

juce::Colour fur(const animalaudio::ui::Palette& palette)
{
    return tan(palette).interpolatedWith(palette.background, 0.5f);
}

juce::Colour face(const animalaudio::ui::Palette& palette)
{
    return palette.text.interpolatedWith(tan(palette), 0.35f);
}

juce::Colour firefly(const animalaudio::ui::Palette& palette)
{
    return palette.accent.interpolatedWith(palette.text, 0.45f);
}

float fract(float x)
{
    return x - std::floor(x);
}

/** An almond leaf along +y from the origin, `length` long. */
juce::Path makeLeaf(float length)
{
    const float w = length * 0.42f;
    juce::Path leaf;
    leaf.startNewSubPath(0.0f, 0.0f);
    leaf.cubicTo(w, length * 0.25f, w * 0.8f, length * 0.75f, 0.0f, length);
    leaf.cubicTo(-w * 0.8f, length * 0.75f, -w, length * 0.25f, 0.0f, 0.0f);
    leaf.closeSubPath();
    return leaf;
}
} // namespace

juce::Path makeBanana(float length, float bow)
{
    // About three times as long as it bows; the inner curve bows a third as much, so it is fattest mid-way.
    const float base = -0.05f * length;
    const float tip = -0.9f * length;
    const float span = tip - base;
    const float outer = bow / 0.75f;
    const float inner = 0.3f * outer;
    juce::Path banana;
    banana.startNewSubPath(0.0f, base);
    banana.cubicTo(outer, base + 0.25f * span, outer, base + 0.75f * span, 0.0f, tip);
    banana.cubicTo(inner, base + 0.75f * span, inner, base + 0.25f * span, 0.0f, base);
    banana.closeSubPath();
    const float stem = std::max(1.0f, bow * 0.45f);
    banana.addRoundedRectangle(-stem * 0.5f, -length, stem, length + tip + stem, stem * 0.3f);
    // Centre the curve's bulk on the axis, so the pointer reads straight.
    banana.applyTransform(juce::AffineTransform::translation(-bow * 0.55f, 0.0f));
    return banana;
}

void MonkeydyneMotif::drawMascot(juce::Graphics& g, juce::Rectangle<float> area,
                                 const animalaudio::ui::Palette& palette) const
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
    const auto at = [&](float x, float y) { return juce::Point<float>(box.getX() + x * size, box.getY() + y * size); };
    const auto oval = [&](float x, float y, float w, float h)
    { return juce::Rectangle<float>(w * size, h * size).withCentre(at(x, y)); };
    const float line = std::max(0.7f, size * 0.018f);
    const auto furColour = fur(palette);
    const auto faceColour = face(palette);

    juce::Path head;
    head.addEllipse(oval(0.16f, 0.50f, 0.27f, 0.27f));
    head.addEllipse(oval(0.84f, 0.50f, 0.27f, 0.27f));
    head.addEllipse(oval(0.50f, 0.49f, 0.66f, 0.62f));
    juce::Path tuft;
    tuft.startNewSubPath(at(0.42f, 0.22f));
    tuft.cubicTo(at(0.41f, 0.12f), at(0.52f, 0.05f), at(0.62f, 0.09f));
    tuft.cubicTo(at(0.54f, 0.11f), at(0.51f, 0.15f), at(0.58f, 0.21f));
    tuft.closeSubPath();
    head.addPath(tuft);

    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillPath(head, juce::AffineTransform::translation(0.0f, size * 0.03f));
    // Outline first: the fill covers the half of the stroke inside, including where ears and tuft overlap.
    g.setColour(palette.background.withAlpha(0.6f));
    g.strokePath(head, juce::PathStrokeType(line * 1.6f));
    g.setGradientFill(juce::ColourGradient(furColour.brighter(0.35f), at(0.5f, 0.15f).x, at(0.5f, 0.15f).y,
                                           furColour.darker(0.35f), at(0.5f, 0.85f).x, at(0.5f, 0.85f).y, false));
    g.fillPath(head);

    g.setColour(faceColour.darker(0.25f));
    g.fillEllipse(oval(0.175f, 0.505f, 0.15f, 0.15f));
    g.fillEllipse(oval(0.825f, 0.505f, 0.15f, 0.15f));

    // Heart-shaped face: two lobes around the eyes over a wide muzzle.
    juce::Path mask;
    mask.addEllipse(oval(0.395f, 0.44f, 0.30f, 0.30f));
    mask.addEllipse(oval(0.605f, 0.44f, 0.30f, 0.30f));
    mask.addEllipse(oval(0.50f, 0.63f, 0.47f, 0.31f));
    g.setGradientFill(juce::ColourGradient(faceColour.brighter(0.1f), at(0.5f, 0.32f).x, at(0.5f, 0.32f).y,
                                           faceColour.darker(0.12f), at(0.5f, 0.78f).x, at(0.5f, 0.78f).y, false));
    g.fillPath(mask);

    g.setColour(palette.accentAlt.withAlpha(0.3f));
    g.fillEllipse(oval(0.31f, 0.60f, 0.08f, 0.05f));
    g.fillEllipse(oval(0.69f, 0.60f, 0.08f, 0.05f));

    for (const float x : {0.405f, 0.595f})
    {
        g.setColour(palette.background);
        g.fillEllipse(oval(x, 0.45f, 0.075f, 0.09f));
        g.setColour(palette.text);
        g.fillEllipse(disc(at(x + 0.012f, 0.432f), size * 0.014f));
    }

    const auto features = furColour.darker(0.7f);
    g.setColour(features);
    g.fillEllipse(oval(0.474f, 0.585f, 0.026f, 0.018f));
    g.fillEllipse(oval(0.526f, 0.585f, 0.026f, 0.018f));
    juce::Path grin;
    grin.startNewSubPath(at(0.40f, 0.655f));
    grin.quadraticTo(at(0.50f, 0.745f), at(0.60f, 0.655f));
    g.strokePath(grin, juce::PathStrokeType(line * 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MonkeydyneMotif::drawKnobCap(juce::Graphics& g, juce::Point<float> centre, float radius, float angle,
                                  const animalaudio::ui::Palette& palette) const
{
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(disc(centre, radius).translated(0.0f, radius * 0.08f));
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.3f), centre.x, centre.y - radius,
                                           palette.background.darker(0.2f), centre.x, centre.y + radius, false));
    g.fillEllipse(disc(centre, radius));
    g.setColour(palette.text.withAlpha(0.09f));
    g.drawEllipse(disc(centre, radius - 0.5f), 1.0f);

    const float faceRadius = radius * 0.84f;
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.06f), centre.x, centre.y - faceRadius,
                                           palette.panel.darker(0.15f), centre.x, centre.y + faceRadius, false));
    g.fillEllipse(disc(centre, faceRadius));

    // The pointer is a banana in the knob's colour, its stem at the value.
    const float length = faceRadius * 0.95f;
    const auto place = juce::AffineTransform::rotation(angle).translated(centre);
    const auto tip = juce::Point<float>(0.0f, -length).transformedBy(place);
    const auto banana = makeBanana(length, length * 0.3f);
    g.setGradientFill(juce::ColourGradient(palette.accent.brighter(0.3f), tip.x, tip.y, palette.accent.darker(0.5f),
                                           centre.x, centre.y, false));
    g.fillPath(banana, place);
    g.setColour(palette.background.withAlpha(0.5f));
    g.strokePath(banana, juce::PathStrokeType(std::max(0.6f, radius * 0.025f)), place);

    g.setColour(palette.background);
    g.fillEllipse(disc(centre, radius * 0.13f));
    g.setColour(palette.text.withAlpha(0.18f));
    g.drawEllipse(disc(centre, radius * 0.13f), 1.0f);
}

void MonkeydyneMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                                  const animalaudio::ui::Palette& palette) const
{
    // Vines hang from the canopy and sway with the clock the caller passes (the host playhead), so a still
    // transport is a still jungle. {x, length as a share of the height, phase}
    static constexpr float vines[][3] = {{0.04f, 0.30f, 0.0f}, {0.17f, 0.18f, 1.7f}, {0.31f, 0.38f, 3.1f},
                                         {0.52f, 0.22f, 4.4f}, {0.66f, 0.34f, 0.9f}, {0.83f, 0.16f, 2.6f},
                                         {0.95f, 0.27f, 5.3f}};
    const auto t = static_cast<float>(timeSeconds);
    const float leafLength = juce::jlimit(5.0f, 11.0f, area.getHeight() * 0.035f);
    for (const auto& vine : vines)
    {
        const float x0 = area.getX() + area.getWidth() * vine[0];
        const float length = std::min(area.getHeight() * vine[1], 170.0f);
        const float sway = 4.0f * std::sin(t * 0.55f + vine[2]);
        const auto pointAt = [&](float u)
        {
            const float curl = 3.0f * std::sin(u * kPi * 1.6f + vine[2]);
            return juce::Point<float>(x0 + curl + sway * u * u, area.getY() + length * u);
        };
        juce::Path stem;
        stem.startNewSubPath(pointAt(0.0f));
        for (int i = 1; i <= 16; ++i)
            stem.lineTo(pointAt(static_cast<float>(i) / 16.0f));
        g.setColour(palette.detail.withAlpha(0.07f));
        g.strokePath(stem, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto leaf = makeLeaf(leafLength);
        g.setColour(palette.detail.withAlpha(0.055f));
        for (int i = 1; i <= 5; ++i)
        {
            const float u = static_cast<float>(i) / 5.2f;
            const float side = i % 2 == 0 ? 1.0f : -1.0f;
            const float droop = side * (0.9f + 0.15f * std::sin(t * 0.8f + vine[2] + static_cast<float>(i)));
            g.fillPath(leaf, juce::AffineTransform::rotation(-droop).translated(pointAt(u)));
        }
    }

    // Fireflies wander and pulse.
    const auto glow = firefly(palette);
    constexpr int flies = 14;
    for (int i = 0; i < flies; ++i)
    {
        const float seed = static_cast<float>(i) * 0.61803f;
        const float speed = 0.6f + 0.8f * fract(seed * 7.3f);
        const float x =
            area.getX() + area.getWidth() * fract(seed) + 22.0f * std::sin(t * 0.21f * speed + seed * 11.0f);
        const float y = area.getY() + area.getHeight() * (0.3f + 0.65f * fract(seed * 3.7f)) +
                        12.0f * std::cos(t * 0.17f * speed + seed * 5.0f);
        const float pulse = 0.5f + 0.5f * std::sin(t * 1.3f * speed + seed * 9.0f);
        g.setColour(glow.withAlpha(0.03f * pulse));
        g.fillEllipse(disc({x, y}, 5.5f));
        g.setColour(glow.withAlpha(0.08f + 0.14f * pulse));
        g.fillEllipse(disc({x, y}, 1.2f));
    }
}

void MonkeydyneMotif::drawDecoration(juce::Graphics& g, juce::Rectangle<float> panelArea,
                                     const animalaudio::ui::Palette& palette) const
{
    static constexpr float leaves[][4] = {{0.08f, 0.80f, 7.0f, 0.6f},
                                          {0.13f, 0.68f, 5.0f, -0.9f},
                                          {0.90f, 0.18f, 6.0f, 2.4f},
                                          {0.94f, 0.30f, 4.0f, 1.2f},
                                          {0.55f, 0.88f, 5.0f, -2.2f}};
    g.setColour(palette.detail.withAlpha(0.07f));
    for (const auto& leaf : leaves)
    {
        const auto c = juce::Point<float>(panelArea.getX() + panelArea.getWidth() * leaf[0],
                                          panelArea.getY() + panelArea.getHeight() * leaf[1]);
        g.strokePath(makeLeaf(leaf[2]), juce::PathStrokeType(1.0f),
                     juce::AffineTransform::rotation(leaf[3]).translated(c));
    }
}
} // namespace monkeydyne::ui
