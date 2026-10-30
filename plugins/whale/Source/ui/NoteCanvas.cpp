#include "NoteCanvas.h"

#include "dsp/Edits.h"

#include <animalaudio/core/TextFormat.h>
#include <animalaudio/dsp/Notes.h>
#include <animalaudio/ui/Panels.h>

#include <algorithm>
#include <cmath>

namespace whale::ui
{
namespace
{
using animalaudio::core::formatCents;
using animalaudio::core::formatSemitones;
using animalaudio::core::joinWithDots;
using animalaudio::ui::makeFont;

constexpr float kEdgeGrab = 5.0f;
constexpr float kHandleRadius = 4.0f;
constexpr double kMinPixelsPerSecond = 8.0;
constexpr double kMaxPixelsPerSecond = 4000.0;
constexpr float kMinRow = 5.0f;
constexpr float kMaxRow = 40.0f;
constexpr float kLowestMidi = 21.0f;   // A0
constexpr float kHighestMidi = 108.0f; // C8
constexpr float kDragThreshold = 4.0f;
constexpr float kCurvePixels = 80.0f; // vertical drag for the full curve range

juce::String noteText(float midi)
{
    return juce::String(animalaudio::dsp::noteName(juce::roundToInt(midi)));
}

juce::String pitchText(float midi, bool withCents)
{
    const int nearest = juce::roundToInt(midi);
    const int cents = juce::roundToInt((midi - static_cast<float>(nearest)) * 100.0f);
    juce::String text = noteText(midi);
    if (withCents && cents != 0)
        text << " " << formatCents(cents);
    return text;
}

juce::String millis(double seconds)
{
    return juce::String(juce::roundToInt(seconds * 1000.0)) + " ms";
}

juce::String signedMillis(double seconds)
{
    const int ms = juce::roundToInt(seconds * 1000.0);
    return (ms > 0 ? "+" : ms < 0 ? "-" : "") + juce::String(std::abs(ms)) + " ms";
}

juce::String curveText(float curve)
{
    const int amount = juce::roundToInt(std::abs(curve) * 100.0f);
    if (amount == 0)
        return "EVEN";
    return (curve < 0.0f ? "EARLY " : "LATE ") + juce::String(amount);
}

double gridStep(double pixelsPerSecond)
{
    static constexpr double steps[] = {0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0};
    for (const double step : steps)
        if (step * pixelsPerSecond >= 70.0)
            return step;
    return 300.0;
}

juce::String timeText(double seconds, double step)
{
    const bool negative = seconds < -1.0e-9;
    seconds = std::abs(seconds);
    const auto minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    const int decimals = step < 0.1 ? 2 : step < 1.0 ? 1 : 0;
    const juce::String secondsText =
        decimals == 0 ? juce::String(juce::roundToInt(rest)) : juce::String(rest, decimals);
    return (negative ? "-" : "") + juce::String(minutes) + ":" + (rest < 9.995 ? "0" : "") + secondsText;
}

bool touching(const dsp::Note& a, const dsp::Note& b)
{
    return std::abs(a.outEnd - b.outStart) < 1.0;
}
} // namespace

NoteCanvas::NoteCanvas(EditController& c, const animalaudio::ui::Theme& t)
    : controller(c)
    , theme(t)
{
    setOpaque(false);
    setWantsKeyboardFocus(true);
    takesSeen = readyTakes();
    controller.addChangeListener(this);
}

NoteCanvas::~NoteCanvas()
{
    controller.removeChangeListener(this);
}

std::size_t NoteCanvas::readyTakes() const
{
    return static_cast<std::size_t>(std::count_if(controller.takes().begin(), controller.takes().end(),
                                                  [](const TakeInfo& t) { return t.status == TakeStatus::Ready; }));
}

void NoteCanvas::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Only a newly analysed take re-fits the view; edits never move it.
    const auto ready = readyTakes();
    if (ready > takesSeen)
        fitAll();
    takesSeen = ready;
    repaint();
}

void NoteCanvas::setTool(Tool newTool)
{
    if (tool == newTool)
        return;
    tool = newTool;
    setMouseCursor(tool == Tool::Cut ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    repaint();
    if (onToolChanged)
        onToolChanged(tool);
}

void NoteCanvas::setTransport(double hostSeconds, bool isPlaying, std::uint32_t take, double sample)
{
    const bool changed = std::abs(hostSeconds - playhead) > 1.0e-4 || isPlaying != playing || take != auditionTake ||
                         std::abs(sample - auditionSample) > 0.5;
    playhead = hostSeconds;
    playing = isPlaying;
    auditionTake = take;
    auditionSample = sample;
    if (playing && follow && drag.mode == Drag::None)
    {
        const double visible = getVisibleSeconds();
        if (playhead < viewStart || playhead > viewStart + visible * 0.98)
            setViewStart(playhead - visible * 0.1);
    }
    if (changed)
        repaint();
}

// View ---------------------------------------------------------------------------------------------------------------

juce::Rectangle<float> NoteCanvas::noteArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft(kKeysWidth).withTrimmedTop(kRulerHeight);
}

double NoteCanvas::getVisibleSeconds() const
{
    return static_cast<double>(noteArea().getWidth()) / pixelsPerSecond;
}

float NoteCanvas::xForSeconds(double seconds) const noexcept
{
    return kKeysWidth + static_cast<float>((seconds - viewStart) * pixelsPerSecond);
}

double NoteCanvas::secondsForX(float x) const noexcept
{
    return viewStart + static_cast<double>(x - kKeysWidth) / pixelsPerSecond;
}

float NoteCanvas::yForMidi(float midi) const noexcept
{
    return kRulerHeight + (topMidi - midi) * rowHeight;
}

float NoteCanvas::midiForY(float y) const noexcept
{
    return topMidi - (y - kRulerHeight) / rowHeight;
}

float NoteCanvas::takeX(const TakeInfo& take, double outSample) const noexcept
{
    return xForSeconds(take.startSeconds + outSample / take.rate());
}

double NoteCanvas::outSampleAt(const TakeInfo& take, float x) const noexcept
{
    return (secondsForX(x) - take.startSeconds) * take.rate();
}

void NoteCanvas::clampView()
{
    rowHeight = std::clamp(rowHeight, kMinRow, kMaxRow);
    const float visibleRows = noteArea().getHeight() / rowHeight;
    topMidi = std::clamp(topMidi, std::min(kLowestMidi + visibleRows, kHighestMidi), kHighestMidi);
    pixelsPerSecond = std::clamp(pixelsPerSecond, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    viewStart = std::max(viewStart, -2.0);
}

void NoteCanvas::viewChanged()
{
    clampView();
    repaint();
    if (onViewChanged)
        onViewChanged();
}

void NoteCanvas::setViewStart(double seconds)
{
    viewStart = seconds;
    viewChanged();
}

void NoteCanvas::zoomTime(double factor, float anchorX)
{
    const double anchor = secondsForX(anchorX);
    pixelsPerSecond = std::clamp(pixelsPerSecond * factor, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    viewStart = anchor - static_cast<double>(anchorX - kKeysWidth) / pixelsPerSecond;
    viewChanged();
}

void NoteCanvas::zoomPitch(float factor, float anchorY)
{
    const float anchor = midiForY(anchorY);
    rowHeight = std::clamp(rowHeight * factor, kMinRow, kMaxRow);
    topMidi = anchor + (anchorY - kRulerHeight) / rowHeight;
    viewChanged();
}

void NoteCanvas::fitAll()
{
    const auto area = noteArea();
    if (area.getWidth() < 10.0f || area.getHeight() < 10.0f)
        return;
    double first = 1.0e12;
    double last = -1.0e12;
    float low = 200.0f;
    float high = -1.0f;
    for (const auto& take : controller.takes())
    {
        first = std::min(first, take.startSeconds);
        last = std::max(last, take.endSeconds());
        if (const auto* plan = controller.plan(take.id))
            for (const auto& note : plan->notes())
            {
                low = std::min(low, plan->targetCentre(note));
                high = std::max(high, plan->targetCentre(note));
            }
    }
    if (last <= first)
        return;
    const double margin = std::max(0.2, 0.03 * (last - first));
    pixelsPerSecond = static_cast<double>(area.getWidth()) / (last - first + 2.0 * margin);
    viewStart = first - margin;
    if (high >= low)
    {
        rowHeight = std::clamp(area.getHeight() / (high - low + 9.0f), 8.0f, 24.0f);
        topMidi = 0.5f * (low + high) + 0.5f * area.getHeight() / rowHeight;
    }
    viewChanged();
}

// Geometry -----------------------------------------------------------------------------------------------------------

juce::Rectangle<float> NoteCanvas::blobBounds(const TakeInfo& take, const dsp::Note& note) const
{
    const auto* plan = controller.plan(take.id);
    if (plan == nullptr)
        return {};
    const float x0 = takeX(take, note.outStart);
    const float x1 = takeX(take, note.outEnd);
    const float y = yForMidi(plan->targetCentre(note));
    return {x0, y - blobHalfHeight(), x1 - x0, 2.0f * blobHalfHeight()};
}

juce::Rectangle<float> NoteCanvas::noteBounds(const NoteRef& ref) const
{
    const auto* take = controller.take(ref.take);
    const auto* note = controller.note(ref);
    return take != nullptr && note != nullptr ? blobBounds(*take, *note) : juce::Rectangle<float>{};
}

juce::Point<float> NoteCanvas::glideHandle(const NoteRef& ref) const
{
    const auto* take = controller.take(ref.take);
    const auto* plan = controller.plan(ref.take);
    const int index = controller.indexOf(ref);
    if (take == nullptr || plan == nullptr || index < 0 || index + 1 >= static_cast<int>(take->notes.size()))
        return {};
    const auto& note = take->notes[static_cast<std::size_t>(index)];
    const auto& next = take->notes[static_cast<std::size_t>(index + 1)];
    float midi = plan->outputMidiAt(note.outEnd);
    if (midi < 0.0f)
        midi = 0.5f * (plan->targetCentre(note) + plan->targetCentre(next));
    return {takeX(*take, note.outEnd), yForMidi(midi)};
}

juce::Path NoteCanvas::blobPath(const TakeInfo& take, const dsp::EditPlan& plan, const dsp::Note& note,
                                float centreY) const
{
    const auto& analysis = *take.analysis;
    const float x0 = takeX(take, note.outStart);
    const float x1 = takeX(take, note.outEnd);
    const float width = std::max(x1 - x0, 1.0f);
    const int steps = std::clamp(static_cast<int>(width / 3.0f), 2, 600);
    const auto stepCount = static_cast<float>(steps);
    const float peak = controller.peakDb(take.id);
    const float floor = peak - 48.0f;
    const float maxH = blobHalfHeight();
    const float minH = std::max(1.2f, rowHeight * 0.07f);
    const float round = std::min(maxH, width * 0.5f);

    std::vector<float> half(static_cast<std::size_t>(steps + 1));
    for (int k = 0; k <= steps; ++k)
    {
        const double out = note.outStart + note.outLength() * static_cast<double>(k) / steps;
        const float db = analysis.levelDbAt(plan.sourceOf(out)) + note.gainDb;
        const float a = std::clamp((db - floor) / (peak - floor), 0.0f, 1.0f);
        float h = minH + (maxH - minH) * std::pow(a, 1.4f);
        const float edge = std::min(static_cast<float>(k), static_cast<float>(steps - k)) * width / stepCount;
        h *= std::sqrt(std::min(1.0f, edge / round + 0.08f));
        half[static_cast<std::size_t>(k)] = h;
    }

    juce::Path path;
    for (int k = 0; k <= steps; ++k)
    {
        const float x = x0 + width * static_cast<float>(k) / stepCount;
        const float y = centreY - half[static_cast<std::size_t>(k)];
        if (k == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    for (int k = steps; k >= 0; --k)
        path.lineTo(x0 + width * static_cast<float>(k) / stepCount, centreY + half[static_cast<std::size_t>(k)]);
    path.closeSubPath();
    return path;
}

NoteCanvas::Hit NoteCanvas::findHit(juce::Point<float> p) const
{
    Hit hit;
    if (!noteArea().contains(p))
        return hit;

    for (const auto& take : controller.takes())
    {
        if (controller.plan(take.id) == nullptr)
            continue;
        for (std::size_t i = 0; i + 1 < take.notes.size(); ++i)
            if (take.notes[i].linkedToNext && tool == Tool::Edit)
            {
                const NoteRef ref{take.id, take.notes[i].id};
                if (glideHandle(ref).getDistanceFrom(p) <= kHandleRadius + 3.0f)
                    return {Hit::Kind::Glide, ref};
            }
    }

    const auto separationAt = [&]() -> Hit
    {
        for (const auto& take : controller.takes())
            for (std::size_t i = 0; i + 1 < take.notes.size(); ++i)
            {
                const auto& a = take.notes[i];
                const auto& b = take.notes[i + 1];
                if (!touching(a, b))
                    continue;
                const auto ra = blobBounds(take, a);
                const auto rb = blobBounds(take, b);
                const float top = std::min(ra.getY(), rb.getY()) - 6.0f;
                const float bottom = std::max(ra.getBottom(), rb.getBottom()) + 6.0f;
                if (std::abs(p.x - ra.getRight()) <= 4.0f && p.y >= top && p.y <= bottom)
                    return {Hit::Kind::Separation, {take.id, a.id}};
            }
        return {};
    };
    if (tool == Tool::Cut)
        if (const auto separation = separationAt(); separation.kind != Hit::Kind::None)
            return separation;

    float bestDistance = 1.0e9f;
    for (const auto& take : controller.takes())
    {
        if (controller.plan(take.id) == nullptr)
            continue;
        for (const auto& note : take.notes)
        {
            const auto b = blobBounds(take, note);
            if (p.x < b.getX() - kEdgeGrab || p.x > b.getRight() + kEdgeGrab)
                continue;
            const float dy = std::abs(p.y - b.getCentreY());
            if (dy > std::max(b.getHeight() * 0.5f, rowHeight * 0.6f) + 2.0f || dy >= bestDistance)
                continue;
            const float grab = std::min(kEdgeGrab, b.getWidth() * 0.25f);
            Hit::Kind kind = Hit::Kind::None;
            if (tool == Tool::Edit && std::abs(p.x - b.getX()) <= grab)
                kind = Hit::Kind::StartEdge;
            else if (tool == Tool::Edit && std::abs(p.x - b.getRight()) <= grab)
                kind = Hit::Kind::EndEdge;
            else if (p.x >= b.getX() && p.x <= b.getRight())
                kind = Hit::Kind::Body;
            if (kind == Hit::Kind::None)
                continue;
            bestDistance = dy;
            hit = {kind, {take.id, note.id}};
        }
    }
    if (hit.kind == Hit::Kind::None)
        hit = separationAt();
    return hit;
}

// Painting -----------------------------------------------------------------------------------------------------------

void NoteCanvas::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    const auto bounds = getLocalBounds().toFloat();
    animalaudio::ui::drawScreen(g, bounds, theme, ambientTime);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, 8.0f);
        g.reduceClipRegion(clip);

        const auto area = noteArea();
        {
            juce::Graphics::ScopedSaveState inner(g);
            g.reduceClipRegion(area.toNearestInt());
            paintGrid(g, area);
            if (controller.takes().empty())
                paintEmpty(g);
            for (const auto& take : controller.takes())
                paintTake(g, take);

            if (drag.mode == Drag::Band)
            {
                g.setColour(palette.accentAlt.withAlpha(0.1f));
                g.fillRect(drag.band);
                g.setColour(palette.accentAlt.withAlpha(0.7f));
                g.drawRect(drag.band, 1.0f);
            }
            if (tool == Tool::Cut && hover.kind == Hit::Kind::Body && hoverPoint.x >= 0.0f)
            {
                const auto b = noteBounds(hover.ref).expanded(0.0f, 6.0f);
                juce::Path cut;
                cut.startNewSubPath(hoverPoint.x, b.getY());
                cut.lineTo(hoverPoint.x, b.getBottom());
                juce::Path dashed;
                const float dashes[] = {3.0f, 3.0f};
                juce::PathStrokeType(1.2f).createDashedStroke(dashed, cut, dashes, 2);
                g.setColour(palette.accentAlt);
                g.fillPath(dashed);
            }

            const float hostX = xForSeconds(playhead);
            g.setColour(palette.text.withAlpha(playing ? 0.85f : 0.3f));
            g.fillRect(hostX - 0.6f, area.getY(), 1.2f, area.getHeight());
            if (auditionSample >= 0.0)
                if (const auto* take = controller.take(auditionTake))
                {
                    const float x = takeX(*take, auditionSample);
                    g.setColour(palette.accentAlt.withAlpha(0.9f));
                    g.fillRect(x - 0.6f, area.getY(), 1.2f, area.getHeight());
                }
            paintStatus(g);
        }
        paintRuler(g);
        paintKeys(g);
        g.setColour(palette.background);
        g.fillRect(0.0f, 0.0f, kKeysWidth, kRulerHeight);
    }
    g.setColour(palette.outline.darker(0.25f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

void NoteCanvas::paintGrid(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    const int lowest = static_cast<int>(std::floor(midiForY(area.getBottom()))) - 1;
    const int highest = static_cast<int>(std::ceil(midiForY(area.getY()))) + 1;
    const auto scale = controller.scale();
    if (scale.mode != dsp::Scale::Mode::Chromatic)
        for (int n = lowest; n <= highest; ++n)
            if (scale.contains(n))
            {
                const bool tonic = ((n - scale.key) % 12 + 12) % 12 == 0;
                g.setColour(palette.accent.withAlpha(tonic ? 0.07f : 0.035f));
                g.fillRect(area.getX(), yForMidi(static_cast<float>(n) + 0.5f), area.getWidth(), rowHeight);
            }
    animalaudio::ui::drawSemitoneRows(
        g, area, [this](float m) { return yForMidi(m); }, lowest, highest, rowHeight, palette, false);

    const double step = gridStep(pixelsPerSecond);
    const double minor = step / 4.0;
    const bool showMinor = minor * pixelsPerSecond >= 14.0;
    const auto first = static_cast<long long>(std::floor(viewStart / minor));
    const auto last = static_cast<long long>(std::ceil((viewStart + getVisibleSeconds()) / minor));
    for (long long k = first; k <= last; ++k)
    {
        const bool major = k % 4 == 0;
        if (!major && !showMinor)
            continue;
        const float x = xForSeconds(static_cast<double>(k) * minor);
        g.setColour(palette.outline.withAlpha(major ? 0.55f : 0.22f));
        g.fillRect(x, area.getY(), 1.0f, area.getHeight());
    }
}

void NoteCanvas::paintTake(juce::Graphics& g, const TakeInfo& take) const
{
    const auto& palette = theme.palette;
    const auto area = noteArea();
    const float x0 = xForSeconds(take.startSeconds);
    const float x1 = xForSeconds(take.endSeconds());
    if (x1 < area.getX() || x0 > area.getRight())
        return;
    g.setColour(palette.accent.withAlpha(0.025f));
    g.fillRect(x0, area.getY(), x1 - x0, area.getHeight());
    g.setColour(palette.accent.withAlpha(0.16f));
    g.fillRect(x0, area.getY(), 1.0f, area.getHeight());
    g.fillRect(x1 - 1.0f, area.getY(), 1.0f, area.getHeight());

    const auto* plan = controller.plan(take.id);
    if (take.status != TakeStatus::Ready || plan == nullptr)
        paintAnalysing(g, take);
    else
        paintNotes(g, take, *plan);
}

void NoteCanvas::paintAnalysing(juce::Graphics& g, const TakeInfo& take) const
{
    const auto& palette = theme.palette;
    const auto area = noteArea();
    if (take.audio == nullptr)
        return;
    const auto& overview = take.audio->overview;
    const float x0 = std::max(area.getX(), xForSeconds(take.startSeconds));
    const float x1 = std::min(area.getRight(), xForSeconds(take.endSeconds()));
    const float mid = area.getCentreY();
    const float height = area.getHeight() * 0.3f;
    g.setColour(palette.textDim.withAlpha(0.3f));
    for (float x = x0; x < x1; x += 2.0f)
    {
        const auto from = static_cast<std::int64_t>(outSampleAt(take, x)) / TakeAudio::kOverviewStep;
        const auto to = static_cast<std::int64_t>(outSampleAt(take, x + 2.0f)) / TakeAudio::kOverviewStep;
        float peak = 0.0f;
        for (auto i = std::max<std::int64_t>(from, 0); i <= to && i < static_cast<std::int64_t>(overview.size()); ++i)
            peak = std::max(peak, overview[static_cast<std::size_t>(i)]);
        const float h = std::max(0.5f, height * std::min(1.0f, peak * 1.6f));
        g.fillRect(x, mid - h, 1.4f, 2.0f * h);
    }

    const auto label =
        juce::Rectangle<float>(x0 + 12.0f, mid - height - 34.0f, std::max(60.0f, x1 - x0 - 24.0f), 14.0f);
    g.setColour(palette.text);
    g.setFont(makeFont(11.0f, true, 0.2f));
    g.drawText("LISTENING FOR NOTES  " + juce::String(juce::roundToInt(take.progress * 100.0f)) + " %", label,
               juce::Justification::centredLeft, false);
    const auto bar =
        juce::Rectangle<float>(label.getX(), label.getBottom() + 5.0f, std::min(label.getWidth(), 220.0f), 3.0f);
    g.setColour(palette.outline);
    g.fillRoundedRectangle(bar, 1.5f);
    g.setColour(palette.accent);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * std::clamp(take.progress, 0.0f, 1.0f)), 1.5f);
}

void NoteCanvas::paintNotes(juce::Graphics& g, const TakeInfo& take, const dsp::EditPlan& plan) const
{
    const auto& palette = theme.palette;
    const auto area = noteArea();
    const auto& notes = take.notes;
    const auto& analysis = *take.analysis;
    const float left = area.getX() - 30.0f;
    const float right = area.getRight() + 30.0f;
    const float maxH = blobHalfHeight();

    const auto visible = [&](const dsp::Note& n)
    { return takeX(take, n.outEnd) >= left && takeX(take, n.outStart) <= right; };
    const auto hot = [&](const NoteRef& ref)
    {
        return hover.ref == ref && (hover.kind == Hit::Kind::Body || hover.kind == Hit::Kind::StartEdge ||
                                    hover.kind == Hit::Kind::EndEdge);
    };

    // Blobs.
    for (const auto& n : notes)
    {
        if (!visible(n))
            continue;
        const NoteRef ref{take.id, n.id};
        const bool selected = controller.isSelected(ref);
        const float cy = yForMidi(plan.targetCentre(n));
        const auto path = blobPath(take, plan, n, cy);
        const auto base = selected ? palette.accentAlt : palette.accent;
        if (n.muted)
        {
            juce::Path dashed;
            const float dashes[] = {2.5f, 2.5f};
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, path, dashes, 2);
            g.setColour((selected ? palette.accentAlt : palette.textDim).withAlpha(0.6f));
            g.fillPath(dashed);
            continue;
        }
        const float lift = hot(ref) ? 0.12f : 0.0f;
        g.setGradientFill(juce::ColourGradient(base.brighter(0.3f).withAlpha(0.82f + lift), 0.0f, cy - maxH,
                                               base.darker(0.45f).withAlpha(0.74f + lift), 0.0f, cy + maxH, false));
        g.fillPath(path);
        g.setColour(base.brighter(0.6f).withAlpha(selected ? 0.95f : 0.4f + lift * 2.0f));
        g.strokePath(path, juce::PathStrokeType(selected ? 1.4f : 1.0f));
    }

    // Separations: solid between separate notes, dotted where notes are linked.
    for (std::size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& a = notes[i];
        const auto& b = notes[i + 1];
        if (!touching(a, b) || !visible(a))
            continue;
        const float x = takeX(take, a.outEnd);
        const float ya = yForMidi(plan.targetCentre(a));
        const float yb = yForMidi(plan.targetCentre(b));
        const float top = std::min(ya, yb) - maxH - 3.0f;
        const float bottom = std::max(ya, yb) + maxH + 3.0f;
        const bool hotSeparation = hover.kind == Hit::Kind::Separation && hover.ref == NoteRef{take.id, a.id};
        g.setColour(palette.text.withAlpha(hotSeparation ? 0.9f : 0.45f));
        if (a.linkedToNext)
        {
            for (float y = top; y < bottom; y += 4.0f)
                g.fillRect(x - 0.5f, y, 1.0f, 2.0f);
        }
        else
        {
            g.fillRect(x - 0.75f, top, 1.5f, bottom - top);
        }
    }

    // Pitch: as recorded (faint, dashed) under edited notes, then as it will sound.
    {
        juce::Path ghost;
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (!visible(n) || (n.isUnedited() && !(i > 0 && notes[i - 1].shapesTransition())))
                continue;
            bool open = false;
            const float x0 = std::max(left, takeX(take, n.outStart));
            const float x1 = std::min(right, takeX(take, n.outEnd));
            for (float x = x0; x <= x1; x += 2.0f)
            {
                const float midi = analysis.midiAt(plan.sourceOf(outSampleAt(take, x)));
                if (midi < 0.0f)
                {
                    open = false;
                    continue;
                }
                const float y = yForMidi(midi);
                if (!open)
                    ghost.startNewSubPath(x, y);
                else
                    ghost.lineTo(x, y);
                open = true;
            }
        }
        juce::Path dashed;
        const float dashes[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, ghost, dashes, 2);
        g.setColour(palette.text.withAlpha(0.28f));
        g.fillPath(dashed);
    }
    {
        juce::Path curve;
        bool open = false;
        const float x0 = std::max(left, takeX(take, 0.0));
        const float x1 = std::min(right, takeX(take, static_cast<double>(take.length())));
        for (float x = x0; x <= x1; x += 2.0f)
        {
            const double out = outSampleAt(take, x);
            const int k = plan.noteAt(out);
            const float midi = k >= 0 && !notes[static_cast<std::size_t>(k)].muted ? plan.outputMidiAt(out) : -1.0f;
            if (midi < 0.0f)
            {
                open = false;
                continue;
            }
            const float y = yForMidi(midi);
            if (!open)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
            open = true;
        }
        g.setColour(palette.background.withAlpha(0.5f));
        g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(palette.text.withAlpha(0.9f));
        g.strokePath(curve, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Glide handles between linked notes: shown on selected or hovered notes, and always while dragged.
    for (std::size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& a = notes[i];
        const auto& b = notes[i + 1];
        if (!a.linkedToNext || !visible(a))
            continue;
        const NoteRef ref{take.id, a.id};
        const NoteRef nextRef{take.id, b.id};
        const bool dragged = drag.mode == Drag::Glide && drag.ref == ref;
        const bool hovered = hover.kind == Hit::Kind::Glide && hover.ref == ref;
        const bool active = dragged || hovered || controller.isSelected(ref) || controller.isSelected(nextRef) ||
                            hot(ref) || hot(nextRef);
        if (!active && !a.shaped)
            continue;
        const auto c = glideHandle(ref);
        const double span = static_cast<double>(a.glide) * take.rate();
        const float gx0 = takeX(take, a.outEnd - 0.5 * span);
        const float gx1 = takeX(take, a.outEnd + 0.5 * span);
        if (dragged || hovered)
        {
            g.setColour(palette.accentAlt.withAlpha(0.25f));
            g.fillRect(gx0, c.y - 1.0f, gx1 - gx0, 2.0f);
            g.fillRect(gx0, c.y - 5.0f, 1.0f, 10.0f);
            g.fillRect(gx1 - 1.0f, c.y - 5.0f, 1.0f, 10.0f);
        }
        // Filled: a shaped glide; hollow: the slide as sung. Shaped ones stay visible, dimmer, when idle.
        const float r = dragged || hovered ? kHandleRadius + 1.5f : kHandleRadius;
        juce::Path diamond;
        diamond.addQuadrilateral(c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
        g.setColour(palette.background);
        g.strokePath(diamond, juce::PathStrokeType(3.0f));
        const auto colour = palette.accentAlt.withAlpha(active ? 1.0f : 0.55f);
        if (a.shaped)
        {
            g.setColour(colour);
            g.fillPath(diamond);
        }
        else
        {
            g.setColour(palette.background);
            g.fillPath(diamond);
            g.setColour(colour);
            g.strokePath(diamond, juce::PathStrokeType(1.5f));
        }
    }

    // Labels: the note each one will sing.
    if (rowHeight >= 8.0f)
    {
        g.setFont(makeFont(9.5f, true, 0.04f));
        for (const auto& n : notes)
        {
            const float x0 = takeX(take, n.outStart);
            const float x1 = takeX(take, n.outEnd);
            if (!visible(n) || x1 - x0 < 26.0f)
                continue;
            const NoteRef ref{take.id, n.id};
            const float target = plan.targetCentre(n);
            const float y = yForMidi(target) - maxH - 13.0f;
            g.setColour(controller.isSelected(ref) ? palette.accentAlt : palette.textDim.withAlpha(0.85f));
            g.drawText(pitchText(target, rowHeight >= 12.0f && x1 - x0 >= 52.0f),
                       juce::Rectangle<float>(x0 + 2.0f, y, std::max(x1 - x0, 70.0f), 11.0f),
                       juce::Justification::centredLeft, false);
        }
    }
}

void NoteCanvas::paintRuler(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    const auto ruler =
        juce::Rectangle<float>(kKeysWidth, 0.0f, static_cast<float>(getWidth()) - kKeysWidth, kRulerHeight);
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(ruler.toNearestInt());
    g.setGradientFill(juce::ColourGradient(palette.panelRaised, 0.0f, 0.0f, palette.panel, 0.0f, kRulerHeight, false));
    g.fillRect(ruler);
    g.setColour(palette.outline);
    g.fillRect(ruler.getX(), ruler.getBottom() - 1.0f, ruler.getWidth(), 1.0f);

    for (const auto& take : controller.takes())
    {
        const float x0 = xForSeconds(take.startSeconds);
        const float x1 = xForSeconds(take.endSeconds());
        g.setColour(take.status == TakeStatus::Ready ? palette.accent.withAlpha(0.6f)
                                                     : palette.textDim.withAlpha(0.5f));
        g.fillRect(x0, ruler.getBottom() - 4.0f, x1 - x0, 3.0f);
    }

    const double step = gridStep(pixelsPerSecond);
    const auto first = static_cast<long long>(std::floor(viewStart / step));
    const auto last = static_cast<long long>(std::ceil((viewStart + getVisibleSeconds()) / step));
    g.setFont(makeFont(9.5f, true, 0.06f));
    for (long long k = first; k <= last; ++k)
    {
        const double t = static_cast<double>(k) * step;
        const float x = xForSeconds(t);
        g.setColour(palette.outline.brighter(0.4f));
        g.fillRect(x, ruler.getBottom() - 8.0f, 1.0f, 7.0f);
        g.setColour(palette.textDim);
        g.drawText(timeText(t, step), juce::Rectangle<float>(x + 4.0f, 3.0f, 60.0f, 12.0f),
                   juce::Justification::centredLeft, false);
    }

    const float hostX = xForSeconds(playhead);
    juce::Path marker;
    marker.addTriangle(hostX - 5.0f, ruler.getBottom() - 8.0f, hostX + 5.0f, ruler.getBottom() - 8.0f, hostX,
                       ruler.getBottom() - 1.0f);
    g.setColour(palette.text.withAlpha(playing ? 0.95f : 0.45f));
    g.fillPath(marker);
}

void NoteCanvas::paintKeys(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    const auto keys =
        juce::Rectangle<float>(0.0f, kRulerHeight, kKeysWidth, static_cast<float>(getHeight()) - kRulerHeight);
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(keys.toNearestInt());
    g.setColour(palette.panel);
    g.fillRect(keys);

    int highlighted = -1000;
    const NoteRef focus = drag.mode != Drag::None ? drag.ref : hover.ref;
    if (focus.take != 0)
        if (const auto* note = controller.note(focus))
            if (const auto* plan = controller.plan(focus.take))
                highlighted = juce::roundToInt(plan->targetCentre(*note));

    const auto scale = controller.scale();
    const int lowest = static_cast<int>(std::floor(midiForY(keys.getBottom()))) - 1;
    const int highest = static_cast<int>(std::ceil(midiForY(keys.getY()))) + 1;
    for (int n = lowest; n <= highest; ++n)
    {
        const float top = yForMidi(static_cast<float>(n) + 0.5f);
        const bool sharp = animalaudio::dsp::isSharp(n);
        if (!sharp)
        {
            g.setColour(palette.text.withAlpha(0.07f));
            g.fillRect(keys.getX(), top, keys.getWidth(), rowHeight);
            g.setColour(palette.background.withAlpha(0.8f));
            g.fillRect(keys.getX(), top + rowHeight - 0.5f, keys.getWidth(), 1.0f);
        }
        else
        {
            g.setColour(palette.background.darker(0.3f));
            g.fillRoundedRectangle(keys.getX(), top + 0.5f, keys.getWidth() * 0.6f, rowHeight - 1.0f, 2.0f);
        }
        if (n == highlighted)
        {
            g.setColour(palette.accentAlt.withAlpha(0.65f));
            g.fillRect(keys.getX(), top + 0.5f, keys.getWidth() * (sharp ? 0.6f : 1.0f), rowHeight - 1.0f);
        }
        if (scale.mode != dsp::Scale::Mode::Chromatic && scale.contains(n))
        {
            g.setColour(palette.accent.withAlpha(0.8f));
            g.fillEllipse(keys.getRight() - 8.0f, top + rowHeight * 0.5f - 1.5f, 3.0f, 3.0f);
        }
        if (((n % 12) + 12) % 12 == 0 && rowHeight >= 6.0f)
        {
            g.setColour(palette.textDim);
            g.setFont(makeFont(std::min(9.5f, rowHeight), true, 0.04f));
            g.drawText(
                juce::String(animalaudio::dsp::noteName(n)),
                juce::Rectangle<float>(keys.getX() + 2.0f, top - 1.0f, keys.getWidth() - 12.0f, rowHeight + 2.0f),
                juce::Justification::centredRight, false);
        }
    }
    g.setColour(palette.outline);
    g.fillRect(keys.getRight() - 1.0f, keys.getY(), 1.0f, keys.getHeight());
}

void NoteCanvas::paintEmpty(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    const auto area = noteArea();
    const auto centre = area.getCentre();
    g.beginTransparencyLayer(0.55f);
    theme.motif->drawMascot(g, juce::Rectangle<float>(110.0f, 110.0f).withCentre(centre.translated(0.0f, -46.0f)),
                            palette);
    g.endTransparencyLayer();
    g.setColour(palette.text);
    g.setFont(makeFont(16.0f, true, 0.18f));
    g.drawText("TRANSFER A VOCAL", juce::Rectangle<float>(area.getX(), centre.y + 22.0f, area.getWidth(), 22.0f),
               juce::Justification::centred, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(11.5f, false));
    g.drawText("Press TRANSFER, then play the part in your host. Whale finds every note, word and phrase.",
               juce::Rectangle<float>(area.getX(), centre.y + 48.0f, area.getWidth(), 16.0f),
               juce::Justification::centred, false);
    g.drawText("Drag notes up and down, stretch them, cut them, and shape how they glide together.",
               juce::Rectangle<float>(area.getX(), centre.y + 66.0f, area.getWidth(), 16.0f),
               juce::Justification::centred, false);
}

juce::String NoteCanvas::describe(const NoteRef& ref) const
{
    const auto* note = controller.note(ref);
    const auto* plan = controller.plan(ref.take);
    const auto* take = controller.take(ref.take);
    if (note == nullptr || plan == nullptr || take == nullptr)
        return {};
    const float target = plan->targetCentre(*note);
    juce::StringArray parts;
    parts.add(pitchText(target, true));
    parts.add(juce::String(note->outLength() / take->rate(), 2) + " s");
    if (std::abs(note->pitch) > 0.005f)
        parts.add("MOVED " + formatSemitones(note->pitch).toUpperCase());
    if (note->snap > 0.005f)
        parts.add("SNAP " + juce::String(juce::roundToInt(note->snap * 100.0f)) + " %");
    if (note->isStretched())
        parts.add(juce::String(note->outLength() / std::max(1.0, note->srcLength()), 2) + "x");
    if (note->muted)
        parts.add("MUTED");
    parts.add("WORD " + juce::String(note->word + 1));
    parts.add("PHRASE " + juce::String(note->phrase + 1));
    return parts.joinIntoString(juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")));
}

void NoteCanvas::paintStatus(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    juce::String text = drag.readout;
    if (text.isEmpty() && hover.kind == Hit::Kind::Glide)
        if (const auto* note = controller.note(hover.ref))
            text = note->shaped ? joinWithDots({"GLIDE " + millis(note->glide), "CURVE " + curveText(note->curve),
                                                "DOUBLE-CLICK: AS SUNG"})
                                : joinWithDots({"GLIDE AS SUNG", "DRAG SIDEWAYS: LENGTH, UP / DOWN: CURVE"});
    if (text.isEmpty() && hover.kind == Hit::Kind::Separation)
        if (const auto* note = controller.note(hover.ref))
            text = joinWithDots({note->linkedToNext ? "LINKED" : "SEPARATE",
                                 note->linkedToNext ? "ALT-CLICK TO SEPARATE" : "ALT-CLICK TO LINK",
                                 "DOUBLE-CLICK TO MERGE"});
    if (text.isEmpty() && hover.kind != Hit::Kind::None)
        text = describe(hover.ref);
    if (text.isEmpty() && tool == Tool::Cut && !controller.takes().empty())
        text = "CUT: CLICK A NOTE TO SPLIT IT";
    if (text.isEmpty())
        return;
    const auto area = noteArea();
    const auto font = makeFont(10.0f, true, 0.1f);
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 18.0f;
    const auto box = juce::Rectangle<float>(area.getX() + 8.0f, area.getBottom() - 26.0f, width, 18.0f);
    g.setColour(palette.background.withAlpha(0.82f));
    g.fillRoundedRectangle(box, 9.0f);
    g.setColour(palette.outline);
    g.drawRoundedRectangle(box.reduced(0.5f), 9.0f, 1.0f);
    g.setColour(palette.text);
    g.setFont(font);
    g.drawText(text, box, juce::Justification::centred, false);
}

void NoteCanvas::resized()
{
    clampView();
}

// Mouse --------------------------------------------------------------------------------------------------------------

void NoteCanvas::mouseMove(const juce::MouseEvent& e)
{
    const auto hit = findHit(e.position);
    const bool changed = hit.kind != hover.kind || hit.ref != hover.ref || tool == Tool::Cut;
    hover = hit;
    hoverPoint = e.position;
    using Kind = Hit::Kind;
    juce::MouseCursor cursor = tool == Tool::Cut ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor;
    if (hit.kind == Kind::StartEdge || hit.kind == Kind::EndEdge)
        cursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (hit.kind == Kind::Glide)
        cursor = juce::MouseCursor::UpDownLeftRightResizeCursor;
    else if (hit.kind == Kind::Separation)
        cursor = juce::MouseCursor::PointingHandCursor;
    else if (hit.kind == Kind::Body && tool == Tool::Edit)
        cursor = juce::MouseCursor::DraggingHandCursor;
    setMouseCursor(cursor);
    if (changed)
        repaint();
}

void NoteCanvas::mouseExit(const juce::MouseEvent&)
{
    hover = {};
    hoverPoint = {-1.0f, -1.0f};
    repaint();
}

void NoteCanvas::audition(const NoteRef& ref)
{
    if (playing || !onAudition)
        return;
    const auto* note = controller.note(ref);
    const auto* take = controller.take(ref.take);
    if (note == nullptr || take == nullptr)
        return;
    onAudition(ref.take, note->outStart, std::min(note->outLength() / take->rate() + 0.12, 4.0));
}

void NoteCanvas::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = {};
    drag.start = e.position;
    drag.viewStartAtDown = viewStart;
    if (e.position.y < kRulerHeight && e.position.x >= kKeysWidth)
    {
        drag.mode = Drag::Pan;
        return;
    }
    if (!noteArea().contains(e.position))
        return;

    const auto hit = findHit(e.position);
    drag.origin = controller.takes();
    drag.ref = hit.ref;

    if (e.mods.isPopupMenu())
    {
        if (hit.kind != Hit::Kind::None && hit.kind != Hit::Kind::Separation && hit.kind != Hit::Kind::Glide &&
            !controller.isSelected(hit.ref))
        {
            const auto group = controller.expand(hit.ref, granularity);
            controller.setSelection({group.begin(), group.end()});
        }
        drag.mode = Drag::None;
        showMenu(hit, e.position);
        return;
    }

    if (tool == Tool::Cut)
    {
        if (hit.kind == Hit::Kind::Separation && e.mods.isAltDown())
            toggleLink(hit.ref);
        else if (hit.kind == Hit::Kind::Body)
            splitAt(hit.ref, secondsForX(e.position.x));
        return;
    }

    switch (hit.kind)
    {
    case Hit::Kind::Glide:
        drag.mode = Drag::Glide;
        controller.beginGesture();
        break;
    case Hit::Kind::StartEdge:
    case Hit::Kind::EndEdge:
        if (!controller.isSelected(hit.ref))
            controller.setSelection({hit.ref});
        drag.mode = Drag::Stretch;
        drag.startEdge = hit.kind == Hit::Kind::StartEdge;
        controller.beginGesture();
        break;
    case Hit::Kind::Body:
    {
        const auto group = controller.expand(hit.ref, granularity);
        auto selection = controller.selection();
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            const bool all =
                std::all_of(group.begin(), group.end(), [&](const NoteRef& r) { return selection.count(r) > 0; });
            for (const auto& r : group)
                if (all)
                    selection.erase(r);
                else
                    selection.insert(r);
        }
        else if (!controller.isSelected(hit.ref))
        {
            selection = {group.begin(), group.end()};
        }
        else
        {
            drag.clickedSelected = true;
        }
        controller.setSelection(selection);
        if (controller.isSelected(hit.ref))
        {
            drag.mode = Drag::Pending;
            controller.beginGesture();
            audition(hit.ref);
        }
        break;
    }
    case Hit::Kind::Separation:
    case Hit::Kind::None:
        drag.mode = Drag::Band;
        drag.bandBase = e.mods.isShiftDown() || e.mods.isCommandDown() ? controller.selection() : std::set<NoteRef>{};
        if (drag.bandBase.empty())
            controller.clearSelection();
        break;
    }
    repaint();
}

void NoteCanvas::mouseDrag(const juce::MouseEvent& e)
{
    const auto delta = e.position - drag.start;
    switch (drag.mode)
    {
    case Drag::Pending:
        if (delta.getDistanceFromOrigin() < kDragThreshold)
            return;
        drag.mode = std::abs(delta.x) > std::abs(delta.y) ? Drag::Time : Drag::Pitch;
        if (drag.mode == Drag::Pitch)
            dragPitch(delta.y, e.mods.isAltDown());
        else
            dragTime(delta.x);
        break;
    case Drag::Pitch:
        dragPitch(delta.y, e.mods.isAltDown());
        break;
    case Drag::Time:
        dragTime(delta.x);
        break;
    case Drag::Stretch:
        dragStretch(delta.x);
        break;
    case Drag::Glide:
        dragGlide(delta);
        break;
    case Drag::Band:
        dragBand(e.position);
        break;
    case Drag::Pan:
        setViewStart(drag.viewStartAtDown - static_cast<double>(delta.x) / pixelsPerSecond);
        break;
    case Drag::None:
        break;
    }
    repaint();
}

void NoteCanvas::mouseUp(const juce::MouseEvent& e)
{
    if (drag.mode == Drag::Pending && drag.clickedSelected && !e.mods.isShiftDown() && !e.mods.isCommandDown())
    {
        const auto group = controller.expand(drag.ref, granularity);
        controller.setSelection({group.begin(), group.end()});
    }
    if (controller.inGesture())
        controller.endGesture();
    drag = {};
    hover = findHit(e.position);
    repaint();
}

void NoteCanvas::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto hit = findHit(e.position);
    if (hit.kind == Hit::Kind::Separation)
    {
        mergeWithNext(hit.ref);
        return;
    }
    if (hit.kind == Hit::Kind::Glide)
    {
        glideAsSung(hit.ref);
        return;
    }
    if (tool == Tool::Edit && hit.kind == Hit::Kind::Body)
        toggleSnap();
}

void NoteCanvas::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomTime(std::exp(static_cast<double>(wheel.deltaY) * 1.5), e.position.x);
        return;
    }
    if (e.mods.isAltDown())
    {
        zoomPitch(std::exp(wheel.deltaY * 1.2f), e.position.y);
        return;
    }
    const float sideways = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
    const float upDown = e.mods.isShiftDown() ? 0.0f : wheel.deltaY;
    viewStart -= static_cast<double>(sideways) * 0.5 * getVisibleSeconds();
    topMidi += upDown * 18.0f * (14.0f / rowHeight);
    viewChanged();
}

void NoteCanvas::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    zoomTime(static_cast<double>(scaleFactor), e.position.x);
}

// Drags --------------------------------------------------------------------------------------------------------------

void NoteCanvas::dragPitch(float dy, bool fine)
{
    float delta = -dy / rowHeight;
    if (!fine)
        delta = std::round(delta);
    const bool stepped = std::abs(delta - drag.pitchDelta) > 1.0e-4f;
    drag.pitchDelta = delta;
    const auto scale = controller.scale();
    float shownTarget = -1.0f;
    for (const auto& take : drag.origin)
    {
        if (take.analysis == nullptr)
            continue;
        auto notes = take.notes;
        bool any = false;
        for (auto& n : notes)
        {
            if (!controller.isSelected({take.id, n.id}))
                continue;
            any = true;
            const float moved = n.centre + n.pitch;
            if (fine)
            {
                // Free movement: start from where the note sounds now, unsnapped.
                const float target = moved + n.snap * (scale.nearest(moved) - moved);
                n.pitch = target - n.centre + delta;
                n.snap = 0.0f;
            }
            else if (std::abs(delta) > 1.0e-4f)
            {
                // Semitone steps land on the scale, like dropping the note onto a key.
                n.pitch += delta;
                n.snap = 1.0f;
            }
            if (n.id == drag.ref.note && take.id == drag.ref.take)
            {
                const float m = n.centre + n.pitch;
                shownTarget = m + n.snap * (scale.nearest(m) - m);
            }
        }
        if (any)
            controller.setNotes(take.id, std::move(notes));
    }
    drag.readout = formatSemitones(delta).toUpperCase();
    if (shownTarget >= 0.0f)
        drag.readout << "  " << juce::String(juce::CharPointer_UTF8("\xe2\x86\x92")) << "  "
                     << pitchText(shownTarget, true);
    if (stepped)
        audition(drag.ref);
}

void NoteCanvas::dragTime(float dx)
{
    const double seconds = static_cast<double>(dx) / pixelsPerSecond;
    double applied = 0.0;
    for (const auto& take : drag.origin)
    {
        if (take.analysis == nullptr)
            continue;
        auto notes = take.notes;
        std::vector<bool> mask(notes.size());
        bool any = false;
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            mask[i] = controller.isSelected({take.id, notes[i].id});
            any = any || mask[i];
        }
        if (!any)
            continue;
        applied =
            dsp::edit::moveTime(notes, mask, seconds * take.rate(), static_cast<double>(take.length()), take.rate()) /
            take.rate();
        controller.setNotes(take.id, std::move(notes));
    }
    drag.readout = "TIME " + signedMillis(applied);
}

void NoteCanvas::dragStretch(float dx)
{
    for (const auto& take : drag.origin)
    {
        if (take.id != drag.ref.take || take.analysis == nullptr)
            continue;
        auto notes = take.notes;
        const int index = dsp::edit::indexOf(notes, drag.ref.note);
        if (index < 0)
            return;
        const auto& n = notes[static_cast<std::size_t>(index)];
        const double edge = drag.startEdge ? n.outStart : n.outEnd;
        dsp::edit::moveEdge(notes, index, drag.startEdge,
                            edge + static_cast<double>(dx) / pixelsPerSecond * take.rate(),
                            static_cast<double>(take.length()), take.rate());
        const auto& stretched = notes[static_cast<std::size_t>(index)];
        drag.readout = "STRETCH " + juce::String(stretched.outLength() / std::max(1.0, stretched.srcLength()), 2) +
                       "x  " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + "  " +
                       juce::String(stretched.outLength() / take.rate(), 2) + " s";
        controller.setNotes(take.id, std::move(notes));
        return;
    }
}

void NoteCanvas::glideAsSung(const NoteRef& ref)
{
    const auto* take = controller.take(ref.take);
    if (take == nullptr)
        return;
    auto notes = take->notes;
    const int index = dsp::edit::indexOf(notes, ref.note);
    if (index < 0 || !notes[static_cast<std::size_t>(index)].shaped)
        return;
    auto& n = notes[static_cast<std::size_t>(index)];
    n.glide = dsp::kDefaultGlide;
    n.curve = 0.0f;
    n.shaped = false;
    controller.setNotes(ref.take, std::move(notes));
}

void NoteCanvas::dragGlide(juce::Point<float> delta)
{
    for (const auto& take : drag.origin)
    {
        if (take.id != drag.ref.take)
            continue;
        auto notes = take.notes;
        const int index = dsp::edit::indexOf(notes, drag.ref.note);
        if (index < 0)
            return;
        auto& n = notes[static_cast<std::size_t>(index)];
        n.glide = std::clamp(n.glide + 2.0f * delta.x / static_cast<float>(pixelsPerSecond), 0.005f, dsp::kMaxGlide);
        n.curve = std::clamp(n.curve - delta.y / kCurvePixels, -1.0f, 1.0f);
        n.shaped = true;
        drag.readout = joinWithDots({"GLIDE " + millis(n.glide), "CURVE " + curveText(n.curve)});
        controller.setNotes(take.id, std::move(notes));
        return;
    }
}

void NoteCanvas::dragBand(juce::Point<float> to)
{
    drag.band = juce::Rectangle<float>(drag.start, to);
    auto selection = drag.bandBase;
    for (const auto& take : controller.takes())
        for (const auto& note : take.notes)
            if (blobBounds(take, note).intersects(drag.band))
                selection.insert({take.id, note.id});
    controller.setSelection(selection);
}

// Actions ------------------------------------------------------------------------------------------------------------

void NoteCanvas::nudgePitch(float semitones)
{
    controller.editTakes(
        [&](const TakeInfo&, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (mask[i])
                    notes[i].pitch += semitones;
            return true;
        },
        true);
    if (!controller.selection().empty())
        audition(*controller.selection().begin());
}

void NoteCanvas::nudgeTime(double seconds)
{
    controller.editTakes(
        [&](const TakeInfo& take, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            return std::abs(dsp::edit::moveTime(notes, mask, seconds * take.rate(), static_cast<double>(take.length()),
                                                take.rate())) > 0.0;
        },
        true);
}

void NoteCanvas::toggleSnap()
{
    bool allSnapped = true;
    for (const auto& ref : controller.selection())
        if (const auto* note = controller.note(ref))
            allSnapped = allSnapped && note->snap >= 0.999f;
    controller.editTakes(
        [&](const TakeInfo&, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (mask[i])
                    notes[i].snap = allSnapped ? 0.0f : 1.0f;
            return true;
        },
        true);
}

void NoteCanvas::toggleMute()
{
    bool allMuted = true;
    for (const auto& ref : controller.selection())
        if (const auto* note = controller.note(ref))
            allMuted = allMuted && note->muted;
    controller.editTakes(
        [&](const TakeInfo&, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (mask[i])
                    notes[i].muted = !allMuted;
            return true;
        },
        true);
}

void NoteCanvas::resetSound()
{
    controller.editTakes(
        [](const TakeInfo&, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (mask[i])
                    dsp::edit::resetSound(notes[i]);
            return true;
        },
        true);
}

void NoteCanvas::resetTiming()
{
    controller.editTakes(
        [](const TakeInfo&, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            dsp::edit::resetTiming(notes, mask);
            return true;
        },
        true);
}

bool NoteCanvas::splitAt(const NoteRef& ref, double songSeconds)
{
    const auto* take = controller.take(ref.take);
    if (take == nullptr || take->analysis == nullptr)
        return false;
    auto notes = take->notes;
    const int index = dsp::edit::indexOf(notes, ref.note);
    const auto newId = controller.newNoteId();
    if (index < 0 ||
        !dsp::edit::split(notes, index, (songSeconds - take->startSeconds) * take->rate(), *take->analysis, newId))
        return false;
    controller.setNotes(ref.take, std::move(notes));
    controller.setSelection({ref, {ref.take, newId}});
    return true;
}

bool NoteCanvas::mergeWithNext(const NoteRef& ref)
{
    const auto* take = controller.take(ref.take);
    if (take == nullptr || take->analysis == nullptr)
        return false;
    auto notes = take->notes;
    const int index = dsp::edit::indexOf(notes, ref.note);
    if (index < 0 || !dsp::edit::merge(notes, index, *take->analysis))
        return false;
    controller.setNotes(ref.take, std::move(notes));
    controller.setSelection({ref});
    return true;
}

bool NoteCanvas::toggleLink(const NoteRef& ref)
{
    const auto* take = controller.take(ref.take);
    if (take == nullptr)
        return false;
    auto notes = take->notes;
    const int index = dsp::edit::indexOf(notes, ref.note);
    if (index < 0 || !dsp::edit::setLinked(notes, index, !notes[static_cast<std::size_t>(index)].linkedToNext))
        return false;
    controller.setNotes(ref.take, std::move(notes));
    return true;
}

void NoteCanvas::showMenu(const Hit& hit, juce::Point<float> at)
{
    enum
    {
        snapId = 1,
        unsnapId,
        straightenId,
        muteId,
        resetSoundId,
        resetTimeId,
        splitId,
        mergeId,
        linkId,
        glideId,
        wordId,
        phraseId,
        allId,
        fitId
    };
    const bool onNote = hit.kind != Hit::Kind::None;
    const auto* note = onNote ? controller.note(hit.ref) : nullptr;
    const bool any = !controller.selection().empty();
    bool muted = any;
    for (const auto& ref : controller.selection())
        if (const auto* n = controller.note(ref))
            muted = muted && n->muted;

    juce::PopupMenu menu;
    menu.addItem(snapId, "Snap to Scale", any);
    menu.addItem(unsnapId, "Unsnap", any);
    menu.addItem(straightenId, "Straighten Drift", any);
    menu.addItem(muteId, muted ? "Unmute" : "Mute", any);
    menu.addItem(resetSoundId, "Reset Pitch and Sound", any);
    menu.addItem(resetTimeId, "Reset Timing", any);
    menu.addSeparator();
    menu.addItem(splitId, "Split Here", note != nullptr && hit.kind == Hit::Kind::Body);
    const auto* take = onNote ? controller.take(hit.ref.take) : nullptr;
    const int index = onNote ? controller.indexOf(hit.ref) : -1;
    const bool hasNext = take != nullptr && index >= 0 && index + 1 < static_cast<int>(take->notes.size());
    menu.addItem(mergeId, "Merge with Next",
                 hasNext && touching(*note, take->notes[static_cast<std::size_t>(index + 1)]));
    menu.addItem(linkId, note != nullptr && note->linkedToNext ? "Unlink from Next" : "Link to Next",
                 hasNext && (note->linkedToNext || dsp::edit::canLink(take->notes, index)));
    menu.addItem(glideId, "Glide as Sung", note != nullptr && note->shapesTransition());
    menu.addSeparator();
    menu.addItem(wordId, "Select Word", note != nullptr);
    menu.addItem(phraseId, "Select Phrase", note != nullptr);
    menu.addItem(allId, "Select All");
    menu.addItem(fitId, "Fit to Window");

    const auto ref = hit.ref;
    const double seconds = secondsForX(at.x);
    juce::Component::SafePointer<NoteCanvas> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(
                           juce::Rectangle<int>(juce::roundToInt(at.x), juce::roundToInt(at.y), 1, 1))),
                       [safe, ref, seconds](int result)
                       {
                           if (safe == nullptr || result == 0)
                               return;
                           auto& canvas = *safe;
                           auto& c = canvas.controller;
                           switch (result)
                           {
                           case snapId:
                           case unsnapId:
                               c.editTargets([result](dsp::Note& n) { n.snap = result == snapId ? 1.0f : 0.0f; });
                               break;
                           case straightenId:
                               c.editTargets([](dsp::Note& n) { n.drift = 0.0f; });
                               break;
                           case muteId:
                               canvas.toggleMute();
                               break;
                           case resetSoundId:
                               canvas.resetSound();
                               break;
                           case resetTimeId:
                               canvas.resetTiming();
                               break;
                           case splitId:
                               canvas.splitAt(ref, seconds);
                               break;
                           case mergeId:
                               canvas.mergeWithNext(ref);
                               break;
                           case linkId:
                               canvas.toggleLink(ref);
                               break;
                           case glideId:
                               canvas.glideAsSung(ref);
                               break;
                           case wordId:
                           case phraseId:
                           {
                               const auto group =
                                   c.expand(ref, result == wordId ? Granularity::Word : Granularity::Phrase);
                               c.setSelection({group.begin(), group.end()});
                               break;
                           }
                           case allId:
                               c.selectAll();
                               break;
                           case fitId:
                               canvas.fitAll();
                               break;
                           default:
                               break;
                           }
                       });
}

bool NoteCanvas::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const auto letter = static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(code)));
    if (mods.isCommandDown())
    {
        if (letter == 'Z' && mods.isShiftDown())
            controller.redo();
        else if (letter == 'Z')
            controller.undo();
        else if (letter == 'Y')
            controller.redo();
        else if (letter == 'A')
            controller.selectAll();
        else
            return false;
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        controller.clearSelection();
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const float step = mods.isAltDown() ? 0.1f : mods.isShiftDown() ? 12.0f : 1.0f;
        nudgePitch(code == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const double step = mods.isShiftDown() ? 0.1 : 0.01;
        nudgeTime(code == juce::KeyPress::rightKey ? step : -step);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        toggleMute();
        return true;
    }
    switch (letter)
    {
    case 'S':
        toggleSnap();
        return true;
    case '0':
        resetSound();
        return true;
    case 'T':
        resetTiming();
        return true;
    case '1':
        setTool(Tool::Edit);
        return true;
    case '2':
        setTool(Tool::Cut);
        return true;
    case 'F':
        fitAll();
        return true;
    case '=':
    case '+':
        zoomTime(1.4, noteArea().getCentreX());
        return true;
    case '-':
        zoomTime(1.0 / 1.4, noteArea().getCentreX());
        return true;
    default:
        return false;
    }
}
} // namespace whale::ui
