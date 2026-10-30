#include <animalaudio/dsp/Notes.h>
#include <animalaudio/ui/Panels.h>

namespace animalaudio::ui
{
void drawTitleBar(juce::Graphics& g, juce::Rectangle<float> header, const Theme& theme)
{
    const auto& palette = theme.palette;
    const float height = header.getHeight();
    g.setGradientFill(juce::ColourGradient(palette.panel, 0.0f, header.getY(), palette.background, 0.0f,
                                           header.getY() + height, false));
    g.fillRect(header);

    juce::ColourGradient stripe(palette.accent, header.getX(), 0.0f, palette.detail, header.getRight(), 0.0f, false);
    stripe.addColour(0.55, palette.accentAlt);
    g.setGradientFill(stripe);
    g.fillRect(header.withHeight(3.0f));
    g.setColour(palette.outline);
    g.fillRect(header.withTop(header.getBottom() - 1.0f));

    const float mascot = height - 16.0f;
    theme.motif->drawMascot(g, {header.getX() + 18.0f, header.getY() + 9.0f, mascot, mascot}, palette);

    const float x = header.getX() + 28.0f + mascot;
    g.setColour(palette.text);
    g.setFont(makeFont(26.0f, true, 0.14f));
    g.drawText(theme.name, juce::Rectangle<float>(x, header.getY() + 12.0f, 300.0f, 30.0f),
               juce::Justification::centredLeft, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(10.5f, true, 0.3f));
    g.drawText(theme.tagline, juce::Rectangle<float>(x + 1.0f, header.getY() + 42.0f, 360.0f, 14.0f),
               juce::Justification::centredLeft, false);
}

void drawPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, juce::Colour key,
               const Palette& palette)
{
    constexpr float radius = 8.0f;
    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);
    g.setGradientFill(juce::ColourGradient(palette.panel.brighter(0.05f), 0.0f, area.getY(), palette.panel.darker(0.1f),
                                           0.0f, area.getBottom(), false));
    g.fillRoundedRectangle(area, radius);
    g.setColour(palette.outline);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    g.setColour(palette.text.withAlpha(0.05f));
    g.fillRect(area.getX() + radius, area.getY() + 1.0f, area.getWidth() - 2.0f * radius, 1.0f);

    if (title.isEmpty())
        return;
    const auto row = juce::Rectangle<float>(area.getX() + 18.0f, area.getY() + 14.0f, area.getWidth() - 36.0f, 14.0f);
    g.setColour(key);
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({row.getX() + 3.5f, row.getCentreY()}));
    g.setColour(palette.textDim);
    g.setFont(makeFont(11.0f, true, 0.24f));
    g.drawText(title, row.withTrimmedLeft(15.0f), juce::Justification::centredLeft, false);
}

void drawScreen(juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme, double timeSeconds)
{
    const auto& palette = theme.palette;
    constexpr float radius = 8.0f;
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(area, radius);
        g.reduceClipRegion(clip);
        g.setColour(palette.ambient);
        g.fillRect(area);
        theme.motif->drawAmbient(g, area, timeSeconds, palette);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), 0.0f, area.getY(),
                                               juce::Colours::transparentBlack, 0.0f, area.getY() + 18.0f, false));
        g.fillRect(area.withHeight(18.0f));
    }
    g.setColour(palette.outline.darker(0.25f));
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
}

void drawSemitoneRows(juce::Graphics& g, juce::Rectangle<float> area, const std::function<float(float)>& yOf,
                      int lowest, int highest, float rowHeight, const Palette& palette, bool labels)
{
    for (int n = lowest; n <= highest; ++n)
    {
        const float rowTop = yOf(static_cast<float>(n) + 0.5f);
        if (dsp::isSharp(n))
        {
            g.setColour(palette.text.withAlpha(0.03f));
            g.fillRect(area.getX(), rowTop, area.getWidth(), rowHeight);
        }
        if (((n % 12) + 12) % 12 == 0)
        {
            g.setColour(palette.outline.withAlpha(0.9f));
            g.fillRect(area.getX(), rowTop + rowHeight - 0.5f, area.getWidth(), 1.0f);
            if (labels)
            {
                g.setColour(palette.textDim.withAlpha(0.75f));
                g.setFont(makeFont(9.0f, true, 0.06f));
                g.drawText(juce::String(dsp::noteName(n)),
                           juce::Rectangle<float>(area.getX() + 5.0f, rowTop - 2.0f, 34.0f, rowHeight + 4.0f),
                           juce::Justification::centredLeft, false);
            }
        }
    }
}
} // namespace animalaudio::ui
