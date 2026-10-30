#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
 * Pieces of TD-PSOLA (Moulines & Charpentier, Speech Communication 9, 1990) shared by the streaming and the offline
 * pitch shifters: placing pitch marks one period apart, phase locked to the waveform, and the asymmetric Hann grain
 * that reaches from a mark to its neighbours.
 *
 * `Signal` is any callable `float(std::int64_t index)` (a ring buffer, or a plain array with zeros outside it).
 */
namespace animalaudio::dsp::psola
{
/**
 * Position of the mark one `period` after `previous`, near `guess`: the lag whose segment best matches the segment
 * before `previous` (normalised cross-correlation, +-1/8 period), refined with a parabola. Whole-sample segments are
 * compared and the previous mark's fraction is carried over, so rounding never accumulates from period to period.
 */
template <class Signal>
double alignToPrevious(const Signal& at, double previous, double guess, float period) noexcept
{
    const int length = std::max(2, static_cast<int>(std::lround(period)));
    const int radius = std::max(1, static_cast<int>(std::lround(period * 0.125f)));
    const std::int64_t anchor = std::llround(previous);
    const double fraction = previous - static_cast<double>(anchor);
    const std::int64_t centre = std::llround(guess - fraction);
    const std::int64_t low = std::max<std::int64_t>(centre - radius, anchor + length / 2);
    const std::int64_t high = centre + radius;
    if (low > high)
        return guess;

    double energy = 0.0;
    for (std::int64_t k = low - length; k < low; ++k)
        energy += static_cast<double>(at(k)) * at(k);

    std::int64_t best = centre;
    double bestScore = -1.0e300;
    double beforeBest = 0.0;
    double afterBest = 0.0;
    double previousScore = 0.0;
    bool captureNext = false;
    for (std::int64_t candidate = low; candidate <= high; ++candidate)
    {
        double dot = 0.0;
        for (int k = 0; k < length; ++k)
            dot += static_cast<double>(at(anchor - length + k)) * at(candidate - length + k);
        const double score = dot / std::sqrt(energy + 1.0e-12);
        if (captureNext)
        {
            afterBest = score;
            captureNext = false;
        }
        if (score > bestScore)
        {
            bestScore = score;
            best = candidate;
            beforeBest = candidate > low ? previousScore : score;
            afterBest = score;
            captureNext = true;
        }
        previousScore = score;
        const double incoming = at(candidate);
        const double outgoing = at(candidate - length);
        energy = std::max(0.0, energy + incoming * incoming - outgoing * outgoing);
    }
    const double curvature = beforeBest - 2.0 * bestScore + afterBest;
    const double offset = curvature < 0.0 ? std::clamp(0.5 * (beforeBest - afterBest) / curvature, -0.5, 0.5) : 0.0;
    return static_cast<double>(best) + offset + fraction;
}

/** First mark of a voiced stretch: the largest |sample| within half a period of `guess`, not before `notBefore`. */
template <class Signal>
double strongestNear(const Signal& at, double guess, float period, double notBefore) noexcept
{
    const int half = std::max(1, static_cast<int>(std::lround(period * 0.5f)));
    const std::int64_t low = std::max(std::llround(guess) - half, static_cast<std::int64_t>(std::ceil(notBefore)));
    const std::int64_t high = std::llround(guess) + half;
    std::int64_t best = std::max(std::llround(guess), low);
    float bestLevel = -1.0f;
    for (std::int64_t i = low; i <= high; ++i)
    {
        const float level = std::abs(at(i));
        if (level > bestLevel)
        {
            bestLevel = level;
            best = i;
        }
    }
    return static_cast<double>(best);
}

/**
 * A grain: input around `source` under a Hann window, placed at output time `centre` and read `beta` times faster
 * (beta is the formant ratio). The window reaches 1 / invLeft output samples back and 1 / invRight forward, so
 * windows reaching to the neighbouring marks overlap-add to exactly one at 1x.
 */
struct Grain
{
    double source = 0.0;
    double centre = 0.0;
    float beta = 1.0f;
    float invLeft = 1.0f;
    float invRight = 1.0f;
    float gain = 1.0f;
};

/** Half a Hann window, tabulated: value(u) for |u| < 1 is 0.5 (1 + cos(pi u)). */
class HannTable
{
public:
    static constexpr int kSize = 1024;

    HannTable()
    {
        for (int i = 0; i < kSize + 2; ++i)
        {
            const double u = std::min(i, kSize) / static_cast<double>(kSize);
            values[static_cast<std::size_t>(i)] =
                static_cast<float>(0.5 * (1.0 + std::cos(3.14159265358979323846 * u)));
        }
    }

    /** `u` in (-1, 1). */
    float value(float u) const noexcept
    {
        const float index = std::abs(u) * kSize;
        const int whole = static_cast<int>(index);
        const float frac = index - static_cast<float>(whole);
        return values[static_cast<std::size_t>(whole)] +
               frac * (values[static_cast<std::size_t>(whole + 1)] - values[static_cast<std::size_t>(whole)]);
    }

    /** Window position of output time `centre + dt` within `grain`: |u| >= 1 is outside. */
    static float position(const Grain& grain, double dt) noexcept
    {
        return static_cast<float>(dt) * (dt < 0.0 ? grain.invLeft : grain.invRight);
    }

private:
    std::array<float, kSize + 2> values{};
};
} // namespace animalaudio::dsp::psola
