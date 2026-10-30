#include "Overview.h"

#include "Glass.h"

#include <algorithm>

namespace monkeydyne::ui
{
Overview::Overview(EditController& c, NoteCanvas& n, const animalaudio::ui::Theme& t)
    : controller(c)
    , canvas(n)
    , theme(t)
{
    controller.addChangeListener(this);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

Overview::~Overview()
{
    controller.removeChangeListener(this);
}

std::pair<double, double> Overview::extent() const
{
    // The takes set the scale (not the view, or dragging the window would rescale the strip under the mouse).
    if (controller.takes().empty())
        return {canvas.getViewStart(), canvas.getViewStart() + std::max(1.0, canvas.getVisibleSeconds())};
    double first = 1.0e12;
    double last = -1.0e12;
    for (const auto& take : controller.takes())
    {
        first = std::min(first, take.startSeconds - 0.5);
        last = std::max(last, take.endSeconds() + 0.5);
    }
    return {first, std::max(last, first + 1.0)};
}

float Overview::xFor(double seconds) const
{
    const auto [first, last] = extent();
    return 8.0f + static_cast<float>((seconds - first) / (last - first)) * (static_cast<float>(getWidth()) - 16.0f);
}

double Overview::secondsFor(float x) const
{
    const auto [first, last] = extent();
    return first + static_cast<double>((x - 8.0f) / (static_cast<float>(getWidth()) - 16.0f)) * (last - first);
}

void Overview::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    const auto bounds = getLocalBounds().toFloat();
    drawGlassWell(g, bounds, 7.0f, palette);

    const float mid = bounds.getCentreY();
    const float half = bounds.getHeight() * 0.36f;
    for (const auto& take : controller.takes())
    {
        if (take.audio == nullptr)
            continue;
        const float x0 = xFor(take.startSeconds);
        const float x1 = xFor(take.endSeconds());
        g.setColour(palette.accent.withAlpha(0.06f));
        g.fillRect(x0, bounds.getY() + 3.0f, x1 - x0, bounds.getHeight() - 6.0f);
        const auto& overview = take.audio->overview;
        const double perPixel = static_cast<double>(overview.size()) / std::max(1.0f, x1 - x0);
        g.setColour(palette.textDim.withAlpha(0.55f));
        for (float x = std::max(x0, 0.0f); x < std::min(x1, bounds.getRight()); x += 1.0f)
        {
            const auto from = static_cast<std::size_t>(std::max(0.0, (x - x0) * perPixel));
            const auto to = std::min(overview.size(), static_cast<std::size_t>((x + 1.0f - x0) * perPixel) + 1);
            float peak = 0.0f;
            for (auto i = from; i < to; ++i)
                peak = std::max(peak, overview[i]);
            const float h = std::max(0.5f, half * std::min(1.0f, peak * 1.6f));
            g.fillRect(x, mid - h, 1.0f, 2.0f * h);
        }
        for (const auto& note : take.notes)
        {
            const bool selected = controller.isSelected({take.id, note.id});
            g.setColour((selected ? palette.accentAlt : palette.accent).withAlpha(selected ? 0.95f : 0.6f));
            const float nx0 = xFor(take.startSeconds + note.outStart / take.rate());
            const float nx1 = xFor(take.startSeconds + note.outEnd / take.rate());
            g.fillRect(nx0, bounds.getBottom() - 6.0f, std::max(1.0f, nx1 - nx0 - 1.0f), 2.0f);
        }
    }

    const float v0 = xFor(canvas.getViewStart());
    const float v1 = xFor(canvas.getViewStart() + canvas.getVisibleSeconds());
    const auto window =
        juce::Rectangle<float>(v0, bounds.getY() + 1.5f, std::max(4.0f, v1 - v0), bounds.getHeight() - 3.0f);
    g.setGradientFill(juce::ColourGradient(palette.text.withAlpha(0.16f), 0.0f, window.getY(),
                                           palette.text.withAlpha(0.04f), 0.0f, window.getBottom(), false));
    g.fillRoundedRectangle(window, 4.0f);
    drawBevel(g, window, 4.0f, palette, true);

    g.setColour(palette.text.withAlpha(0.7f));
    g.fillRect(xFor(playhead) - 0.5f, bounds.getY() + 3.0f, 1.0f, bounds.getHeight() - 6.0f);
}

void Overview::centreOn(float x)
{
    canvas.setViewStart(secondsFor(x) - 0.5 * canvas.getVisibleSeconds());
    repaint();
}

void Overview::mouseDown(const juce::MouseEvent& e)
{
    centreOn(e.position.x);
}

void Overview::mouseDrag(const juce::MouseEvent& e)
{
    centreOn(e.position.x);
}
} // namespace monkeydyne::ui
