#include "SegmentCache.h"

#include <cmath>
#include <cstring>

namespace whale::dsp
{
namespace
{
constexpr double kContextSeconds = 0.8;

struct Hasher
{
    std::uint64_t value = 1469598103934665603ULL;

    template <class T>
    void add(const T& x) noexcept
    {
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &x, sizeof(T));
        for (const unsigned char b : bytes)
        {
            value ^= b;
            value *= 1099511628211ULL;
        }
    }
};

void addNote(Hasher& h, const Note& n) noexcept
{
    h.add(n.srcStart);
    h.add(n.srcEnd);
    h.add(n.voicedStart);
    h.add(n.voicedEnd);
    h.add(n.centre);
    h.add(n.linkedToNext);
    h.add(n.outStart);
    h.add(n.outEnd);
    h.add(n.pitch);
    h.add(n.snap);
    h.add(n.drift);
    h.add(n.vibrato);
    h.add(n.formant);
    h.add(n.gainDb);
    h.add(n.muted);
    h.add(n.glide);
    h.add(n.curve);
    h.add(n.shaped);
}
} // namespace

std::vector<SegmentCache::Planned> SegmentCache::planSegments(const TakeAnalysis& analysis, const EditPlan& plan) const
{
    const double rate = analysis.rate;
    const auto fade = static_cast<std::int64_t>(std::llround(kFadeSeconds * rate));
    const std::int64_t length = analysis.length;

    // Cut positions in output time, at least two fades apart.
    std::vector<std::int64_t> outs;
    for (const double cut : analysis.cuts)
    {
        const auto o = std::clamp<std::int64_t>(std::llround(plan.outputOf(cut)), 0, length);
        if (outs.empty() || o - outs.back() >= 2 * fade + 2)
            outs.push_back(o);
    }
    if (outs.empty() || outs.front() != 0)
        outs.insert(outs.begin(), 0);
    if (outs.back() != length)
    {
        if (outs.size() > 1 && length - outs.back() < 2 * fade + 2)
            outs.back() = length;
        else
            outs.push_back(length);
    }

    std::vector<Planned> planned;
    const auto& notes = plan.notes();
    const double context = kContextSeconds * rate;
    for (std::size_t k = 0; k + 1 < outs.size(); ++k)
    {
        Planned p;
        p.rises = k > 0;
        p.falls = k + 2 < outs.size();
        p.rise = outs[k];
        p.fall = outs[k + 1];
        p.begin = p.rises ? p.rise - fade : 0;
        p.end = p.falls ? p.fall + fade : length;
        p.neutral = plan.isNeutral(static_cast<double>(p.begin), static_cast<double>(p.end));
        Hasher h;
        h.add(p.begin);
        h.add(p.end);
        h.add(p.rise);
        h.add(p.fall);
        h.add(p.rises);
        h.add(p.falls);
        h.add(rate);
        bool snaps = false;
        for (const auto& note : notes)
            if (note.outEnd > static_cast<double>(p.begin) - context &&
                note.outStart < static_cast<double>(p.end) + context)
            {
                addNote(h, note);
                snaps = snaps || note.snap > 0.0f;
            }
        if (snaps && !notes.empty())
        {
            // The scale only matters where something snaps.
            const auto& probe = notes.front();
            Note test = probe;
            for (int pc = 0; pc < 12; ++pc)
            {
                test.centre = 60.0f + static_cast<float>(pc) + 0.4f;
                test.pitch = 0.0f;
                test.snap = 1.0f;
                h.add(plan.targetCentre(test));
            }
        }
        p.signature = h.value;
        planned.push_back(p);
    }
    return planned;
}

bool SegmentCache::update(const SourceView& source, const TakeAnalysis& analysis, const EditPlan& plan,
                          const std::function<void()>& published, const std::function<bool()>& stop,
                          const std::atomic<bool>* cancel)
{
    renderedCount = 0;
    const auto planned = planSegments(analysis, plan);

    // Keep what still matches; drop the rest. Rendered segments are rebuilt in plan order.
    std::vector<std::shared_ptr<const RenderedSegment>> next;
    std::vector<std::uint64_t> nextSignatures;
    std::vector<std::size_t> toRender;
    for (std::size_t k = 0; k < planned.size(); ++k)
    {
        const auto& p = planned[k];
        if (p.neutral)
            continue;
        bool reused = false;
        for (std::size_t c = 0; c < current.size(); ++c)
            if (signatures[c] == p.signature)
            {
                next.push_back(current[c]);
                nextSignatures.push_back(p.signature);
                reused = true;
                break;
            }
        if (!reused)
            toRender.push_back(k);
    }
    // Publish the kept segments first so stale audio of changed segments stops playing.
    current = next;
    signatures = nextSignatures;
    const auto insertSorted = [this](std::shared_ptr<const RenderedSegment> segment, std::uint64_t signature)
    {
        std::size_t at = 0;
        while (at < current.size() && current[at]->begin < segment->begin)
            ++at;
        current.insert(current.begin() + static_cast<std::ptrdiff_t>(at), std::move(segment));
        signatures.insert(signatures.begin() + static_cast<std::ptrdiff_t>(at), signature);
    };

    for (const auto k : toRender)
    {
        if (stop && stop())
            return false;
        const auto& p = planned[k];
        auto segment = std::make_shared<RenderedSegment>();
        segment->begin = p.begin;
        segment->end = p.end;
        segment->riseCentre = p.rise;
        segment->fallCentre = p.fall;
        segment->fade = static_cast<std::int64_t>(std::llround(kFadeSeconds * analysis.rate));
        segment->rises = p.rises;
        segment->falls = p.falls;
        if (!renderer.render(source, analysis, plan, p.begin, p.end, segment->audio, cancel))
            return false;
        applyWindow(*segment, fades);
        insertSorted(std::move(segment), p.signature);
        ++renderedCount;
        if (published)
            published();
    }
    return true;
}
} // namespace whale::dsp
