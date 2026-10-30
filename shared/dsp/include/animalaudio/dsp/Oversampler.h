#pragma once

#include "WindowedSinc.h"

#include <array>

namespace animalaudio::dsp
{
/**
 * 4x up/down sampler for one channel of a nonlinearity: linear-phase Kaiser FIR (cutoff at the base-rate
 * Nyquist, about -90 dB stopband), run polyphase so only the non-zero products are computed.
 * Up then down delays the signal by exactly kLatency base-rate samples. Allocation free after construction.
 */
class Oversampler4x
{
public:
    static constexpr int kFactor = 4;
    static constexpr int kTaps = 129; // odd: integer group delay
    static constexpr int kLatency = (kTaps - 1) / kFactor;
    static constexpr int kPhaseTaps = (kTaps + kFactor - 1) / kFactor;

    Oversampler4x()
    {
        const auto taps = designLowpass(kTaps, 0.5 / kFactor, 9.0);
        for (int k = 0; k < kTaps; ++k)
        {
            down[static_cast<std::size_t>(k)] = static_cast<float>(taps[static_cast<std::size_t>(k)]);
            up[static_cast<std::size_t>(k % kFactor)][static_cast<std::size_t>(k / kFactor)] =
                static_cast<float>(taps[static_cast<std::size_t>(k)] * kFactor);
        }
        reset();
    }

    void reset() noexcept
    {
        inputHistory.fill(0.0f);
        outputHistory.fill(0.0f);
        inputPos = 0;
        outputPos = 0;
    }

    /** One base-rate sample in, kFactor oversampled samples out. */
    void upsample(float x, float* out) noexcept
    {
        inputPos = inputPos == 0 ? kPhaseTaps - 1 : inputPos - 1;
        inputHistory[static_cast<std::size_t>(inputPos)] = x;
        inputHistory[static_cast<std::size_t>(inputPos + kPhaseTaps)] = x;
        const float* recent = inputHistory.data() + inputPos; // recent[j] = x[n - j]
        for (int p = 0; p < kFactor; ++p)
        {
            const auto& phase = up[static_cast<std::size_t>(p)];
            float sum = 0.0f;
            for (int j = 0; j < kPhaseTaps; ++j)
                sum += phase[static_cast<std::size_t>(j)] * recent[j];
            out[p] = sum;
        }
    }

    /** kFactor oversampled samples in, one base-rate sample out. */
    float downsample(const float* in) noexcept
    {
        for (int p = 0; p < kFactor; ++p)
        {
            outputPos = outputPos == 0 ? kDownSpan - 1 : outputPos - 1;
            outputHistory[static_cast<std::size_t>(outputPos)] = in[p];
            outputHistory[static_cast<std::size_t>(outputPos + kDownSpan)] = in[p];
        }
        // Evaluated at the first sample of the group, so up + down is a whole number of base-rate samples.
        const float* recent = outputHistory.data() + outputPos + (kFactor - 1);
        float sum = 0.0f;
        for (int k = 0; k < kTaps; ++k)
            sum += down[static_cast<std::size_t>(k)] * recent[k];
        return sum;
    }

private:
    static constexpr int kDownSpan = kTaps + kFactor;

    std::array<std::array<float, kPhaseTaps>, kFactor> up{};
    std::array<float, kTaps> down{};
    std::array<float, 2 * kPhaseTaps> inputHistory{};
    std::array<float, 2 * kDownSpan> outputHistory{};
    int inputPos = 0;
    int outputPos = 0;
};
} // namespace animalaudio::dsp
