#pragma once

// JUCE-free signal measurements for numeric DSP tests (pitch, level, harmonics). Header only.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace animalaudio::testing
{
inline constexpr double kPi = 3.14159265358979323846;

inline std::vector<float> sineWave(double hz, double sampleRate, std::size_t count, float amplitude = 0.5f)
{
    std::vector<float> out(count);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / sampleRate));
    return out;
}

inline std::vector<float> whiteNoise(std::size_t count, float amplitude, std::uint32_t seed = 1)
{
    std::vector<float> out(count);
    std::uint32_t state = seed;
    for (auto& v : out)
    {
        state = state * 1664525u + 1013904223u;
        v = amplitude * (static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f);
    }
    return out;
}

inline double rms(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return end > begin ? std::sqrt(sum / static_cast<double>(end - begin)) : 0.0;
}

/** Amplitude of the component at `hz` (Hann-windowed DFT bin, scaled so a unit sine reads 1). */
inline double toneAmplitude(const std::vector<float>& x, std::size_t begin, std::size_t count, double hz,
                            double sampleRate)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    double re = 0.0;
    double im = 0.0;
    double windowSum = 0.0;
    for (std::size_t n = 0; n < count; ++n)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(count));
        const double v = x[begin + n] * window;
        re += v * std::cos(w * static_cast<double>(n));
        im -= v * std::sin(w * static_cast<double>(n));
        windowSum += window;
    }
    return 2.0 * std::sqrt(re * re + im * im) / windowSum;
}

/** Frequency of the strongest component between lowHz and highHz (scan in cent steps, parabolic refinement). */
inline double peakFrequency(const std::vector<float>& x, std::size_t begin, std::size_t count, double sampleRate,
                            double lowHz, double highHz, double stepCents = 2.0)
{
    const double ratio = std::pow(2.0, stepCents / 1200.0);
    std::vector<double> freqs;
    std::vector<double> mags;
    for (double f = lowHz; f <= highHz; f *= ratio)
    {
        freqs.push_back(f);
        mags.push_back(toneAmplitude(x, begin, count, f, sampleRate));
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < mags.size(); ++i)
        if (mags[i] > mags[best])
            best = i;
    if (best == 0 || best + 1 >= mags.size())
        return freqs[best];
    const double a = mags[best - 1];
    const double b = mags[best];
    const double c = mags[best + 1];
    const double denominator = a - 2.0 * b + c;
    const double offset = denominator < 0.0 ? 0.5 * (a - c) / denominator : 0.0;
    return freqs[best] * std::pow(ratio, offset);
}

inline double centsBetween(double hz, double referenceHz)
{
    return 1200.0 * std::log2(hz / referenceHz);
}

/** Largest sample-to-sample step, a click detector. */
inline double largestStep(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    double largest = 0.0;
    for (std::size_t i = begin + 1; i < end; ++i)
        largest = std::max(largest, static_cast<double>(std::abs(x[i] - x[i - 1])));
    return largest;
}
} // namespace animalaudio::testing
