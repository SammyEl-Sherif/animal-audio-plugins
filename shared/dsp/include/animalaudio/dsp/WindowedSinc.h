#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace animalaudio::dsp
{
/** Zeroth-order modified Bessel function of the first kind (power series). */
inline double besselI0(double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 200; ++k)
    {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1.0e-15)
            break;
    }
    return sum;
}

/** Kaiser window at `position` in [-1, 1] (0 outside). `beta` trades main-lobe width for side-lobe level. */
inline double kaiserWindow(double position, double beta) noexcept
{
    const double r = 1.0 - position * position;
    if (r < 0.0)
        return 0.0;
    return besselI0(beta * std::sqrt(r)) / besselI0(beta);
}

inline double normalisedSinc(double x) noexcept
{
    if (std::abs(x) < 1.0e-12)
        return 1.0;
    const double px = 3.14159265358979323846 * x;
    return std::sin(px) / px;
}

/**
 * Linear-phase low-pass FIR (Kaiser-windowed sinc) with unity DC gain. `cutoff` is a fraction of the
 * sample rate (0..0.5). Use an odd `numTaps` for an integer group delay of (numTaps - 1) / 2.
 */
inline std::vector<double> designLowpass(int numTaps, double cutoff, double beta)
{
    std::vector<double> taps(static_cast<std::size_t>(numTaps));
    const double centre = 0.5 * static_cast<double>(numTaps - 1);
    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n)
    {
        const double t = static_cast<double>(n) - centre;
        const double window = centre > 0.0 ? kaiserWindow(t / centre, beta) : 1.0;
        const double tap = 2.0 * cutoff * normalisedSinc(2.0 * cutoff * t) * window;
        taps[static_cast<std::size_t>(n)] = tap;
        sum += tap;
    }
    for (auto& tap : taps)
        tap /= sum;
    return taps;
}

/**
 * Band-limited fractional reads from a power-of-two ring of samples, with a tabulated Kaiser-windowed sinc.
 * `stretch` >= 1 widens the kernel and lowers its cutoff by the same factor, for reads that sweep through the
 * history faster than 1x: what they would compress past Nyquist is filtered out instead of aliasing back.
 *
 * Weights are computed once per position (`prepareRead`) and applied to any number of channels (`apply`).
 */
class SincReader
{
public:
    static constexpr int kZeroCrossings = 6;
    static constexpr int kStepsPerCrossing = 512;
    static constexpr float kMaxStretch = 2.0f;
    static constexpr float kCutoff = 0.94f; // fraction of Nyquist kept at stretch 1
    static constexpr int kMaxTaps = 2 * (static_cast<int>(kZeroCrossings * kMaxStretch / kCutoff) + 2);

    SincReader()
    {
        constexpr int size = kZeroCrossings * kStepsPerCrossing + 2;
        table.resize(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i)
        {
            const double x = static_cast<double>(i) / kStepsPerCrossing;
            table[static_cast<std::size_t>(i)] =
                static_cast<float>(normalisedSinc(x) * kaiserWindow(x / kZeroCrossings, 7.0));
        }
    }

    struct Weights
    {
        std::int64_t first = 0; // absolute index of the first tap
        int count = 0;
        float values[kMaxTaps] = {};
    };

    void prepareRead(double position, float stretch, Weights& out) const noexcept
    {
        const double base = std::floor(position);
        const float frac = static_cast<float>(position - base);
        const float s = std::clamp(stretch, 1.0f, kMaxStretch);
        const float scale = kCutoff / s;
        const int reach = static_cast<int>(static_cast<float>(kZeroCrossings) / scale) + 1;
        out.first = static_cast<std::int64_t>(base) - reach + 1;
        out.count = 2 * reach;
        for (int k = 0; k < out.count; ++k)
        {
            const float distance = std::abs((static_cast<float>(k - reach + 1) - frac) * scale);
            out.values[k] = scale * kernel(distance);
        }
    }

    static float apply(const Weights& weights, const float* ring, std::int64_t mask) noexcept
    {
        float sum = 0.0f;
        for (int k = 0; k < weights.count; ++k)
            sum += weights.values[k] * ring[static_cast<std::size_t>((weights.first + k) & mask)];
        return sum;
    }

    /** Samples needed on either side of a read position at `stretch`. */
    static int reachFor(float stretch) noexcept
    {
        return static_cast<int>(static_cast<float>(kZeroCrossings) * std::clamp(stretch, 1.0f, kMaxStretch) / kCutoff) +
               1;
    }

private:
    float kernel(float distance) const noexcept
    {
        const float index = distance * kStepsPerCrossing;
        if (index >= static_cast<float>(kZeroCrossings * kStepsPerCrossing))
            return 0.0f;
        const int i = static_cast<int>(index);
        const float t = index - static_cast<float>(i);
        return table[static_cast<std::size_t>(i)] +
               t * (table[static_cast<std::size_t>(i + 1)] - table[static_cast<std::size_t>(i)]);
    }

    std::vector<float> table;
};
} // namespace animalaudio::dsp
