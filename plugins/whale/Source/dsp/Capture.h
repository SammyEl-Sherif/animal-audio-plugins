#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

/**
 * Recording a take on the audio thread without allocating: audio goes into fixed-size chunks that a background
 * thread allocates ahead of time. One capture at a time:
 *
 *   Idle --begin (audio)--> Capturing --finish (audio)--> Finished --collect (worker)--> Idle
 *
 * If the worker falls behind and the next chunk is missing, the capture finishes early (`overflowed`).
 */
namespace whale::dsp
{
class CaptureBuffer
{
public:
    static constexpr int kChunkFrames = 1 << 16;
    static constexpr int kMaxChunks = 4096; // ~93 minutes at 48 kHz
    static constexpr int kChannels = 2;

    enum class State
    {
        Idle = 0,
        Capturing,
        Finished
    };

    struct Take
    {
        double startSeconds = 0.0;
        double rate = 48000.0;
        bool overflowed = false;
        std::vector<std::vector<float>> channels;
    };

    CaptureBuffer()
    {
        for (auto& slot : slots)
            slot.store(nullptr, std::memory_order_relaxed);
    }

    ~CaptureBuffer()
    {
        for (auto& slot : slots)
            delete[] slot.exchange(nullptr);
    }

    CaptureBuffer(const CaptureBuffer&) = delete;
    CaptureBuffer& operator=(const CaptureBuffer&) = delete;

    State state() const noexcept { return static_cast<State>(stateValue.load(std::memory_order_acquire)); }
    std::int64_t capturedFrames() const noexcept { return frames.load(std::memory_order_relaxed); }

    // Audio thread ------------------------------------------------------------------------------------------------

    /** Starts a capture if idle. */
    bool begin(double startSeconds, double rate, int channels) noexcept
    {
        if (state() != State::Idle || slots[0].load(std::memory_order_acquire) == nullptr)
            return false;
        start = startSeconds;
        sampleRate = rate;
        channelCount = std::clamp(channels, 1, kChannels);
        overflow = false;
        frames.store(0, std::memory_order_relaxed);
        stateValue.store(static_cast<int>(State::Capturing), std::memory_order_release);
        return true;
    }

    /** Appends a block; finishes the capture (overflow) if no chunk is ready. */
    void write(const float* const* input, int channels, int count) noexcept
    {
        if (state() != State::Capturing)
            return;
        std::int64_t at = frames.load(std::memory_order_relaxed);
        for (int done = 0; done < count;)
        {
            const auto chunkIndex = static_cast<std::size_t>(at / kChunkFrames);
            float* chunk = chunkIndex < slots.size() ? slots[chunkIndex].load(std::memory_order_acquire) : nullptr;
            if (chunk == nullptr)
            {
                overflow = true;
                frames.store(at, std::memory_order_relaxed);
                finish();
                return;
            }
            const auto offset = static_cast<int>(at % kChunkFrames);
            const int n = std::min(count - done, kChunkFrames - offset);
            for (int c = 0; c < channelCount; ++c)
            {
                const float* in = input[std::min(c, channels - 1)] + done;
                std::copy(in, in + n, chunk + c * kChunkFrames + offset);
            }
            at += n;
            done += n;
        }
        frames.store(at, std::memory_order_relaxed);
    }

    void finish() noexcept
    {
        if (state() == State::Capturing)
            stateValue.store(static_cast<int>(State::Finished), std::memory_order_release);
    }

    // Background thread -------------------------------------------------------------------------------------------

    /** Makes sure `ahead` chunks past the current write position exist. */
    void keepAhead(int ahead)
    {
        const auto needed = std::min<std::int64_t>(capturedFrames() / kChunkFrames + 1 + ahead, kMaxChunks);
        for (std::int64_t i = 0; i < needed; ++i)
            if (slots[static_cast<std::size_t>(i)].load(std::memory_order_acquire) == nullptr)
                slots[static_cast<std::size_t>(i)].store(new float[static_cast<std::size_t>(kChannels * kChunkFrames)],
                                                         std::memory_order_release);
    }

    /** Copies out a finished capture and frees the chunks beyond `keep`; then the buffer is idle again. */
    bool collect(Take& take, int keep)
    {
        if (state() != State::Finished)
            return false;
        const auto length = frames.load(std::memory_order_relaxed);
        take.startSeconds = start;
        take.rate = sampleRate;
        take.overflowed = overflow;
        take.channels.assign(static_cast<std::size_t>(channelCount),
                             std::vector<float>(static_cast<std::size_t>(length)));
        for (std::int64_t at = 0; at < length;)
        {
            const float* chunk = slots[static_cast<std::size_t>(at / kChunkFrames)].load(std::memory_order_acquire);
            const auto offset = static_cast<int>(at % kChunkFrames);
            const auto n = static_cast<int>(std::min<std::int64_t>(length - at, kChunkFrames - offset));
            for (int c = 0; c < channelCount; ++c)
                std::copy(chunk + c * kChunkFrames + offset, chunk + c * kChunkFrames + offset + n,
                          take.channels[static_cast<std::size_t>(c)].begin() + at);
            at += n;
        }
        for (std::size_t i = static_cast<std::size_t>(std::max(keep, 1)); i < slots.size(); ++i)
            delete[] slots[i].exchange(nullptr, std::memory_order_acq_rel);
        frames.store(0, std::memory_order_relaxed);
        stateValue.store(static_cast<int>(State::Idle), std::memory_order_release);
        return true;
    }

private:
    std::array<std::atomic<float*>, kMaxChunks> slots;
    std::atomic<int> stateValue{static_cast<int>(State::Idle)};
    std::atomic<std::int64_t> frames{0};
    // Written by the audio thread before the release store of `stateValue`, read by the worker after acquiring it.
    double start = 0.0;
    double sampleRate = 48000.0;
    int channelCount = 1;
    bool overflow = false;
};
} // namespace whale::dsp
