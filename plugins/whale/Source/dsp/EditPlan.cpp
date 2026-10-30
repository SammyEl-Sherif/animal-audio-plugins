#include "EditPlan.h"

#include <algorithm>
#include <cmath>

namespace whale::dsp
{
namespace
{
constexpr double kAttackSeconds = 0.03;
constexpr double kEdgeRampSeconds = 0.003;
constexpr double kGapFadeSeconds = 0.01;
constexpr double kNeutralMarginSeconds = 0.7;
constexpr double kShapedReachSeconds = 0.08;
constexpr double kShapedFadeSeconds = 0.04;

double attackLength(const Note& note, double rate) noexcept
{
    if (!note.isStretched())
        return 0.0;
    const double attack =
        std::min(std::max(0.0, note.voicedStart - note.srcStart) + kAttackSeconds * rate, 0.4 * note.srcLength());
    if (note.outLength() <= attack * 1.05 || note.srcLength() <= attack * 1.05)
        return 0.0;
    return attack;
}

bool touching(const Note& a, const Note& b) noexcept
{
    return std::abs(b.outStart - a.outEnd) < 1.0;
}
} // namespace

float glideShape(float u, float curve) noexcept
{
    u = std::clamp(u, 0.0f, 1.0f);
    curve = std::clamp(curve, -1.0f, 1.0f);
    if (curve > 0.0f)
        u = std::pow(u, 1.0f + 3.0f * curve);
    else if (curve < 0.0f)
        u = 1.0f - std::pow(1.0f - u, 1.0f - 3.0f * curve);
    return u * u * (3.0f - 2.0f * u);
}

double noteSourceAt(const Note& note, double out, double rate) noexcept
{
    if (note.outLength() <= 0.0)
        return note.srcStart;
    const double attack = attackLength(note, rate);
    if (attack > 0.0)
    {
        if (out < note.outStart + attack)
            return note.srcStart + (out - note.outStart);
        return note.srcStart + attack +
               (out - note.outStart - attack) * (note.srcLength() - attack) / (note.outLength() - attack);
    }
    return note.srcStart + (out - note.outStart) * note.srcLength() / note.outLength();
}

EditPlan::EditPlan(const TakeAnalysis& takeAnalysis, std::vector<Note> notes, const Scale& takeScale)
    : analysis(&takeAnalysis)
    , notes_(std::move(notes))
    , scale(takeScale)
{
    std::sort(notes_.begin(), notes_.end(), [](const Note& a, const Note& b) { return a.srcStart < b.srcStart; });
    const auto add = [this](double out, double source)
    {
        if (!anchors.empty() && (out <= anchors.back().first + 1.0e-9 || source < anchors.back().second))
            return;
        anchors.emplace_back(out, source);
    };
    anchors.emplace_back(0.0, 0.0);
    for (const auto& note : notes_)
    {
        add(note.outStart, note.srcStart);
        const double attack = attackLength(note, analysis->rate);
        if (attack > 0.0)
            add(note.outStart + attack, note.srcStart + attack);
        add(note.outEnd, note.srcEnd);
    }
    const auto length = static_cast<double>(analysis->length);
    add(std::max(length, anchors.back().first + (length - anchors.back().second)), length);
}

double EditPlan::sourceOf(double out) const noexcept
{
    if (anchors.size() < 2 || out <= anchors.front().first)
        return out;
    if (out >= anchors.back().first)
        return anchors.back().second + (out - anchors.back().first);
    const auto it = std::upper_bound(anchors.begin(), anchors.end(), out,
                                     [](double value, const std::pair<double, double>& a) { return value < a.first; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    return a.second + (out - a.first) * (b.second - a.second) / (b.first - a.first);
}

double EditPlan::outputOf(double source) const noexcept
{
    if (anchors.size() < 2 || source <= anchors.front().second)
        return source;
    if (source >= anchors.back().second)
        return anchors.back().first + (source - anchors.back().second);
    const auto it = std::upper_bound(anchors.begin(), anchors.end(), source,
                                     [](double value, const std::pair<double, double>& a) { return value < a.second; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    if (b.second - a.second < 1.0e-9)
        return a.first;
    return a.first + (source - a.second) * (b.first - a.first) / (b.second - a.second);
}

int EditPlan::noteAt(double out) const noexcept
{
    const auto it = std::upper_bound(notes_.begin(), notes_.end(), out,
                                     [](double value, const Note& n) { return value < n.outStart; });
    if (it == notes_.begin())
        return -1;
    const auto index = static_cast<int>(it - notes_.begin()) - 1;
    return out < notes_[static_cast<std::size_t>(index)].outEnd ? index : -1;
}

EditPlan::Blend EditPlan::blendAt(double out, double minimumGlideSeconds) const noexcept
{
    const int k = noteAt(out);
    Blend blend;
    blend.first = k;
    if (k < 0)
        return blend;
    const auto joined = [&](const Note& a, const Note& b)
    { return a.linkedToNext || (minimumGlideSeconds > 0.0 && touching(a, b)); };

    const auto& note = notes_[static_cast<std::size_t>(k)];
    if (k > 0 && joined(notes_[static_cast<std::size_t>(k - 1)], note))
    {
        const auto& previous = notes_[static_cast<std::size_t>(k - 1)];
        const double g = glideSamples(previous, note, minimumGlideSeconds);
        if (out < note.outStart + 0.5 * g)
        {
            blend.first = k - 1;
            blend.second = k;
            blend.weight = glideShape(static_cast<float>((out - (note.outStart - 0.5 * g)) / g), previous.curve);
            return blend;
        }
    }
    if (k + 1 < static_cast<int>(notes_.size()) && joined(note, notes_[static_cast<std::size_t>(k + 1)]))
    {
        const double g = glideSamples(note, notes_[static_cast<std::size_t>(k + 1)], minimumGlideSeconds);
        if (out > note.outEnd - 0.5 * g)
        {
            blend.second = k + 1;
            blend.weight = glideShape(static_cast<float>((out - (note.outEnd - 0.5 * g)) / g), note.curve);
        }
    }
    return blend;
}

double EditPlan::glideSamples(const Note& a, const Note& b, double minimumGlideSeconds) const noexcept
{
    const double seconds =
        a.linkedToNext ? std::max(static_cast<double>(a.glide), minimumGlideSeconds) : minimumGlideSeconds;
    return std::clamp(seconds * analysis->rate, 1.0, std::max(1.0, 0.9 * std::min(a.outLength(), b.outLength())));
}

float EditPlan::movementKept(int index, double out) const noexcept
{
    const double rate = analysis->rate;
    double kept = 1.0;
    const auto around = [&](int first)
    {
        if (first < 0 || first + 1 >= static_cast<int>(notes_.size()))
            return;
        const auto& a = notes_[static_cast<std::size_t>(first)];
        if (!a.shapesTransition())
            return;
        const auto& b = notes_[static_cast<std::size_t>(first + 1)];
        const double reach = std::max(0.5 * glideSamples(a, b, 0.0), kShapedReachSeconds * rate);
        const double past = std::abs(out - a.outEnd) - reach;
        kept = std::min(kept, std::clamp(past / (kShapedFadeSeconds * rate), 0.0, 1.0));
    };
    around(index - 1);
    around(index);
    return static_cast<float>(kept);
}

float EditPlan::targetCentre(const Note& note) const noexcept
{
    const float moved = note.centre + note.pitch;
    return moved + note.snap * (scale.nearest(moved) - moved);
}

float EditPlan::noteShift(const Note& note, double source, float kept) const noexcept
{
    const float centre = targetCentre(note);
    const float sung = analysis->midiAt(source);
    if (sung < 0.0f)
        return centre - note.centre;
    float slow = analysis->driftAt(source);
    if (slow < 0.0f)
        slow = sung;
    const float target = centre + kept * (note.drift * (slow - note.centre) + note.vibrato * (sung - slow));
    return target - sung;
}

float EditPlan::shiftAt(double out) const noexcept
{
    if (notes_.empty() || analysis == nullptr)
        return 0.0f;
    const auto blend = blendAt(out, 0.0);
    const double source = sourceOf(out);
    if (blend.first < 0)
    {
        const auto it = std::upper_bound(notes_.begin(), notes_.end(), out,
                                         [](double value, const Note& n) { return value < n.outStart; });
        const Note* before = it == notes_.begin() ? nullptr : &*(it - 1);
        const Note* after = it == notes_.end() ? nullptr : &*it;
        const Note* nearest = before == nullptr  ? after
                              : after == nullptr ? before
                                                 : (out - before->outEnd < after->outStart - out ? before : after);
        return targetCentre(*nearest) - nearest->centre;
    }
    const float a = noteShift(notes_[static_cast<std::size_t>(blend.first)], source, movementKept(blend.first, out));
    if (blend.second < 0)
        return a;
    const float b = noteShift(notes_[static_cast<std::size_t>(blend.second)], source, movementKept(blend.second, out));
    return a + (b - a) * blend.weight;
}

float EditPlan::formantAt(double out) const noexcept
{
    if (notes_.empty())
        return 0.0f;
    const auto blend = blendAt(out, 0.02);
    if (blend.first < 0)
    {
        const auto it = std::upper_bound(notes_.begin(), notes_.end(), out,
                                         [](double value, const Note& n) { return value < n.outStart; });
        const Note& nearest = it == notes_.begin() ? *it
                              : it == notes_.end() ? *(it - 1)
                                                   : (out - (it - 1)->outEnd < it->outStart - out ? *(it - 1) : *it);
        return nearest.formant;
    }
    const float a = notes_[static_cast<std::size_t>(blend.first)].formant;
    if (blend.second < 0)
        return a;
    return a + (notes_[static_cast<std::size_t>(blend.second)].formant - a) * blend.weight;
}

float EditPlan::noteGain(int index) const noexcept
{
    const auto& note = notes_[static_cast<std::size_t>(index)];
    return note.muted ? 0.0f : std::pow(10.0f, note.gainDb / 20.0f);
}

float EditPlan::gapGain(int before, double out) const noexcept
{
    const int after = before + 1;
    if (before < 0 || after >= static_cast<int>(notes_.size()))
        return 1.0f;
    const auto& a = notes_[static_cast<std::size_t>(before)];
    const auto& b = notes_[static_cast<std::size_t>(after)];
    const double rate = analysis->rate;
    const double outGap = b.outStart - a.outEnd;
    const double sourceGap = b.srcStart - a.srcEnd;
    if (outGap <= 0.02 * rate || sourceGap >= 0.5 * outGap)
        return 1.0f;
    const double fade = kGapFadeSeconds * rate;
    const double down = std::max(0.0, 1.0 - (out - a.outEnd) / fade);
    const double up = std::max(0.0, 1.0 - (b.outStart - out) / fade);
    return static_cast<float>(std::min(1.0, down + up));
}

float EditPlan::gainAt(double out) const noexcept
{
    if (notes_.empty())
        return 1.0f;
    const int k = noteAt(out);
    if (k < 0)
    {
        const auto it = std::upper_bound(notes_.begin(), notes_.end(), out,
                                         [](double value, const Note& n) { return value < n.outStart; });
        return gapGain(static_cast<int>(it - notes_.begin()) - 1, out);
    }
    const auto blend = blendAt(out, kEdgeRampSeconds);
    float gain = noteGain(blend.first);
    if (blend.second >= 0)
        gain += (noteGain(blend.second) - gain) * blend.weight;

    const auto& note = notes_[static_cast<std::size_t>(k)];
    const double edge = kEdgeRampSeconds * analysis->rate;
    const bool joinedBefore = k > 0 && (notes_[static_cast<std::size_t>(k - 1)].linkedToNext ||
                                        touching(notes_[static_cast<std::size_t>(k - 1)], note));
    const bool joinedAfter = k + 1 < static_cast<int>(notes_.size()) &&
                             (note.linkedToNext || touching(note, notes_[static_cast<std::size_t>(k + 1)]));
    double ramp = 1.0;
    if (!joinedBefore)
        ramp = std::min(ramp, (out - note.outStart) / edge);
    if (!joinedAfter)
        ramp = std::min(ramp, (note.outEnd - out) / edge);
    ramp = std::clamp(ramp, 0.0, 1.0);
    return static_cast<float>(1.0 + (gain - 1.0) * ramp);
}

float EditPlan::outputMidiAt(double out) const noexcept
{
    if (analysis == nullptr)
        return -1.0f;
    const float sung = analysis->midiAt(sourceOf(out));
    return sung < 0.0f ? -1.0f : sung + shiftAt(out);
}

bool EditPlan::isNeutral(double from, double to) const noexcept
{
    if (analysis == nullptr)
        return true;
    const double margin = kNeutralMarginSeconds * analysis->rate;
    for (const auto& note : notes_)
        if (note.outEnd > from - margin && note.outStart < to + margin && !note.isUnedited())
            return false;
    if (std::abs(sourceOf(from) - from) > 0.5 || std::abs(sourceOf(to) - to) > 0.5)
        return false;
    for (const auto& [out, source] : anchors)
        if (out > from && out < to && std::abs(out - source) > 0.5)
            return false;
    return true;
}
} // namespace whale::dsp
