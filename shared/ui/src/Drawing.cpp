#include <animalaudio/ui/Drawing.h>

namespace animalaudio::ui
{
void drawRipples(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, double time)
{
    constexpr int layers = 3;
    for (int layer = 0; layer < layers; ++layer)
    {
        const float yBase = area.getY() + area.getHeight() * (0.2f + 0.3f * static_cast<float>(layer));
        const float amplitude = 2.0f + static_cast<float>(layer);
        const float wavelength = 46.0f + 22.0f * static_cast<float>(layer);
        const float phase =
            static_cast<float>(time) * (0.5f + 0.25f * static_cast<float>(layer)) * (layer % 2 == 0 ? 1.0f : -1.0f);

        juce::Path wave;
        for (float x = 0.0f; x <= area.getWidth(); x += 3.0f)
        {
            const float y = yBase + amplitude * std::sin(x / wavelength * juce::MathConstants<float>::twoPi + phase);
            if (x == 0.0f)
                wave.startNewSubPath(area.getX() + x, y);
            else
                wave.lineTo(area.getX() + x, y);
        }
        g.setColour(colour.withAlpha(0.09f + 0.03f * static_cast<float>(layers - layer)));
        g.strokePath(wave, juce::PathStrokeType(1.4f));
    }
}

juce::Path makeCurvePath(const dsp::ShapeCurve& curve, juce::Rectangle<float> area, int steps)
{
    juce::Path p;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = static_cast<float>(i) / static_cast<float>(steps);
        const float px = area.getX() + x * area.getWidth();
        const float py = area.getBottom() - curve.evaluate(x) * area.getHeight();
        if (i == 0)
            p.startNewSubPath(px, py);
        else
            p.lineTo(px, py);
    }
    return p;
}

void drawArrowGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool right)
{
    const auto box = area.withSizeKeepingCentre(area.getHeight() * 0.7f, area.getHeight());
    juce::Path arrow;
    if (right)
        arrow.addTriangle(box.getX(), box.getY(), box.getRight(), box.getCentreY(), box.getX(), box.getBottom());
    else
        arrow.addTriangle(box.getRight(), box.getY(), box.getX(), box.getCentreY(), box.getRight(), box.getBottom());
    g.setColour(colour);
    g.fillPath(arrow);
}
} // namespace animalaudio::ui
