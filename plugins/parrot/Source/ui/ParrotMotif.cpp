#include "ParrotMotif.h"

#include <cmath>

namespace parrot::ui
{
namespace
{
juce::Rectangle<float> disc(juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}

juce::Colour ivory(const animalaudio::ui::Palette& palette)
{
    return palette.text.interpolatedWith(palette.accentAlt, 0.14f);
}
} // namespace

juce::Path makeFeather(float length, float width)
{
    const float half = width * 0.5f;
    juce::Path path;
    path.startNewSubPath(0.0f, 0.0f);
    path.cubicTo(half * 1.15f, -length * 0.16f, half, -length * 0.74f, 0.0f, -length);
    path.cubicTo(-half, -length * 0.74f, -half * 1.15f, -length * 0.16f, 0.0f, 0.0f);
    path.closeSubPath();
    return path;
}

void ParrotMotif::drawMascot(juce::Graphics& g, juce::Rectangle<float> area,
                             const animalaudio::ui::Palette& palette) const
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
    const auto at = [&](float x, float y) { return juce::Point<float>(box.getX() + x * size, box.getY() + y * size); };

    // Wing bands peek out below the head as offset crescents: blue, then gold, then the scarlet head.
    g.setColour(palette.detail);
    g.fillEllipse(disc(at(0.38f, 0.52f), size * 0.33f));
    g.setColour(palette.accentAlt);
    g.fillEllipse(disc(at(0.41f, 0.47f), size * 0.315f));
    g.setGradientFill(juce::ColourGradient(palette.accent.brighter(0.15f), at(0.45f, 0.12f).x, at(0.45f, 0.12f).y,
                                           palette.accent.darker(0.25f), at(0.45f, 0.72f).x, at(0.45f, 0.72f).y,
                                           false));
    g.fillEllipse(disc(at(0.45f, 0.42f), size * 0.30f));

    // Bare face patch with the macaw's fine feather lines, and a pale-gold eye.
    g.setColour(ivory(palette));
    g.fillEllipse(juce::Rectangle<float>(size * 0.25f, size * 0.29f).withCentre(at(0.60f, 0.41f)));
    g.setColour(palette.accent.withAlpha(0.6f));
    for (int i = 0; i < 3; ++i)
    {
        const float y = 0.45f + 0.04f * static_cast<float>(i);
        juce::Path line;
        line.startNewSubPath(at(0.54f, y));
        line.quadraticTo(at(0.60f, y + 0.02f), at(0.66f, y - 0.005f));
        g.strokePath(line, juce::PathStrokeType(std::max(0.8f, size * 0.017f), juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
    g.setColour(palette.accentAlt);
    g.fillEllipse(disc(at(0.59f, 0.355f), size * 0.055f));
    g.setColour(palette.background);
    g.fillEllipse(disc(at(0.593f, 0.355f), size * 0.03f));

    // Hooked upper mandible over a dark lower one.
    juce::Path lower;
    lower.startNewSubPath(at(0.69f, 0.50f));
    lower.lineTo(at(0.81f, 0.52f));
    lower.cubicTo(at(0.83f, 0.57f), at(0.79f, 0.635f), at(0.72f, 0.625f));
    lower.closeSubPath();
    g.setColour(palette.panelRaised.brighter(0.2f));
    g.fillPath(lower);

    juce::Path upper;
    upper.startNewSubPath(at(0.67f, 0.265f));
    upper.cubicTo(at(0.80f, 0.20f), at(0.96f, 0.29f), at(0.95f, 0.48f));
    upper.cubicTo(at(0.945f, 0.585f), at(0.895f, 0.665f), at(0.84f, 0.685f));
    upper.cubicTo(at(0.865f, 0.60f), at(0.855f, 0.545f), at(0.80f, 0.52f));
    upper.lineTo(at(0.68f, 0.50f));
    upper.closeSubPath();
    g.setGradientFill(juce::ColourGradient(ivory(palette), at(0.75f, 0.22f).x, at(0.75f, 0.22f).y,
                                           ivory(palette).darker(0.35f), at(0.88f, 0.68f).x, at(0.88f, 0.68f).y,
                                           false));
    g.fillPath(upper);
    g.setColour(palette.background.withAlpha(0.45f));
    g.strokePath(upper, juce::PathStrokeType(std::max(0.6f, size * 0.012f)));
}

void ParrotMotif::drawKnobCap(juce::Graphics& g, juce::Point<float> centre, float radius, float angle,
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

    // The pointer is a feather in the knob's colour, quill at the hub.
    const float length = face * 0.94f;
    const auto place = juce::AffineTransform::rotation(angle).translated(centre);
    const auto tip = juce::Point<float>(0.0f, -length).transformedBy(place);
    g.setGradientFill(juce::ColourGradient(palette.accent.brighter(0.3f), tip.x, tip.y, palette.accent.darker(0.45f),
                                           centre.x, centre.y, false));
    g.fillPath(makeFeather(length, face * 0.36f), place);
    const auto quill = juce::Point<float>(0.0f, -length * 0.12f).transformedBy(place);
    const auto shaftEnd = juce::Point<float>(0.0f, -length * 0.9f).transformedBy(place);
    g.setColour(palette.background.withAlpha(0.5f));
    g.drawLine(quill.x, quill.y, shaftEnd.x, shaftEnd.y, std::max(1.0f, radius * 0.03f));

    g.setColour(palette.background);
    g.fillEllipse(disc(centre, radius * 0.13f));
    g.setColour(palette.text.withAlpha(0.18f));
    g.drawEllipse(disc(centre, radius * 0.13f), 1.0f);
}

void ParrotMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                              const animalaudio::ui::Palette& palette) const
{
    struct Drift
    {
        float x, y, angle, scale;
    };
    static constexpr Drift drifts[] = {
        {0.90f, 0.42f, 0.55f, 0.70f}, {0.07f, 1.02f, -0.85f, 0.55f}, {0.62f, 1.08f, 0.20f, 0.42f}};
    for (const auto& drift : drifts)
    {
        const float length = area.getHeight() * drift.scale;
        const float sway = 0.04f * static_cast<float>(std::sin(timeSeconds * 0.35 + drift.x * 9.0));
        const auto place =
            juce::AffineTransform::rotation(drift.angle + sway)
                .translated(area.getX() + area.getWidth() * drift.x, area.getY() + area.getHeight() * drift.y);
        g.setColour(palette.text.withAlpha(0.03f));
        g.fillPath(makeFeather(length, length * 0.3f), place);

        // Barb gaps, cut in the background colour, give it a feather's texture.
        g.setColour(palette.ambient.withAlpha(0.8f));
        for (int i = 2; i < 9; ++i)
        {
            const float t = static_cast<float>(i) / 10.0f;
            for (const float side : {-1.0f, 1.0f})
            {
                const auto from = juce::Point<float>(0.0f, -length * t).transformedBy(place);
                const auto to = juce::Point<float>(side * length * 0.16f, -length * (t + 0.07f)).transformedBy(place);
                g.drawLine(from.x, from.y, to.x, to.y, 1.0f);
            }
        }
    }
}
} // namespace parrot::ui
