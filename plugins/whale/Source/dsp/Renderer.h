#pragma once

#include "EditPlan.h"

#include <animalaudio/dsp/Psola.h>
#include <animalaudio/dsp/WindowedSinc.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace whale::dsp
{
/** The captured take: up to two channels plus the mono mix analysis ran on. */
struct SourceView
{
    const float* channels[2] = {nullptr, nullptr};
    int numChannels = 0;
    const float* mono = nullptr;
    std::int64_t length = 0;
};

/**
 * Offline TD-PSOLA render of an edited take (Moulines & Charpentier 1990), with the analysis pitch marks:
 *
 * - Grains are scheduled along output time. In voiced stretches each grain is the source around the pitch mark
 *   nearest to `sourceOf(t)`, the next grain follows one source period / 2^(shift / 12) later, so the pitch moves
 *   while the time map stays free (marks repeat or drop when stretched). Entering a voiced stretch starts on a mark.
 * - Consonants and breath are 5 ms grains read at their own time (jittered when slowed down a lot, so they do not
 *   buzz).
 * - A voiced grain's window reaches to the neighbouring marks, read `2^(formant / 12)` times faster (formant shift
 *   by resampling the grain, Parrot's method); unvoiced grains reach to the neighbouring grains. Overlap-add is
 *   normalised where the windows sum above one, so an untouched stretch comes out as the recording.
 * - The output level follows the source's (10 ms RMS, ratio smoothed over 20 ms), then the plan's gain applies.
 *
 * Grains and level frames sit on absolute grids, so rendering any part of a take gives that part of a full render
 * (to float rounding, under 1e-4): segments re-rendered separately stitch without seams.
 */
class Renderer
{
public:
    /**
     * Output samples [begin, end) of the edited take, per channel, into `out` (resized). Allocates; for a background
     * thread. Returns false if `cancel` was raised.
     */
    bool render(const SourceView& source, const TakeAnalysis& analysis, const EditPlan& plan, std::int64_t begin,
                std::int64_t end, std::vector<std::vector<float>>& out, const std::atomic<bool>* cancel = nullptr);

    /** Grains the last render placed, for tests. */
    struct Stats
    {
        int voicedGrains = 0;
        int unvoicedGrains = 0;
    };
    Stats lastStats() const noexcept { return stats; }

private:
    struct Grain
    {
        double centre = 0.0; // output sample
        double source = 0.0; // source sample at the centre
        float beta = 1.0f;
        double left = 0.0; // window half-widths, output samples
        double right = 0.0;
        double sourceLeft = 0.0; // voiced: distance to the neighbouring marks in the source
        double sourceRight = 0.0;
        bool voiced = false;
    };

    std::vector<Grain> grains;
    std::vector<float> norm;
    std::vector<float> envelope;
    Stats stats;
    animalaudio::dsp::SincReader reader;
    animalaudio::dsp::psola::HannTable hann;
};
} // namespace whale::dsp
