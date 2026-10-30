#include "ParrotWidgets.h"

#include <cmath>

namespace parrot::ui
{
ParrotKnob::ParrotKnob(const animalaudio::ui::Theme& themeToUse, juce::Colour arcColour, bool isBipolar)
    : animalaudio::ui::RotaryKnob(themeToUse.palette)
    , theme(themeToUse)
    , colour(arcColour)
    , bipolar(isBipolar)
{
}

void ParrotKnob::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(3.0f);
    const float outer = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float proportion = static_cast<float>(valueToProportionOfLength(getValue()));
    const float angle = kStartAngle + proportion * (kEndAngle - kStartAngle);
    const float middle = 0.5f * (kStartAngle + kEndAngle);
    const float thickness = std::max(3.0f, outer * 0.075f);
    const float ring = outer - thickness * 1.6f;
    const auto stroke = [&](float width)
    { return juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    const auto onRing = [&](float radius, float a)
    { return juce::Point<float>(centre.x + radius * std::sin(a), centre.y - radius * std::cos(a)); };

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, kStartAngle, kEndAngle, true);
    g.setColour(colours.background.darker(0.3f));
    g.strokePath(track, stroke(thickness + 2.0f));
    g.setColour(colours.outline.withAlpha(0.7f));
    g.strokePath(track, stroke(thickness));

    if (bipolar)
    {
        const auto detent = onRing(outer - 1.5f, middle);
        g.setColour(colours.textDim);
        g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre(detent));
    }

    const float from = bipolar ? middle : kStartAngle;
    if (std::abs(angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, std::min(from, angle), std::max(from, angle), true);
        g.setColour(colour.withAlpha(isEnabled() ? 0.2f : 0.08f));
        g.strokePath(value, stroke(thickness * 2.8f));
        g.setColour(isEnabled() ? colour : colour.withSaturation(0.2f));
        g.strokePath(value, stroke(thickness));
    }
    const auto end = onRing(ring, angle);
    g.setColour(colours.text);
    g.fillEllipse(juce::Rectangle<float>(thickness * 0.9f, thickness * 0.9f).withCentre(end));

    auto palette = theme.palette;
    palette.accent = colour;
    theme.motif->drawKnobCap(g, centre, ring - thickness * 2.0f, angle, palette);

    if (hasKeyboardFocus(false))
    {
        g.setColour(colour.withAlpha(0.45f));
        g.drawEllipse(bounds.reduced(0.5f), 1.0f);
    }
}
} // namespace parrot::ui
