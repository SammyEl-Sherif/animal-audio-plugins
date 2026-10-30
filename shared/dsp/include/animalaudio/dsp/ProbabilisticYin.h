#pragma once

#include "Yin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace animalaudio::dsp
{
/**
 * Offline pitch track of a monophonic recording: pYIN (Mauch & Dixon, "PYIN: a fundamental frequency estimator
 * using probabilistic threshold distributions", ICASSP 2014).
 *
 * 1. YIN on a low-passed copy decimated to ~12 kHz (zero-phase, so frames are not delayed). Instead of one
 *    threshold, 100 thresholds weighted by a Beta(2, 18) prior each pick the first dip under them; a dip's
 *    probability is the prior mass of the thresholds that chose it (the global minimum keeps 1% of the mass of the
 *    thresholds no dip got under). The sum is the frame's voicing probability.
 * 2. A hidden Markov model over 20-cent pitch bins, each voiced or unvoiced, is Viterbi-decoded: pitch moves at
 *    most +-1.6 semitones per frame (triangular prior), voicing switches with probability 0.01 per frame. This
 *    removes octave slips and isolated voicing errors that single-frame YIN makes.
 * 3. The chosen candidate is refined at the full rate (difference function around it, parabola), so the pitch is
 *    accurate to a few cents.
 *
 * Frames are ~5.8 ms apart (the hop is a whole number of decimated samples). analyse() allocates; it is meant for
 * a background thread. `progress` (0..1) may return false to cancel; the track is then empty.
 */
class ProbabilisticYin
{
public:
    struct Settings
    {
        float minHz = 60.0f;
        float maxHz = 1100.0f;
        double hopSeconds = 0.0058;
        float gateDb = -60.0f; // frames quieter than this (RMS, dBFS) are unvoiced
    };

    struct Frame
    {
        double centre = 0.0;   // input sample index the frame is centred on
        float hz = 0.0f;       // 0 when unvoiced
        float voicing = 0.0f;  // probability mass of all pitch candidates, 0..1
        float rmsDb = -120.0f; // level over ~20 ms around the centre
    };

    struct Track
    {
        double rate = 0.0;
        int hop = 0; // input samples between frames; frame i is centred on i * hop
        std::vector<Frame> frames;
    };

    static Track analyse(const float* input, std::int64_t length, double rate)
    {
        return analyse(input, length, rate, Settings(), {});
    }

    static Track analyse(const float* input, std::int64_t length, double rate, const Settings& settings,
                         const std::function<bool(float)>& progress = {})
    {
        ProbabilisticYin yin(rate, settings);
        return yin.run(input, length, progress);
    }

private:
    static constexpr int kThresholds = 100;
    static constexpr float kFallbackShare = 0.01f;
    static constexpr float kYinTrust = 0.5f;
    static constexpr float kStayVoicing = 0.99f;
    static constexpr int kBinsPerSemitone = 5;
    static constexpr int kJumpBins = 8;

    struct Candidate
    {
        float lag = 0.0f; // decimated samples
        float probability = 0.0f;
    };

    ProbabilisticYin(double sampleRate, const Settings& s)
        : rate(sampleRate)
        , settings(s)
    {
        decimation = std::max(1, static_cast<int>(std::lround(rate / 12000.0)));
        coarseRate = rate / decimation;
        maxLag = static_cast<int>(std::ceil(coarseRate / settings.minHz));
        minLag = std::max(2, static_cast<int>(std::floor(coarseRate / settings.maxHz)));
        window = maxLag;
        coarseHop = std::max(1, static_cast<int>(std::lround(coarseRate * settings.hopSeconds)));
        hop = coarseHop * decimation;
        bins = static_cast<int>(std::ceil(12.0 * kBinsPerSemitone * std::log2(settings.maxHz / settings.minHz))) + 1;

        // Beta(2, 18) over thresholds 0.01 .. 1.00, normalised to sum to one.
        double total = 0.0;
        for (int k = 0; k < kThresholds; ++k)
        {
            const double threshold = (k + 1) / static_cast<double>(kThresholds);
            const double p = threshold * std::pow(1.0 - threshold, 17.0);
            thresholdPrior[static_cast<std::size_t>(k)] = p;
            total += p;
        }
        for (auto& p : thresholdPrior)
            p /= total;

        // Triangular pitch-jump prior, normalised.
        double sum = 0.0;
        for (int d = -kJumpBins; d <= kJumpBins; ++d)
            sum += kJumpBins + 1 - std::abs(d);
        for (int d = -kJumpBins; d <= kJumpBins; ++d)
            jumpPrior[static_cast<std::size_t>(d + kJumpBins)] =
                static_cast<float>((kJumpBins + 1 - std::abs(d)) / sum);
    }

    Track run(const float* input, std::int64_t length, const std::function<bool(float)>& progress)
    {
        Track track;
        track.rate = rate;
        track.hop = hop;
        if (length <= 0)
            return track;

        // Zero-phase anti-alias filter, then decimate.
        std::vector<float> filtered(input, input + length);
        const double cutoff = std::min(0.25 * coarseRate, 0.45 * rate);
        for (int pass = 0; pass < 2; ++pass)
        {
            auto sections = yin::antiAliasLowpass(rate, cutoff);
            if (pass == 0)
            {
                for (auto& v : filtered)
                {
                    double x = v;
                    for (auto& s : sections)
                        x = s.process(x);
                    v = static_cast<float>(x);
                }
            }
            else
            {
                for (auto it = filtered.rbegin(); it != filtered.rend(); ++it)
                {
                    double x = *it;
                    for (auto& s : sections)
                        x = s.process(x);
                    *it = static_cast<float>(x);
                }
            }
        }
        std::vector<float> coarse(static_cast<std::size_t>((length + decimation - 1) / decimation));
        for (std::size_t i = 0; i < coarse.size(); ++i)
            coarse[i] = filtered[i * static_cast<std::size_t>(decimation)];
        source = std::move(filtered);

        const auto frameCount = static_cast<std::size_t>(length / hop + 1);
        track.frames.resize(frameCount);
        std::vector<std::vector<Candidate>> candidates(frameCount);

        const int span = window + maxLag + 2;
        std::vector<float> segment(static_cast<std::size_t>(span));
        std::vector<float> difference(static_cast<std::size_t>(maxLag + 2));
        std::vector<float> normalised(static_cast<std::size_t>(maxLag + 2));
        const auto rmsHalf = static_cast<std::int64_t>(0.01 * rate);

        for (std::size_t f = 0; f < frameCount; ++f)
        {
            if (progress && (f & 1023) == 0 && !progress(0.8f * static_cast<float>(f) / static_cast<float>(frameCount)))
                return {};
            auto& frame = track.frames[f];
            const std::int64_t centre = static_cast<std::int64_t>(f) * hop;
            frame.centre = static_cast<double>(centre);

            double energy = 0.0;
            const std::int64_t r0 = std::max<std::int64_t>(0, centre - rmsHalf);
            const std::int64_t r1 = std::min<std::int64_t>(length, centre + rmsHalf);
            for (std::int64_t i = r0; i < r1; ++i)
                energy += static_cast<double>(input[i]) * input[i];
            const double meanSquare = energy / static_cast<double>(std::max<std::int64_t>(1, 2 * rmsHalf));
            frame.rmsDb = static_cast<float>(10.0 * std::log10(meanSquare + 1.0e-12));
            if (frame.rmsDb < settings.gateDb)
                continue;

            const std::int64_t start = centre / decimation - (window + maxLag) / 2;
            for (int i = 0; i < span; ++i)
            {
                const std::int64_t k = start + i;
                segment[static_cast<std::size_t>(i)] =
                    k >= 0 && k < static_cast<std::int64_t>(coarse.size()) ? coarse[static_cast<std::size_t>(k)] : 0.0f;
            }
            yin::difference(segment.data(), window, maxLag, difference.data());
            yin::normalise(difference.data(), maxLag, normalised.data());
            frame.voicing = findCandidates(normalised, candidates[f]);
        }

        decode(track, candidates, progress);
        if (progress && !progress(1.0f))
            return {};
        return track;
    }

    /** pYIN's threshold-distribution step. Returns the voicing probability (sum of the candidates' masses). */
    float findCandidates(const std::vector<float>& n, std::vector<Candidate>& out) const
    {
        out.clear();
        float previousLowest = 2.0f;
        int globalMin = minLag;
        double total = 0.0;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            const float v = n[static_cast<std::size_t>(tau)];
            if (v < n[static_cast<std::size_t>(globalMin)])
                globalMin = tau;
            const bool isMinimum =
                v < n[static_cast<std::size_t>(tau - 1)] && v <= n[static_cast<std::size_t>(tau + 1)];
            if (!isMinimum || v >= previousLowest)
                continue;
            // Thresholds in (v, previousLowest] pick this dip first.
            double mass = 0.0;
            for (int k = 0; k < kThresholds; ++k)
            {
                const float s = static_cast<float>(k + 1) / kThresholds;
                if (s > v && s <= previousLowest)
                    mass += thresholdPrior[static_cast<std::size_t>(k)];
            }
            previousLowest = v;
            if (mass <= 0.0)
                continue;
            Candidate c;
            c.lag = static_cast<float>(tau) +
                    yin::parabolicOffset(n[static_cast<std::size_t>(tau - 1)], v, n[static_cast<std::size_t>(tau + 1)]);
            c.probability = static_cast<float>(mass);
            out.push_back(c);
            total += mass;
        }
        // Thresholds below every dip: the global minimum, with a small share.
        double none = 0.0;
        for (int k = 0; k < kThresholds; ++k)
            if (static_cast<float>(k + 1) / kThresholds <=
                std::min(previousLowest, n[static_cast<std::size_t>(globalMin)]))
                none += thresholdPrior[static_cast<std::size_t>(k)];
        if (none > 0.0 && globalMin > minLag && globalMin < maxLag)
        {
            const auto g = static_cast<std::size_t>(globalMin);
            const float lag = static_cast<float>(globalMin) + yin::parabolicOffset(n[g - 1], n[g], n[g + 1]);
            bool merged = false;
            for (auto& c : out)
                if (std::abs(c.lag - lag) < 0.5f)
                {
                    c.probability += static_cast<float>(none * kFallbackShare);
                    merged = true;
                }
            if (!merged)
                out.push_back({lag, static_cast<float>(none * kFallbackShare)});
            total += none * kFallbackShare;
        }
        return static_cast<float>(std::min(1.0, total));
    }

    int binOf(float lag) const noexcept
    {
        const double hz = coarseRate / lag;
        return static_cast<int>(std::lround(12.0 * kBinsPerSemitone * std::log2(hz / settings.minHz)));
    }

    /** Viterbi over voiced/unvoiced pitch bins, decoded region by region (regions split at long silences). */
    void decode(Track& track, const std::vector<std::vector<Candidate>>& candidates,
                const std::function<bool(float)>& progress)
    {
        const auto count = static_cast<int>(track.frames.size());
        int regionStart = 0;
        while (regionStart < count)
        {
            // A region ends after 40 frames (~230 ms) with no pitch candidates at all.
            int end = regionStart;
            int quiet = 0;
            while (end < count && quiet < 40)
            {
                quiet = candidates[static_cast<std::size_t>(end)].empty() ? quiet + 1 : 0;
                ++end;
            }
            decodeRegion(track, candidates, regionStart, end);
            regionStart = end;
            if (progress && !progress(0.8f + 0.15f * static_cast<float>(end) / static_cast<float>(count)))
                return;
        }
        refine(track, candidates);
    }

    void decodeRegion(Track& track, const std::vector<std::vector<Candidate>>& candidates, int begin, int end)
    {
        const int length = end - begin;
        bool any = false;
        for (int f = begin; f < end && !any; ++f)
            any = !candidates[static_cast<std::size_t>(f)].empty();
        if (!any || length <= 0)
            return;

        const int states = 2 * bins;
        std::vector<float> score(static_cast<std::size_t>(states), 1.0f / static_cast<float>(states));
        std::vector<float> next(static_cast<std::size_t>(states));
        std::vector<float> observation(static_cast<std::size_t>(states));
        std::vector<std::uint16_t> back(static_cast<std::size_t>(length) * static_cast<std::size_t>(states));

        for (int f = begin; f < end; ++f)
        {
            // Observation probabilities.
            const auto& cands = candidates[static_cast<std::size_t>(f)];
            std::fill(observation.begin(), observation.begin() + bins, 0.0f);
            float pitched = 0.0f;
            for (const auto& c : cands)
            {
                const int b = binOf(c.lag);
                if (b < 0 || b >= bins)
                    continue;
                observation[static_cast<std::size_t>(b)] += c.probability;
                pitched += c.probability;
            }
            const float reallyPitched = kYinTrust * std::min(1.0f, pitched);
            if (pitched > 0.0f)
                for (int b = 0; b < bins; ++b)
                    observation[static_cast<std::size_t>(b)] *= reallyPitched / pitched;
            std::fill(observation.begin() + bins, observation.end(), (1.0f - reallyPitched) / static_cast<float>(bins));

            // Transitions: pitch moves within +-kJumpBins, voicing switches rarely.
            const auto row = static_cast<std::size_t>(f - begin) * static_cast<std::size_t>(states);
            float largest = 0.0f;
            for (int to = 0; to < states; ++to)
            {
                const bool toVoiced = to < bins;
                const int toBin = toVoiced ? to : to - bins;
                float best = -1.0f;
                int bestFrom = to;
                for (int d = -kJumpBins; d <= kJumpBins; ++d)
                {
                    const int fromBin = toBin + d;
                    if (fromBin < 0 || fromBin >= bins)
                        continue;
                    const float jump = jumpPrior[static_cast<std::size_t>(d + kJumpBins)];
                    const float stay = score[static_cast<std::size_t>(toVoiced ? fromBin : fromBin + bins)];
                    const float flip = score[static_cast<std::size_t>(toVoiced ? fromBin + bins : fromBin)];
                    const float viaStay = stay * jump * kStayVoicing;
                    const float viaFlip = flip * jump * (1.0f - kStayVoicing);
                    if (viaStay > best)
                    {
                        best = viaStay;
                        bestFrom = toVoiced ? fromBin : fromBin + bins;
                    }
                    if (viaFlip > best)
                    {
                        best = viaFlip;
                        bestFrom = toVoiced ? fromBin + bins : fromBin;
                    }
                }
                const float value = best * observation[static_cast<std::size_t>(to)];
                next[static_cast<std::size_t>(to)] = value;
                back[row + static_cast<std::size_t>(to)] = static_cast<std::uint16_t>(bestFrom);
                largest = std::max(largest, value);
            }
            const float scale = largest > 0.0f ? 1.0f / largest : 1.0f;
            for (int s = 0; s < states; ++s)
                score[static_cast<std::size_t>(s)] = next[static_cast<std::size_t>(s)] * scale;
        }

        int state = static_cast<int>(std::max_element(score.begin(), score.end()) - score.begin());
        for (int f = end - 1; f >= begin; --f)
        {
            auto& frame = track.frames[static_cast<std::size_t>(f)];
            if (state < bins)
                frame.hz = static_cast<float>(settings.minHz * std::exp2(state / (12.0 * kBinsPerSemitone)));
            else
                frame.hz = 0.0f;
            state = back[static_cast<std::size_t>(f - begin) * static_cast<std::size_t>(states) +
                         static_cast<std::size_t>(state)];
        }
    }

    /** Replace each voiced frame's bin pitch with the nearest candidate, refined at the full rate. */
    void refine(Track& track, const std::vector<std::vector<Candidate>>& candidates) const
    {
        for (std::size_t f = 0; f < track.frames.size(); ++f)
        {
            auto& frame = track.frames[f];
            if (frame.hz <= 0.0f)
                continue;
            const double binLag = coarseRate / frame.hz;
            float lag = static_cast<float>(binLag);
            float bestDistance = 3.0f; // bins
            for (const auto& c : candidates[f])
            {
                const float distance =
                    static_cast<float>(std::abs(12.0 * kBinsPerSemitone * std::log2(binLag / c.lag)));
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    lag = c.lag;
                }
            }
            frame.hz = static_cast<float>(rate / refineLag(lag * static_cast<float>(decimation), frame.centre));
        }
    }

    /** Full-rate difference function over +-decimation lags around `guess` (input samples). */
    double refineLag(float guess, double centre) const
    {
        if (decimation <= 1)
            return guess;
        const int low = std::max(2, static_cast<int>(std::floor(guess)) - decimation);
        const int high = static_cast<int>(std::ceil(guess)) + decimation;
        const int length = window * decimation;
        const auto first = static_cast<std::int64_t>(centre) - (length + high) / 2;
        const auto at = [&](std::int64_t i)
        { return i >= 0 && i < static_cast<std::int64_t>(source.size()) ? source[static_cast<std::size_t>(i)] : 0.0f; };
        float values[64] = {};
        const int lags = std::min(high - low + 1, 64);
        int best = 0;
        for (int k = 0; k < lags; ++k)
        {
            const int tau = low + k;
            float sum = 0.0f;
            for (int j = 0; j < length; ++j)
            {
                const float delta = at(first + j) - at(first + j + tau);
                sum += delta * delta;
            }
            values[k] = sum;
            if (sum < values[best])
                best = k;
        }
        if (best == 0 || best == lags - 1)
            return guess;
        return static_cast<double>(low + best) + yin::parabolicOffset(values[best - 1], values[best], values[best + 1]);
    }

    double rate;
    Settings settings;
    int decimation = 4;
    double coarseRate = 12000.0;
    int maxLag = 200;
    int minLag = 10;
    int window = 200;
    int coarseHop = 70;
    int hop = 280;
    int bins = 250;
    std::vector<float> source; // full-rate low-passed signal, for refine()
    std::array<double, kThresholds> thresholdPrior{};
    std::array<float, 2 * kJumpBins + 1> jumpPrior{};
};
} // namespace animalaudio::dsp
