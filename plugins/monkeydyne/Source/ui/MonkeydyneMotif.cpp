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

juce::Colour fur(const animalaudio::ui::Palette& palette)
{
    return palette.accentAlt.interpolatedWith(palette.background, 0.4f);
}

juce::Colour face(const animalaudio::ui::Palette& palette)
{
    return palette.text.interpolatedWith(palette.accentAlt, 0.42f);
}

juce::Colour glint(const animalaudio::ui::Palette& palette)
{
    return palette.accentAlt.interpolatedWith(palette.text, 0.45f);
}

juce::Colour metalLight(const animalaudio::ui::Palette& palette)
{
    return palette.text.interpolatedWith(palette.textDim, 0.45f);
}

juce::Colour metalDark(const animalaudio::ui::Palette& palette)
{
    return palette.panelRaised.darker(0.35f);
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
    // Polished bronze: a bright crown, the dark band of a reflected horizon, warm light returning at the jaw, and a
    // specular glint where the light lands.
    juce::ColourGradient bronze(furColour.brighter(0.9f), at(0.5f, 0.1f).x, at(0.5f, 0.1f).y, furColour.brighter(0.2f),
                                at(0.5f, 0.9f).x, at(0.5f, 0.9f).y, false);
    bronze.addColour(0.42, furColour.brighter(0.35f));
    bronze.addColour(0.58, furColour.darker(0.4f));
    bronze.addColour(0.76, furColour.darker(0.05f));
    g.setGradientFill(bronze);
    g.fillPath(head);
    const auto glintCentre = at(0.37f, 0.27f);
    g.setGradientFill(juce::ColourGradient(palette.text.withAlpha(0.55f), glintCentre.x, glintCentre.y,
                                           palette.text.withAlpha(0.0f), glintCentre.x + size * 0.16f, glintCentre.y,
                                           true));
    g.fillEllipse(oval(0.37f, 0.27f, 0.32f, 0.18f));

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

    g.setColour(palette.accent.withAlpha(0.35f));
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
    const auto light = metalLight(palette);
    const auto dark = metalDark(palette);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(disc(centre, radius).translated(0.0f, radius * 0.1f));
    // Polished bezel, lit from above.
    g.setGradientFill(
        juce::ColourGradient(light, centre.x, centre.y - radius, dark, centre.x, centre.y + radius, false));
    g.fillEllipse(disc(centre, radius));

    // Spun-metal face: two bright lobes facing the light, the grooves of the lathe.
    const float faceRadius = radius * 0.84f;
    const auto face = disc(centre, faceRadius);
    g.setColour(dark.interpolatedWith(light, 0.25f));
    g.fillEllipse(face);
    constexpr int sectors = 40;
    constexpr float step = 2.0f * kPi / static_cast<float>(sectors);
    for (int i = 0; i < sectors; ++i)
    {
        const float from = static_cast<float>(i) * step;
        const float shine = 0.5f + 0.5f * std::cos(2.0f * (from + 0.5f * step + 0.55f));
        juce::Path wedge;
        wedge.addPieSegment(face, from, from + step * 1.08f, 0.0f);
        g.setColour(dark.interpolatedWith(light, 0.12f + 0.5f * shine * shine));
        g.fillPath(wedge);
    }
    g.setColour(juce::Colours::black.withAlpha(0.07f));
    for (float r = faceRadius * 0.3f; r < faceRadius; r += std::max(1.5f, faceRadius * 0.12f))
        g.drawEllipse(disc(centre, r), 0.5f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(face, std::max(0.6f, radius * 0.04f));

    // The pointer is a banana in the knob's colour, its stem at the value, polished like the rest.
    const float length = faceRadius * 0.95f;
    const auto place = juce::AffineTransform::rotation(angle).translated(centre);
    const auto across = juce::Point<float>(length * 0.3f, 0.0f).transformedBy(juce::AffineTransform::rotation(angle));
    const auto banana = makeBanana(length, length * 0.3f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillPath(banana, place.translated(0.0f, radius * 0.06f));
    juce::ColourGradient polish(palette.accent.brighter(0.7f), centre.x + across.x, centre.y + across.y,
                                palette.accent.darker(0.45f), centre.x - across.x, centre.y - across.y, false);
    polish.addColour(0.4, palette.accent);
    g.setGradientFill(polish);
    g.fillPath(banana, place);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.strokePath(banana, juce::PathStrokeType(std::max(0.6f, radius * 0.03f)), place);

    const auto hub = disc(centre, radius * 0.14f);
    g.setGradientFill(juce::ColourGradient(light, centre.x, hub.getY(), dark, centre.x, hub.getBottom(), false));
    g.fillEllipse(hub);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(hub, 0.6f);
}

void MonkeydyneMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                                  const animalaudio::ui::Palette& palette) const
{
    g.setGradientFill(juce::ColourGradient(palette.panel, 0.0f, area.getY(), palette.ambient, 0.0f,
                                           area.getY() + area.getHeight() * 0.7f, false));
    g.fillRect(area);
    // Light through tinted glass: orchid pooling low on the left, gold high on the right.
    const float reach = std::max(area.getWidth(), area.getHeight()) * 0.55f;
    g.setGradientFill(juce::ColourGradient(palette.accent.withAlpha(0.07f), area.getX() + area.getWidth() * 0.12f,
                                           area.getBottom(), palette.accent.withAlpha(0.0f),
                                           area.getX() + area.getWidth() * 0.12f + reach, area.getBottom(), true));
    g.fillRect(area);
    g.setGradientFill(
        juce::ColourGradient(palette.accentAlt.withAlpha(0.045f), area.getRight() - area.getWidth() * 0.15f,
                             area.getY(), palette.accentAlt.withAlpha(0.0f),
                             area.getRight() - area.getWidth() * 0.15f + reach * 0.8f, area.getY(), true));
    g.fillRect(area);

    // Silver filigree vines hang from the top and sway with the clock the caller passes (the host playhead), so a
    // still transport is a still jungle. {x, length as a share of the height, phase}
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
        g.setColour(palette.textDim.withAlpha(0.09f));
        g.strokePath(stem, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto leaf = makeLeaf(leafLength);
        g.setColour(palette.textDim.withAlpha(0.065f));
        for (int i = 1; i <= 5; ++i)
        {
            const float u = static_cast<float>(i) / 5.2f;
            const float side = i % 2 == 0 ? 1.0f : -1.0f;
            const float droop = side * (0.9f + 0.15f * std::sin(t * 0.8f + vine[2] + static_cast<float>(i)));
            g.fillPath(leaf, juce::AffineTransform::rotation(-droop).translated(pointAt(u)));
        }
    }

    // Gold dust drifts and glints: the brightest specks flash a four-point star.
    const auto glow = glint(palette);
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
        g.setColour(glow.withAlpha(0.08f + 0.16f * pulse));
        g.fillEllipse(disc({x, y}, 1.1f));
        if (pulse > 0.75f)
        {
            const float arm = 2.0f + 10.0f * (pulse - 0.75f);
            g.setColour(glow.withAlpha(0.5f * (pulse - 0.75f)));
            g.fillRect(x - arm, y - 0.35f, arm * 2.0f, 0.7f);
            g.fillRect(x - 0.35f, y - arm, 0.7f, arm * 2.0f);
        }
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
    g.setColour(palette.textDim.withAlpha(0.08f));
    for (const auto& leaf : leaves)
    {
        const auto c = juce::Point<float>(panelArea.getX() + panelArea.getWidth() * leaf[0],
                                          panelArea.getY() + panelArea.getHeight() * leaf[1]);
        g.strokePath(makeLeaf(leaf[2]), juce::PathStrokeType(1.0f),
                     juce::AffineTransform::rotation(leaf[3]).translated(c));
    }
}
} // namespace monkeydyne::ui
