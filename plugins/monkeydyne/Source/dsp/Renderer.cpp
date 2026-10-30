#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace monkeydyne::dsp
{
namespace
{
constexpr double kUnvoicedGrainSeconds = 0.005;
constexpr double kLevelHopSeconds = 0.005;
constexpr double kLevelHalfWindowSeconds = 0.01;
constexpr double kLevelSmoothingSeconds = 0.02; // FIR half-width
constexpr float kLevelRange = 4.0f;

float jitter(double t) noexcept
{
    const auto h = static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::llround(std::abs(t))) * 2654435761ULL);
    return static_cast<float>(h >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
}

/** RMS over [from, to), counting samples outside the buffer as silence. */
double rms(const float* x, std::int64_t length, std::int64_t from, std::int64_t to) noexcept
{
    const auto window = static_cast<double>(std::max<std::int64_t>(to - from, 1));
    double sum = 0.0;
    for (auto i = std::max<std::int64_t>(from, 0); i < std::min(to, length); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / window);
}
} // namespace

bool Renderer::render(const SourceView& source, const TakeAnalysis& analysis, const EditPlan& plan, std::int64_t begin,
                      std::int64_t end, std::vector<std::vector<float>>& out, const std::atomic<bool>* cancel)
{
    const int channels = std::clamp(source.numChannels, 1, 2);
    out.assign(static_cast<std::size_t>(channels),
               std::vector<float>(static_cast<std::size_t>(std::max<std::int64_t>(end - begin, 0)), 0.0f));
    stats = {};
    if (end <= begin || source.length <= 0)
        return true;

    const double rate = analysis.rate;
    const double longestPeriod = rate / 60.0;
    const auto pad = static_cast<std::int64_t>(std::ceil(3.0 * longestPeriod + 0.04 * rate));
    const std::int64_t r0 = begin - pad;
    const std::int64_t r1 = end + pad;
    const auto count = static_cast<std::size_t>(r1 - r0);
    const double unvoicedStep = kUnvoicedGrainSeconds * rate;
    const auto& marks = analysis.marks;
    const auto markCount = static_cast<int>(marks.size());

    const auto voicedMark = [&](double s)
    {
        const int m = analysis.nearestMark(s);
        if (m < 0)
            return -1;
        const auto& mark = marks[static_cast<std::size_t>(m)];
        const bool previousSame = m > 0 && marks[static_cast<std::size_t>(m - 1)].island == mark.island;
        const bool nextSame = m + 1 < markCount && marks[static_cast<std::size_t>(m + 1)].island == mark.island;
        if (s < mark.position - 0.5 * mark.period && !previousSame)
            return -1;
        if (s > mark.position + 0.5 * mark.period && !nextSame)
            return -1;
        return m;
    };

    // 1. Schedule grains along output time. Unvoiced grains sit on a fixed grid and a voiced stretch is always
    //    entered from before its first mark, so any two renders place the same grains where they overlap.
    const auto onGrid = [&](double x, bool up)
    { return (up ? std::ceil(x / unvoicedStep) : std::floor(x / unvoicedStep)) * unvoicedStep; };
    double t = onGrid(static_cast<double>(r0), false);
    for (int guard = 0; guard < 100000; ++guard)
    {
        const int m = voicedMark(plan.sourceOf(t));
        if (m < 0)
            break;
        int firstOfIsland = m;
        while (firstOfIsland > 0 &&
               marks[static_cast<std::size_t>(firstOfIsland - 1)].island == marks[static_cast<std::size_t>(m)].island)
            --firstOfIsland;
        t = onGrid(plan.outputOf(marks[static_cast<std::size_t>(firstOfIsland)].position) - 2.0 * unvoicedStep, false);
    }

    grains.clear();
    bool previousVoiced = false;
    int previousIsland = -1;
    while (t < static_cast<double>(r1))
    {
        double s = plan.sourceOf(t);
        int m = voicedMark(s);
        if (m < 0 && previousVoiced)
        {
            t = onGrid(t, true);
            s = plan.sourceOf(t);
            m = voicedMark(s);
        }
        if (m >= 0 && (!previousVoiced || marks[static_cast<std::size_t>(m)].island != previousIsland))
        {
            int k = m;
            if (marks[static_cast<std::size_t>(k)].position < s && k + 1 < markCount &&
                marks[static_cast<std::size_t>(k + 1)].island == marks[static_cast<std::size_t>(m)].island)
                ++k;
            const double onMark = plan.outputOf(marks[static_cast<std::size_t>(k)].position);
            if (onMark >= t)
            {
                t = onMark;
                s = marks[static_cast<std::size_t>(k)].position;
                m = k;
            }
        }

        Grain grain;
        grain.centre = t;
        grain.voiced = m >= 0;
        grain.beta = std::exp2(plan.formantAt(t) / 12.0f);
        double step = unvoicedStep;
        if (grain.voiced)
        {
            const auto& mark = marks[static_cast<std::size_t>(m)];
            const bool previousSame = m > 0 && marks[static_cast<std::size_t>(m - 1)].island == mark.island;
            const bool nextSame = m + 1 < markCount && marks[static_cast<std::size_t>(m + 1)].island == mark.island;
            grain.source = mark.position;
            grain.sourceLeft = previousSame ? mark.position - marks[static_cast<std::size_t>(m - 1)].position
                                            : static_cast<double>(mark.period);
            grain.sourceRight = nextSame ? marks[static_cast<std::size_t>(m + 1)].position - mark.position
                                         : static_cast<double>(mark.period);
            const double alpha = std::exp2(static_cast<double>(plan.shiftAt(t)) / 12.0);
            step = std::max(grain.sourceRight / alpha, 8.0);
            previousIsland = mark.island;
            ++stats.voicedGrains;
        }
        else
        {
            grain.source = s;
            const double slope = (plan.sourceOf(t + unvoicedStep) - s) / unvoicedStep;
            if (slope < 0.8)
                grain.source += jitter(t) * 0.5 * unvoicedStep * (1.0 - std::max(0.0, slope));
            ++stats.unvoicedGrains;
        }
        previousVoiced = grain.voiced;
        grains.push_back(grain);
        t += step;
        if (cancel != nullptr && (grains.size() & 255u) == 0 && cancel->load(std::memory_order_relaxed))
            return false;
    }

    // 2. Windows: voiced grains reach to the neighbouring marks; anything else meets its neighbour's centre.
    const auto grainCount = grains.size();
    for (std::size_t i = 0; i < grainCount; ++i)
    {
        auto& g = grains[i];
        const double previousGap = i > 0 ? g.centre - grains[i - 1].centre : unvoicedStep;
        const double nextGap = i + 1 < grainCount ? grains[i + 1].centre - g.centre : unvoicedStep;
        const bool voicedBefore = i > 0 && grains[i - 1].voiced;
        const bool voicedAfter = i + 1 < grainCount && grains[i + 1].voiced;
        g.left = g.voiced && voicedBefore ? g.sourceLeft / g.beta : previousGap;
        g.right = g.voiced && voicedAfter ? g.sourceRight / g.beta : nextGap;
        g.left = std::max(g.left, 1.0);
        g.right = std::max(g.right, 1.0);
    }

    // 3. Overlap-add.
    std::vector<std::vector<float>> acc(static_cast<std::size_t>(channels), std::vector<float>(count, 0.0f));
    norm.assign(count, 0.0f);
    animalaudio::dsp::SincReader::Weights weights;
    for (std::size_t gi = 0; gi < grainCount; ++gi)
    {
        const auto& g = grains[gi];
        const auto from = std::max<std::int64_t>(static_cast<std::int64_t>(std::ceil(g.centre - g.left)), r0);
        const auto to = std::min<std::int64_t>(static_cast<std::int64_t>(std::floor(g.centre + g.right)), r1 - 1);
        const bool unity = std::abs(g.beta - 1.0f) < 1.0e-6f;
        for (auto i = from; i <= to; ++i)
        {
            const double dt = static_cast<double>(i) - g.centre;
            const double u = dt < 0.0 ? dt / g.left : dt / g.right;
            if (std::abs(u) >= 1.0)
                continue;
            const float w = hann.value(static_cast<float>(u));
            const double position = g.source + g.beta * dt;
            const auto slot = static_cast<std::size_t>(i - r0);
            norm[slot] += w;
            const double nearest = std::round(position);
            if (unity && std::abs(position - nearest) < 1.0e-4)
            {
                const auto index = static_cast<std::int64_t>(nearest);
                if (index >= 0 && index < source.length)
                    for (int c = 0; c < channels; ++c)
                        acc[static_cast<std::size_t>(c)][slot] += w * source.channels[c][index];
                continue;
            }
            reader.prepareRead(position, std::max(1.0f, g.beta), weights);
            for (int c = 0; c < channels; ++c)
                acc[static_cast<std::size_t>(c)][slot] +=
                    w * animalaudio::dsp::SincReader::applyLinear(weights, source.channels[c], source.length);
        }
        if (cancel != nullptr && (gi & 255u) == 0 && cancel->load(std::memory_order_relaxed))
            return false;
    }
    std::vector<float> mono(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
    {
        const float scale = 1.0f / std::max(norm[i], 1.0f);
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            acc[static_cast<std::size_t>(c)][i] *= scale;
            sum += acc[static_cast<std::size_t>(c)][i];
        }
        mono[i] = sum / static_cast<float>(channels);
    }

    // 4. Level: follow the source's envelope (frames on a fixed grid, FIR-smoothed), then the plan's gain.
    const auto hop = std::max<std::int64_t>(1, std::llround(kLevelHopSeconds * rate));
    const auto half = std::max<std::int64_t>(1, std::llround(kLevelHalfWindowSeconds * rate));
    const auto floorDiv = [](std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const std::int64_t firstFrame = -floorDiv(-(r0 + half), hop);
    const std::int64_t lastFrame = floorDiv(r1 - half, hop);
    const auto frames = static_cast<std::size_t>(std::max<std::int64_t>(lastFrame - firstFrame + 1, 1));
    std::vector<float> ratios(frames, 0.0f);
    for (std::size_t k = 0; k < frames; ++k)
    {
        const std::int64_t centre = (firstFrame + static_cast<std::int64_t>(k)) * hop;
        const double produced =
            rms(mono.data(), static_cast<std::int64_t>(count), centre - half - r0, centre + half - r0);
        const auto s = std::llround(plan.sourceOf(static_cast<double>(centre)));
        const double wanted = rms(source.mono, source.length, s - half, s + half);
        float ratio = 1.0f;
        if (produced > 1.0e-5 && wanted > 1.0e-5)
            ratio = std::clamp(static_cast<float>(wanted / produced), 1.0f / kLevelRange, kLevelRange);
        ratios[k] = std::log(ratio);
    }
    const int reach = static_cast<int>(std::lround(kLevelSmoothingSeconds / kLevelHopSeconds));
    envelope.assign(frames, 0.0f);
    for (std::size_t k = 0; k < frames; ++k)
    {
        float sum = 0.0f, weightSum = 0.0f;
        for (int j = -reach; j <= reach; ++j)
        {
            const auto index = static_cast<std::int64_t>(k) + j;
            if (index < 0 || index >= static_cast<std::int64_t>(frames))
                continue;
            const float w = 0.5f + 0.5f * std::cos(3.14159265f * static_cast<float>(j) / static_cast<float>(reach + 1));
            sum += w * ratios[static_cast<std::size_t>(index)];
            weightSum += w;
        }
        envelope[k] = sum / weightSum;
    }

    for (auto i = begin; i < end; ++i)
    {
        const auto rel = i - r0;
        const double position = static_cast<double>(i) / static_cast<double>(hop) - static_cast<double>(firstFrame);
        const auto k = static_cast<std::size_t>(std::clamp(std::floor(position), 0.0, static_cast<double>(frames - 1)));
        const float frac = static_cast<float>(std::clamp(position - static_cast<double>(k), 0.0, 1.0));
        const float level = std::exp(envelope[k] + (envelope[std::min(k + 1, frames - 1)] - envelope[k]) * frac);
        const float gain = level * plan.gainAt(static_cast<double>(i));
        for (int c = 0; c < channels; ++c)
            out[static_cast<std::size_t>(c)][static_cast<std::size_t>(i - begin)] =
                acc[static_cast<std::size_t>(c)][static_cast<std::size_t>(rel)] * gain;
    }
    return true;
}
} // namespace monkeydyne::dsp
