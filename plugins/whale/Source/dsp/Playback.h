#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

/**
 * What the audio thread plays: per take, the original audio plus the segments the worker re-rendered. Segments
 * meet at render cuts (pauses between words) and overlap there by +-`fade` samples with sin^2 / cos^2 windows,
 * so a segment re-rendered after an edit drops in without a click while its neighbours keep their audio.
 * Everything here is immutable once published; reading allocates nothing.
 */
namespace whale::dsp
{
using ChannelData = std::vector<std::vector<float>>;

struct RenderedSegment
{
    std::int64_t begin = 0; // output samples held: [begin, end), window applied
    std::int64_t end = 0;
    std::int64_t riseCentre = 0; // the cut this segment starts at (no rise for the first segment)
    std::int64_t fallCentre = 0; // the cut it ends at (no fall for the last)
    std::int64_t fade = 0;       // half-width of the crossfade
    bool rises = false;
    bool falls = false;
    ChannelData audio;
};

class FadeTable
{
public:
    static constexpr int kSize = 1024;

    FadeTable()
    {
        for (int i = 0; i <= kSize; ++i)
        {
            const double s = std::sin(0.5 * 3.14159265358979323846 * i / kSize);
            values[static_cast<std::size_t>(i)] = static_cast<float>(s * s);
        }
    }

    /** sin^2 rise over u in [0, 1]; 0 before, 1 after. */
    float rise(double u) const noexcept
    {
        if (u <= 0.0)
            return 0.0f;
        if (u >= 1.0)
            return 1.0f;
        const double index = u * kSize;
        const auto whole = static_cast<int>(index);
        const auto frac = static_cast<float>(index - whole);
        return values[static_cast<std::size_t>(whole)] +
               frac * (values[static_cast<std::size_t>(whole + 1)] - values[static_cast<std::size_t>(whole)]);
    }

private:
    std::array<float, kSize + 1> values{};
};

/** Weight of a segment's window at output sample t. */
inline float segmentWeight(const RenderedSegment& segment, std::int64_t t, const FadeTable& fades) noexcept
{
    float w = 1.0f;
    const double width = 2.0 * static_cast<double>(std::max<std::int64_t>(segment.fade, 1));
    if (segment.rises)
        w *= fades.rise(static_cast<double>(t - (segment.riseCentre - segment.fade)) / width);
    if (segment.falls)
        w *= 1.0f - fades.rise(static_cast<double>(t - (segment.fallCentre - segment.fade)) / width);
    return w;
}

/** Multiplies freshly rendered audio (covering [segment.begin, segment.end)) by the segment's window. */
inline void applyWindow(RenderedSegment& segment, const FadeTable& fades)
{
    for (auto& channel : segment.audio)
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] *= segmentWeight(segment, segment.begin + static_cast<std::int64_t>(i), fades);
}

struct TakePlayback
{
    std::uint32_t id = 0;
    double startSeconds = 0.0; // where the take sits on the host timeline
    double rate = 48000.0;
    std::int64_t length = 0;
    std::shared_ptr<const ChannelData> original;
    std::vector<std::shared_ptr<const RenderedSegment>> segments; // rendered ones only, sorted by begin
};

struct PlaybackMap
{
    std::vector<TakePlayback> takes;
    std::uint64_t generation = 0; // edit generation this map fully reflects (0 while still rendering)
};

/**
 * Edited audio of `take` for output samples [from, from + n) into out[0..channels) (overwritten). `mix` 1 plays
 * the edit, 0 the recording. Samples outside the take are silent.
 */
inline void readTake(const TakePlayback& take, std::int64_t from, int n, float* const* out, int channels, float mix,
                     const FadeTable& fades) noexcept
{
    const auto& original = *take.original;
    const int sourceChannels = static_cast<int>(original.size());
    const auto& segments = take.segments;
    // First segment that may overlap [from, from + n).
    std::size_t first = static_cast<std::size_t>(
        std::lower_bound(segments.begin(), segments.end(), from,
                         [](const std::shared_ptr<const RenderedSegment>& s, std::int64_t t) { return s->end <= t; }) -
        segments.begin());
    for (int i = 0; i < n; ++i)
    {
        const std::int64_t t = from + i;
        if (t < 0 || t >= take.length || sourceChannels == 0)
        {
            for (int c = 0; c < channels; ++c)
                out[c][i] = 0.0f;
            continue;
        }
        while (first < segments.size() && segments[first]->end <= t)
            ++first;
        float edited[2] = {0.0f, 0.0f};
        float covered = 0.0f;
        for (std::size_t s = first; s < segments.size() && segments[s]->begin <= t; ++s)
        {
            const auto& segment = *segments[s];
            if (t >= segment.end)
                continue;
            const auto index = static_cast<std::size_t>(t - segment.begin);
            for (int c = 0; c < sourceChannels && c < 2; ++c)
                edited[c] += segment.audio[static_cast<std::size_t>(c)][index];
            covered += segmentWeight(segment, t, fades);
        }
        const float rest = 1.0f - std::min(covered, 1.0f);
        for (int c = 0; c < channels; ++c)
        {
            const int sc = std::min(c, sourceChannels - 1);
            const float dry = original[static_cast<std::size_t>(sc)][static_cast<std::size_t>(t)];
            const float wet = edited[sc] + dry * rest;
            out[c][i] = dry + mix * (wet - dry);
        }
    }
}
} // namespace whale::dsp
