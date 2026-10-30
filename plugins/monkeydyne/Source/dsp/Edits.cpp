#include "Edits.h"

#include "EditPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace monkeydyne::dsp::edit
{
namespace
{
struct LengthLimits
{
    double shortest = 0.0;
    double longest = 0.0;
};

LengthLimits limitsOf(const Note& note, double rate) noexcept
{
    const double shortest = std::max(kShortestNoteSeconds * rate, kMinStretch * note.srcLength());
    return {shortest, std::max(shortest, kMaxStretch * note.srcLength())};
}

bool touchingBoth(const Note& a, const Note& b) noexcept
{
    return std::abs(b.outStart - a.outEnd) < 1.0 && std::abs(b.srcStart - a.srcEnd) < 1.0;
}

void recentre(Note& note, const TakeAnalysis& analysis) noexcept
{
    const float centre = analysis.centreBetween(note.voicedStart, note.voicedEnd);
    if (centre >= 0.0f)
        note.centre = centre;
}
} // namespace

int indexOf(const std::vector<Note>& notes, std::uint32_t id) noexcept
{
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].id == id)
            return static_cast<int>(i);
    return -1;
}

double moveTime(std::vector<Note>& notes, const std::vector<bool>& selected, double delta, double takeLength,
                double rate)
{
    const int n = static_cast<int>(notes.size());
    if (n == 0 || static_cast<int>(selected.size()) != n)
        return 0.0;
    double low = -std::numeric_limits<double>::infinity();
    double high = std::numeric_limits<double>::infinity();
    bool any = false;
    for (int i = 0; i < n;)
    {
        if (!selected[static_cast<std::size_t>(i)])
        {
            ++i;
            continue;
        }
        any = true;
        int j = i;
        while (j + 1 < n && selected[static_cast<std::size_t>(j + 1)])
            ++j;
        const auto& first = notes[static_cast<std::size_t>(i)];
        const auto& last = notes[static_cast<std::size_t>(j)];
        if (i == 0)
            low = std::max(low, -first.outStart);
        else
        {
            const auto& left = notes[static_cast<std::size_t>(i - 1)];
            if (left.linkedToNext)
            {
                const auto limits = limitsOf(left, rate);
                low = std::max(low, limits.shortest - left.outLength());
                high = std::min(high, limits.longest - left.outLength());
            }
            else
                low = std::max(low, left.outEnd - first.outStart);
        }
        if (j == n - 1)
            high = std::min(high, takeLength - last.outEnd);
        else
        {
            const auto& right = notes[static_cast<std::size_t>(j + 1)];
            if (last.linkedToNext)
            {
                const auto limits = limitsOf(right, rate);
                high = std::min(high, right.outLength() - limits.shortest);
                low = std::max(low, right.outLength() - limits.longest);
            }
            else
                high = std::min(high, right.outStart - last.outEnd);
        }
        i = j + 1;
    }
    if (!any)
        return 0.0;
    const double applied = std::clamp(delta, std::min(low, 0.0), std::max(high, 0.0));
    if (std::abs(applied) < 1.0e-9)
        return 0.0;
    for (int i = 0; i < n; ++i)
    {
        auto& note = notes[static_cast<std::size_t>(i)];
        if (selected[static_cast<std::size_t>(i)])
        {
            note.outStart += applied;
            note.outEnd += applied;
            continue;
        }
        if (i > 0 && selected[static_cast<std::size_t>(i - 1)] && notes[static_cast<std::size_t>(i - 1)].linkedToNext)
            note.outStart += applied;
        if (i + 1 < n && selected[static_cast<std::size_t>(i + 1)] && note.linkedToNext)
            note.outEnd += applied;
    }
    return applied;
}

double moveEdge(std::vector<Note>& notes, int index, bool startEdge, double position, double takeLength, double rate)
{
    if (index < 0 || index >= static_cast<int>(notes.size()))
        return position;
    auto& note = notes[static_cast<std::size_t>(index)];
    const auto own = limitsOf(note, rate);
    double low, high;
    if (startEdge)
    {
        low = note.outEnd - own.longest;
        high = note.outEnd - own.shortest;
        if (index > 0)
        {
            const auto& left = notes[static_cast<std::size_t>(index - 1)];
            if (left.linkedToNext)
            {
                const auto limits = limitsOf(left, rate);
                low = std::max(low, left.outStart + limits.shortest);
                high = std::min(high, left.outStart + limits.longest);
            }
            else
                low = std::max(low, left.outEnd);
        }
        else
            low = std::max(low, 0.0);
    }
    else
    {
        low = note.outStart + own.shortest;
        high = note.outStart + own.longest;
        if (index + 1 < static_cast<int>(notes.size()))
        {
            const auto& right = notes[static_cast<std::size_t>(index + 1)];
            if (note.linkedToNext)
            {
                const auto limits = limitsOf(right, rate);
                low = std::max(low, right.outEnd - limits.longest);
                high = std::min(high, right.outEnd - limits.shortest);
            }
            else
                high = std::min(high, right.outStart);
        }
        else
            high = std::min(high, takeLength);
    }
    const double current = startEdge ? note.outStart : note.outEnd;
    const double target = low <= high ? std::clamp(position, low, high) : current;
    if (startEdge)
    {
        note.outStart = target;
        if (index > 0 && notes[static_cast<std::size_t>(index - 1)].linkedToNext)
            notes[static_cast<std::size_t>(index - 1)].outEnd = target;
    }
    else
    {
        note.outEnd = target;
        if (index + 1 < static_cast<int>(notes.size()) && note.linkedToNext)
            notes[static_cast<std::size_t>(index + 1)].outStart = target;
    }
    return target;
}

bool split(std::vector<Note>& notes, int index, double position, const TakeAnalysis& analysis, std::uint32_t newId)
{
    if (index < 0 || index >= static_cast<int>(notes.size()))
        return false;
    auto& a = notes[static_cast<std::size_t>(index)];
    const double shortest = kShortestNoteSeconds * analysis.rate;
    if (position - a.outStart < shortest || a.outEnd - position < shortest)
        return false;
    const double source = noteSourceAt(a, position, analysis.rate);
    Note b = a;
    b.id = newId;
    b.srcStart = source;
    b.outStart = position;
    b.voicedStart = std::clamp(source, a.voicedStart, a.voicedEnd);
    a.srcEnd = source;
    a.outEnd = position;
    a.voicedEnd = std::clamp(source, a.voicedStart, a.voicedEnd);
    a.linkedToNext = true;
    a.glide = kSplitGlide;
    a.curve = 0.0f;
    a.shaped = false;
    recentre(a, analysis);
    recentre(b, analysis);
    notes.insert(notes.begin() + index + 1, b);
    return true;
}

bool merge(std::vector<Note>& notes, int index, const TakeAnalysis& analysis)
{
    if (index < 0 || index + 1 >= static_cast<int>(notes.size()))
        return false;
    auto& a = notes[static_cast<std::size_t>(index)];
    const auto& b = notes[static_cast<std::size_t>(index + 1)];
    if (!touchingBoth(a, b))
        return false;
    const bool aVoiced = a.voicedEnd - a.voicedStart >= 1.0;
    const bool bVoiced = b.voicedEnd - b.voicedStart >= 1.0;
    if (!aVoiced)
        a.voicedStart = b.voicedStart;
    if (bVoiced || !aVoiced)
        a.voicedEnd = b.voicedEnd;
    a.srcEnd = b.srcEnd;
    a.outEnd = b.outEnd;
    a.linkedToNext = b.linkedToNext;
    a.glide = b.glide;
    a.curve = b.curve;
    a.shaped = b.shaped;
    recentre(a, analysis);
    notes.erase(notes.begin() + index + 1);
    return true;
}

bool canLink(const std::vector<Note>& notes, int index) noexcept
{
    return index >= 0 && index + 1 < static_cast<int>(notes.size()) &&
           touchingBoth(notes[static_cast<std::size_t>(index)], notes[static_cast<std::size_t>(index + 1)]);
}

bool setLinked(std::vector<Note>& notes, int index, bool linked)
{
    if (index < 0 || index >= static_cast<int>(notes.size()) || (linked && !canLink(notes, index)))
        return false;
    notes[static_cast<std::size_t>(index)].linkedToNext = linked;
    return true;
}

void resetSound(Note& note) noexcept
{
    note.pitch = 0.0f;
    note.snap = 0.0f;
    note.drift = 1.0f;
    note.vibrato = 1.0f;
    note.formant = 0.0f;
    note.gainDb = 0.0f;
    note.muted = false;
    note.glide = kDefaultGlide;
    note.curve = 0.0f;
    note.shaped = false;
}

void resetTiming(std::vector<Note>& notes, const std::vector<bool>& selected)
{
    const auto n = notes.size();
    if (selected.size() != n)
        return;
    std::vector<bool> reset = selected;
    for (bool changed = true; changed;)
    {
        changed = false;
        for (std::size_t k = 0; k + 1 < n; ++k)
        {
            const bool linked = notes[k].linkedToNext;
            const auto endOf = [&](std::size_t i) { return reset[i] ? notes[i].srcEnd : notes[i].outEnd; };
            const auto startOf = [&](std::size_t i) { return reset[i] ? notes[i].srcStart : notes[i].outStart; };
            const bool broken = (linked && reset[k] != reset[k + 1]) || startOf(k + 1) < endOf(k) - 0.5;
            if (broken && !(reset[k] && reset[k + 1]))
            {
                reset[k] = reset[k + 1] = true;
                changed = true;
            }
        }
    }
    for (std::size_t k = 0; k < n; ++k)
        if (reset[k])
        {
            notes[k].outStart = notes[k].srcStart;
            notes[k].outEnd = notes[k].srcEnd;
        }
}
} // namespace monkeydyne::dsp::edit
