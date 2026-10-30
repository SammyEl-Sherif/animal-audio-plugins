#include "Model.h"

#include <algorithm>
#include <cmath>

namespace whale::dsp
{
namespace
{
bool near(float a, float b) noexcept
{
    return std::abs(a - b) < 1.0e-5f;
}

template <class Field>
float interpolate(const std::vector<AnalysisFrame>& frames, int hop, double sample, Field field) noexcept
{
    if (frames.empty())
        return -1.0f;
    const double f = std::clamp(sample / hop, 0.0, static_cast<double>(frames.size() - 1));
    const auto i = static_cast<std::size_t>(f);
    const auto j = std::min(i + 1, frames.size() - 1);
    const auto frac = static_cast<float>(f - static_cast<double>(i));
    const float a = field(frames[i]);
    const float b = field(frames[j]);
    if (a >= 0.0f && b >= 0.0f)
        return a + (b - a) * frac;
    return frac < 0.5f ? a : b;
}
} // namespace

float TakeAnalysis::midiAt(double sample) const noexcept
{
    return interpolate(frames, hop, sample, [](const AnalysisFrame& f) { return f.midi; });
}

float TakeAnalysis::driftAt(double sample) const noexcept
{
    return interpolate(frames, hop, sample, [](const AnalysisFrame& f) { return f.drift; });
}

float TakeAnalysis::levelDbAt(double sample) const noexcept
{
    if (frames.empty())
        return -120.0f;
    const double f = std::clamp(sample / hop, 0.0, static_cast<double>(frames.size() - 1));
    const auto i = static_cast<std::size_t>(f);
    const auto j = std::min(i + 1, frames.size() - 1);
    const auto frac = static_cast<float>(f - static_cast<double>(i));
    return frames[i].levelDb + (frames[j].levelDb - frames[i].levelDb) * frac;
}

float TakeAnalysis::centreBetween(double start, double end) const noexcept
{
    if (frames.empty() || end <= start)
        return -1.0f;
    const auto first = static_cast<std::int64_t>(std::ceil(std::max(0.0, start) / hop));
    const auto last =
        std::min(static_cast<std::int64_t>(std::floor(end / hop)), static_cast<std::int64_t>(frames.size()) - 1);
    double steadySum = 0.0, steadyWeight = 0.0, allSum = 0.0, allWeight = 0.0;
    for (auto i = first; i <= last; ++i)
    {
        const auto& f = frames[static_cast<std::size_t>(i)];
        if (f.midi < 0.0f)
            continue;
        const double weight = std::pow(10.0, f.levelDb / 20.0) + 1.0e-9;
        allSum += weight * f.midi;
        allWeight += weight;
        const auto& before = frames[static_cast<std::size_t>(std::max<std::int64_t>(i - 1, 0))];
        const auto& after = frames[static_cast<std::size_t>(std::min<std::int64_t>(i + 1, last))];
        // Steady frames only: a glide into the note would drag the centre towards the previous note.
        if (before.midi >= 0.0f && after.midi >= 0.0f && std::abs(after.midi - before.midi) < 0.25f)
        {
            steadySum += weight * f.midi;
            steadyWeight += weight;
        }
    }
    if (steadyWeight > 0.0 && steadyWeight > 0.25 * allWeight)
        return static_cast<float>(steadySum / steadyWeight);
    return allWeight > 0.0 ? static_cast<float>(allSum / allWeight) : -1.0f;
}

int TakeAnalysis::nearestMark(double sample) const noexcept
{
    if (marks.empty())
        return -1;
    const auto it = std::lower_bound(marks.begin(), marks.end(), sample,
                                     [](const PitchMark& m, double s) { return m.position < s; });
    auto index = static_cast<int>(it - marks.begin());
    if (index >= static_cast<int>(marks.size()))
        return static_cast<int>(marks.size()) - 1;
    if (index > 0 && sample - marks[static_cast<std::size_t>(index - 1)].position <
                         marks[static_cast<std::size_t>(index)].position - sample)
        --index;
    return index;
}

bool Scale::contains(int midiNote) const noexcept
{
    static constexpr bool kMajor[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
    static constexpr bool kMinor[12] = {true, false, true, true, false, true, false, true, true, false, true, false};
    const int degree = ((midiNote - key) % 12 + 12) % 12;
    switch (mode)
    {
    case Mode::Major:
        return kMajor[degree];
    case Mode::Minor:
        return kMinor[degree];
    case Mode::Chromatic:
        break;
    }
    return true;
}

float Scale::nearest(float midi) const noexcept
{
    const int low = static_cast<int>(std::floor(midi)) - 6;
    float best = std::round(midi);
    float bestDistance = 1.0e9f;
    for (int n = low; n <= low + 13; ++n)
    {
        const float distance = std::abs(static_cast<float>(n) - midi);
        if (contains(n) && distance < bestDistance)
        {
            best = static_cast<float>(n);
            bestDistance = distance;
        }
    }
    return best;
}

bool Note::isStretched() const noexcept
{
    return std::abs(outLength() - srcLength()) > 0.5;
}

bool Note::isUnedited() const noexcept
{
    return std::abs(outStart - srcStart) < 0.5 && std::abs(outEnd - srcEnd) < 0.5 && near(pitch, 0.0f) &&
           near(snap, 0.0f) && near(drift, 1.0f) && near(vibrato, 1.0f) && near(formant, 0.0f) && near(gainDb, 0.0f) &&
           !muted && !shapesTransition();
}

Note noteFromDetection(const DetectedNote& detected, std::uint32_t id)
{
    Note note;
    note.id = id;
    note.srcStart = note.outStart = detected.start;
    note.srcEnd = note.outEnd = detected.end;
    note.voicedStart = detected.voicedStart;
    note.voicedEnd = detected.voicedEnd;
    note.centre = detected.centre;
    note.word = detected.word;
    note.phrase = detected.phrase;
    note.linkedToNext = detected.linkedToNext;
    return note;
}

std::vector<Note> notesFromAnalysis(const TakeAnalysis& analysis, std::uint32_t firstId)
{
    std::vector<Note> notes;
    notes.reserve(analysis.notes.size());
    for (const auto& detected : analysis.notes)
        notes.push_back(noteFromDetection(detected, firstId++));
    return notes;
}
} // namespace whale::dsp
