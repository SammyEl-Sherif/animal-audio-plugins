#pragma once

#include "Yin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace animalaudio::dsp
{
/**
 * Monophonic pitch tracker: YIN (de Cheveigné & Kawahara, JASA 111(4), 2002) on a low-passed copy of the input
 * decimated to ~12 kHz, then refined at the full rate around the coarse lag so the period is accurate to a few
 * cents. Steps: difference function, cumulative-mean normalisation, absolute threshold (first dip below it, then
 * its local minimum), parabolic interpolation; a median of three removes single-frame octave slips.
 *
 * push() one sample at a time; every hop (~2.7 ms) it returns true and latest() holds a new estimate whose window
 * is centred `getCentreLag()` samples before the newest input. prepare() allocates; push() does not.
 */
class PitchTracker
{
public:
    struct Estimate
    {
        std::int64_t centre = 0;   // input sample index the analysis window is centred on
        float period = 0.0f;       // input samples; meaningful when voiced
        float aperiodicity = 1.0f; // YIN d'(tau) at the chosen lag: 0 = perfectly periodic
        bool voiced = false;
    };

    static constexpr float kDipThreshold = 0.15f;
    static constexpr float kVoicedOn = 0.25f;
    static constexpr float kVoicedOff = 0.35f;
    static constexpr float kGateRms = 0.002f;
    static constexpr float kHarmonicCheckFloor = 0.03f; // first dips shallower than this are trusted

    void prepare(double sampleRate, float minHz = 75.0f, float maxHz = 1000.0f)
    {
        decimation = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.0)));
        const double coarseRate = sampleRate / decimation;
        maxLag = static_cast<int>(std::ceil(coarseRate / minHz));
        minLag = std::max(2, static_cast<int>(std::floor(coarseRate / maxHz)));
        window = maxLag;
        hop = std::max(1, static_cast<int>(std::lround(coarseRate * 0.0027)));
        span = window + maxLag + 2;

        coarse.assign(nextPowerOfTwo(span + 4), 0.0f);
        fine.assign(nextPowerOfTwo((span + 4) * decimation), 0.0f);
        linear.assign(static_cast<std::size_t>((span + 4) * decimation), 0.0f);
        difference.assign(static_cast<std::size_t>(maxLag + 2), 0.0f);
        normalised.assign(static_cast<std::size_t>(maxLag + 2), 1.0f);

        const double cutoff = std::min(0.25 * coarseRate, 0.45 * sampleRate);
        sections = yin::antiAliasLowpass(sampleRate, cutoff);
        reset();
    }

    void reset() noexcept
    {
        std::fill(coarse.begin(), coarse.end(), 0.0f);
        std::fill(fine.begin(), fine.end(), 0.0f);
        for (auto& s : sections)
            s.z1 = s.z2 = 0.0;
        written = 0;
        coarseWritten = 0;
        phase = 0;
        sinceHop = 0;
        current = {};
        recentCount = 0;
    }

    bool push(float sample) noexcept
    {
        double x = sample;
        for (auto& s : sections)
            x = s.process(x);
        const auto filtered = static_cast<float>(x);
        fine[static_cast<std::size_t>(written & static_cast<std::int64_t>(fine.size() - 1))] = filtered;
        ++written;
        if (++phase < decimation)
            return false;
        phase = 0;
        coarse[static_cast<std::size_t>(coarseWritten & static_cast<std::int64_t>(coarse.size() - 1))] = filtered;
        ++coarseWritten;
        if (++sinceHop < hop || coarseWritten < span)
            return false;
        sinceHop = 0;
        analyse();
        return true;
    }

    const Estimate& latest() const noexcept { return current; }

    /** Longest period searched, in input samples. */
    int getMaxPeriod() const noexcept { return maxLag * decimation; }
    /** Input samples between estimates. */
    int getHop() const noexcept { return hop * decimation; }
    /** How far behind the newest input sample an estimate's window centre is, when it is produced. */
    int getCentreLag() const noexcept { return (span * decimation) / 2 + decimation; }

private:
    static std::size_t nextPowerOfTwo(int n)
    {
        std::size_t size = 1;
        while (size < static_cast<std::size_t>(n))
            size <<= 1;
        return size;
    }

    void analyse() noexcept
    {
        const std::int64_t start = coarseWritten - span;
        const auto coarseMask = static_cast<std::int64_t>(coarse.size() - 1);
        double energy = 0.0;
        for (int i = 0; i < span; ++i)
        {
            const float v = coarse[static_cast<std::size_t>((start + i) & coarseMask)];
            linear[static_cast<std::size_t>(i)] = v;
            energy += static_cast<double>(v) * v;
        }
        const float rms = static_cast<float>(std::sqrt(energy / span));

        yin::difference(linear.data(), window, maxLag, difference.data());
        yin::normalise(difference.data(), maxLag, normalised.data());

        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (normalised[static_cast<std::size_t>(tau)] < kDipThreshold)
            {
                while (tau + 1 <= maxLag &&
                       normalised[static_cast<std::size_t>(tau + 1)] < normalised[static_cast<std::size_t>(tau)])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            best = minLag;
            for (int tau = minLag + 1; tau <= maxLag; ++tau)
                if (normalised[static_cast<std::size_t>(tau)] < normalised[static_cast<std::size_t>(best)])
                    best = tau;
        }
        else
        {
            best = periodBehindHarmonic(best);
        }

        const float aperiodicity = normalised[static_cast<std::size_t>(best)];
        const float coarseLag =
            static_cast<float>(best) + yin::parabolicOffset(normalised[static_cast<std::size_t>(best - 1)],
                                                            normalised[static_cast<std::size_t>(best)],
                                                            normalised[static_cast<std::size_t>(best + 1)]);
        const std::int64_t centre = (start + span / 2) * decimation + decimation - 1;
        float period = coarseLag * static_cast<float>(decimation);
        if (decimation > 1)
            period = refine(period, centre);

        const bool voiced = rms > kGateRms && aperiodicity < (current.voiced ? kVoicedOff : kVoicedOn);
        if (voiced)
        {
            recent[static_cast<std::size_t>(recentCount % 3)] = period;
            ++recentCount;
            if (recentCount >= 3)
            {
                auto sorted = recent;
                std::sort(sorted.begin(), sorted.end());
                period = sorted[1];
            }
        }
        else
        {
            recentCount = 0;
        }

        current.centre = centre;
        current.period = period;
        current.aperiodicity = aperiodicity;
        current.voiced = voiced;
    }

    float dipDepth(int tau) const noexcept { return yin::dipDepth(normalised.data(), tau); }

    /**
     * The first dip under the threshold can be a harmonic's period when the true period's dip is shallower than
     * usual: a fast glide smears it, and a harmonic sitting on a formant dominates the waveform. A multiple of
     * the true period is never much deeper than the period itself, so a multiple of `lag` whose dip is less than
     * half as deep is the period (the smallest such multiple, so a true octave jump is never undone).
     */
    int periodBehindHarmonic(int lag) const noexcept
    {
        const float depth = dipDepth(lag);
        if (depth < kHarmonicCheckFloor)
            return lag;
        for (int k = 2; k * lag + k < maxLag; ++k)
        {
            int deepest = k * lag;
            for (int tau = std::max(minLag, k * lag - k); tau <= k * lag + k; ++tau)
                if (normalised[static_cast<std::size_t>(tau)] < normalised[static_cast<std::size_t>(deepest)])
                    deepest = tau;
            if (dipDepth(deepest) < 0.5f * depth)
                return deepest;
        }
        return lag;
    }

    /** Full-rate difference function over +-decimation lags around `guess`, on a window centred at `centre`. */
    float refine(float guess, std::int64_t centre) noexcept
    {
        const int low = std::max(2, static_cast<int>(std::floor(guess)) - decimation);
        const int high = static_cast<int>(std::ceil(guess)) + decimation;
        const int length = window * decimation;
        const int needed = length + high + 1;
        if (needed > static_cast<int>(linear.size()))
            return guess;
        const std::int64_t first = std::min(centre - needed / 2, written - needed);
        const auto fineMask = static_cast<std::int64_t>(fine.size() - 1);
        for (int i = 0; i < needed; ++i)
            linear[static_cast<std::size_t>(i)] = fine[static_cast<std::size_t>((first + i) & fineMask)];

        float values[64] = {};
        const int lags = std::min(high - low + 1, 64);
        int best = 0;
        for (int k = 0; k < lags; ++k)
        {
            const int tau = low + k;
            float sum = 0.0f;
            for (int j = 0; j < length; ++j)
            {
                const float delta = linear[static_cast<std::size_t>(j)] - linear[static_cast<std::size_t>(j + tau)];
                sum += delta * delta;
            }
            values[k] = sum;
            if (sum < values[best])
                best = k;
        }
        if (best == 0 || best == lags - 1)
            return guess;
        return static_cast<float>(low + best) + yin::parabolicOffset(values[best - 1], values[best], values[best + 1]);
    }

    int decimation = 4;
    int minLag = 12;
    int maxLag = 160;
    int window = 160;
    int hop = 32;
    int span = 322;

    std::vector<float> coarse;
    std::vector<float> fine;
    std::vector<float> linear;
    std::vector<float> difference;
    std::vector<float> normalised;
    std::array<yin::Biquad, 2> sections{};

    std::int64_t written = 0;
    std::int64_t coarseWritten = 0;
    int phase = 0;
    int sinceHop = 0;
    Estimate current;
    std::array<float, 3> recent{};
    int recentCount = 0;
};
} // namespace animalaudio::dsp
