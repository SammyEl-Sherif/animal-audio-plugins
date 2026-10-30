#pragma once

#include "PlaybackExchange.h"
#include "Session.h"
#include "dsp/Capture.h"
#include "dsp/SegmentCache.h"

#include <juce_core/juce_core.h>

#include <map>

namespace monkeydyne
{
/**
 * The background thread: keeps capture chunks allocated ahead of the audio thread, turns finished captures into
 * takes, analyses them, and re-renders the segments edits touched, publishing playback maps as it goes. It always
 * works from the newest session snapshot: while the user drags a note, the segment under it re-renders over and
 * over with the latest position.
 */
class Worker : private juce::Thread
{
public:
    enum class Activity
    {
        Idle = 0,
        Analysing,
        Rendering
    };

    Worker(Session& session, dsp::CaptureBuffer& capture, PlaybackExchange& exchange);
    ~Worker() override;

    void start();
    void stop();
    void wake() { notify(); }
    void setArmed(bool isArmed) noexcept { armed.store(isArmed, std::memory_order_relaxed); }

    /** True when every take is analysed and every edit is in the published playback map. */
    bool isUpToDate() const noexcept;
    Activity activity() const noexcept { return static_cast<Activity>(activityValue.load(std::memory_order_relaxed)); }
    float progress() const noexcept { return progressValue.load(std::memory_order_relaxed); }

    /** Runs one pass of everything the thread does, on the calling thread (tests, or before the thread starts). */
    void runOnce();

private:
    void run() override;
    void collectCapture();
    bool analysePending();
    void renderEdits();
    void publish(const Session::Snapshot& snapshot, std::uint64_t generation);
    bool shouldStop() const;

    Session& session;
    dsp::CaptureBuffer& capture;
    PlaybackExchange& exchange;
    struct Cache
    {
        const TakeAudio* audio = nullptr; // a restored take may reuse an id: its cache starts over
        dsp::SegmentCache segments;
    };
    std::map<std::uint32_t, Cache> caches;
    std::atomic<std::uint64_t> renderedGeneration{0};
    std::atomic<int> activityValue{0};
    std::atomic<float> progressValue{0.0f};
    std::atomic<bool> armed{false};
    std::atomic<bool> cancelRender{false};
    std::atomic<bool> analysing{false};
    juce::CriticalSection passLock; // one pass at a time (thread vs runOnce)
};
} // namespace monkeydyne
