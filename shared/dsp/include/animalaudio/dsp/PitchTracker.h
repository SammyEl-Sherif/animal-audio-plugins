#pragma once

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
        sections[0] = Biquad::lowpass(sampleRate, cutoff, 0.54119610);
        sections[1] = Biquad::lowpass(sampleRate, cutoff, 1.30656296);
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

    static std::size_t nextPowerOfTwo(int n)
    {
        std::size_t size = 1;
        while (size < static_cast<std::size_t>(n))
            size <<= 1;
        return size;
    }

    static float parabolicOffset(float left, float mid, float right) noexcept
    {
        const float denominator = left - 2.0f * mid + right;
        if (denominator <= 1.0e-12f)
            return 0.0f;
        return std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);
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

        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float delta = linear[static_cast<std::size_t>(j)] - linear[static_cast<std::size_t>(j + tau)];
                sum += delta * delta;
            }
            difference[static_cast<std::size_t>(tau)] = sum;
        }
        normalised[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            running += difference[static_cast<std::size_t>(tau)];
            normalised[static_cast<std::size_t>(tau)] =
                running > 0.0
                    ? static_cast<float>(static_cast<double>(difference[static_cast<std::size_t>(tau)]) * tau / running)
                    : 1.0f;
        }

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

        const float aperiodicity = normalised[static_cast<std::size_t>(best)];
        const float coarseLag =
            static_cast<float>(best) + parabolicOffset(normalised[static_cast<std::size_t>(best - 1)],
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
        return static_cast<float>(low + best) + parabolicOffset(values[best - 1], values[best], values[best + 1]);
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
    std::array<Biquad, 2> sections{};

    std::int64_t written = 0;
    std::int64_t coarseWritten = 0;
    int phase = 0;
    int sinceHop = 0;
    Estimate current;
    std::array<float, 3> recent{};
    int recentCount = 0;
};
} // namespace animalaudio::dsp
