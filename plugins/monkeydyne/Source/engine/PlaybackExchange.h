#pragma once

#include "dsp/Playback.h"

#include <atomic>
#include <memory>

namespace monkeydyne
{
/**
 * Hands immutable PlaybackMaps from the worker to the audio thread without locks or frees on the audio thread.
 * The worker publishes into `incoming` (replacing a map the audio thread never saw); the audio thread swaps it in
 * only when `outgoing` is empty, parking the map it stops using there for the worker to delete.
 */
class PlaybackExchange
{
public:
    PlaybackExchange() = default;
    ~PlaybackExchange()
    {
        delete incoming.exchange(nullptr);
        delete outgoing.exchange(nullptr);
        delete active;
    }

    PlaybackExchange(const PlaybackExchange&) = delete;
    PlaybackExchange& operator=(const PlaybackExchange&) = delete;

    // Worker thread.
    void publish(std::unique_ptr<dsp::PlaybackMap> map)
    {
        delete incoming.exchange(map.release(), std::memory_order_acq_rel);
    }
    void collectGarbage() { delete outgoing.exchange(nullptr, std::memory_order_acq_rel); }

    // Audio thread.
    const dsp::PlaybackMap* acquire() noexcept
    {
        if (incoming.load(std::memory_order_acquire) != nullptr && outgoing.load(std::memory_order_acquire) == nullptr)
            if (auto* next = incoming.exchange(nullptr, std::memory_order_acq_rel))
            {
                outgoing.store(active, std::memory_order_release);
                active = next;
            }
        return active;
    }

private:
    std::atomic<dsp::PlaybackMap*> incoming{nullptr};
    std::atomic<dsp::PlaybackMap*> outgoing{nullptr};
    dsp::PlaybackMap* active = nullptr; // audio thread only
};
} // namespace monkeydyne
