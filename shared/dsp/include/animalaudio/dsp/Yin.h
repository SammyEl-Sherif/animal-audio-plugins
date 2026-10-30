#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
 * Building blocks of YIN (de Cheveigné & Kawahara, JASA 111(4), 2002), shared by the streaming PitchTracker and the
 * offline ProbabilisticYin: the anti-alias low-pass used before decimating, the difference function, its
 * cumulative-mean normalisation and parabolic refinement of a dip.
 */
namespace animalaudio::dsp::yin
{
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    static Biquad lowpass(double sampleRate, double cutoff, double q)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * cutoff / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cosw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = (1.0 - cosw) * 0.5 / a0;
        b.b1 = (1.0 - cosw) / a0;
        b.b2 = b.b0;
        b.a1 = -2.0 * cosw / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    double process(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/** Fourth-order Butterworth low-pass (two biquads) that keeps a decimated copy free of aliases. */
inline std::array<Biquad, 2> antiAliasLowpass(double sampleRate, double cutoff)
{
    return {Biquad::lowpass(sampleRate, cutoff, 0.54119610), Biquad::lowpass(sampleRate, cutoff, 1.30656296)};
}

/** Offset of a parabola's extremum through three equally spaced values, in [-0.5, 0.5]. */
inline float parabolicOffset(float left, float mid, float right) noexcept
{
    const float denominator = left - 2.0f * mid + right;
    if (denominator <= 1.0e-12f)
        return 0.0f;
    return std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);
}

/** d(tau) = sum over `window` samples of (x[j] - x[j + tau])^2, for tau = 1 .. maxLag + 1 (x needs window + maxLag
    + 1 samples; out needs maxLag + 2). */
inline void difference(const float* x, int window, int maxLag, float* out) noexcept
{
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < window; ++j)
        {
            const float delta = x[j] - x[j + tau];
            sum += delta * delta;
        }
        out[tau] = sum;
    }
}

/** Cumulative-mean-normalised difference d'(tau): 1 at tau = 0, dips towards 0 at the period. */
inline void normalise(const float* difference, int maxLag, float* out) noexcept
{
    out[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        running += difference[tau];
        out[tau] = running > 0.0 ? static_cast<float>(static_cast<double>(difference[tau]) * tau / running) : 1.0f;
    }
}

/** Depth of the dip at `tau`: the minimum of the parabola through its neighbours, so a period that falls between
    two lags is not judged shallower than one that lands on a lag. */
inline float dipDepth(const float* normalised, int tau) noexcept
{
    const float left = normalised[tau - 1];
    const float mid = normalised[tau];
    const float right = normalised[tau + 1];
    const float curvature = left - 2.0f * mid + right;
    if (curvature <= 1.0e-12f)
        return mid;
    return std::max(0.0f, mid - 0.125f * (left - right) * (left - right) / curvature);
}
} // namespace animalaudio::dsp::yin
