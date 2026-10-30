#pragma once

#include <algorithm>
#include <vector>

namespace animalaudio::dsp
{
/**
 * Splits a sequence into segments of constant mean, optimally: minimises the weighted squared error of each
 * segment around its weighted mean plus `penalty` per segment, with every segment at least `minLength` long.
 * Exact, in roughly linear time, with PELT pruning (Killick, Fearnhead & Eckley, "Optimal detection of
 * changepoints with a linear computational cost", JASA 107(500), 2012).
 *
 * Returns the start index of every segment (the first is always 0). Weights must be >= 0; a zero weight makes a
 * sample free to belong to either side (use it for glides between notes).
 */
inline std::vector<int> segmentConstantMean(const std::vector<float>& values, const std::vector<float>& weights,
                                            double penalty, int minLength)
{
    const int n = static_cast<int>(values.size());
    minLength = std::max(1, minLength);
    if (n < 2 * minLength)
        return {0};

    std::vector<double> sw(static_cast<std::size_t>(n + 1), 0.0);
    std::vector<double> swx(static_cast<std::size_t>(n + 1), 0.0);
    std::vector<double> swxx(static_cast<std::size_t>(n + 1), 0.0);
    for (int i = 0; i < n; ++i)
    {
        const double w = weights[static_cast<std::size_t>(i)];
        const double x = values[static_cast<std::size_t>(i)];
        sw[static_cast<std::size_t>(i + 1)] = sw[static_cast<std::size_t>(i)] + w;
        swx[static_cast<std::size_t>(i + 1)] = swx[static_cast<std::size_t>(i)] + w * x;
        swxx[static_cast<std::size_t>(i + 1)] = swxx[static_cast<std::size_t>(i)] + w * x * x;
    }
    const auto cost = [&](int s, int t)
    {
        const double w = sw[static_cast<std::size_t>(t)] - sw[static_cast<std::size_t>(s)];
        const double wx = swx[static_cast<std::size_t>(t)] - swx[static_cast<std::size_t>(s)];
        const double wxx = swxx[static_cast<std::size_t>(t)] - swxx[static_cast<std::size_t>(s)];
        return w > 1.0e-12 ? std::max(0.0, wxx - wx * wx / w) : 0.0;
    };

    std::vector<double> best(static_cast<std::size_t>(n + 1), 0.0);
    std::vector<int> previous(static_cast<std::size_t>(n + 1), 0);
    best[0] = -penalty;
    std::vector<int> candidates; // segment starts still worth considering
    std::vector<int> kept;
    for (int t = minLength; t <= n; ++t)
    {
        // Starts become eligible once a segment from them to t would be long enough.
        const int newest = t - minLength;
        if (newest == 0 || newest >= minLength)
            candidates.push_back(newest);
        double value = 1.0e300;
        int from = 0;
        for (const int s : candidates)
        {
            const double v = best[static_cast<std::size_t>(s)] + cost(s, t) + penalty;
            if (v < value)
            {
                value = v;
                from = s;
            }
        }
        best[static_cast<std::size_t>(t)] = value;
        previous[static_cast<std::size_t>(t)] = from;
        kept.clear();
        for (const int s : candidates)
            if (best[static_cast<std::size_t>(s)] + cost(s, t) <= value)
                kept.push_back(s);
        candidates.swap(kept);
    }

    std::vector<int> starts;
    for (int t = n; t > 0; t = previous[static_cast<std::size_t>(t)])
        starts.push_back(previous[static_cast<std::size_t>(t)]);
    std::reverse(starts.begin(), starts.end());
    return starts;
}
} // namespace animalaudio::dsp
