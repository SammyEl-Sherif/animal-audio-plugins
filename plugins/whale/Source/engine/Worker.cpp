#include "Worker.h"

#include "dsp/Analysis.h"
#include "dsp/EditPlan.h"

namespace whale
{
namespace
{
constexpr int kChunksAheadArmed = 16;
constexpr int kChunksAheadIdle = 2;
constexpr double kShortestTakeSeconds = 0.25;
constexpr juce::uint32 kPublishIntervalMs = 30;
} // namespace

Worker::Worker(Session& s, dsp::CaptureBuffer& c, PlaybackExchange& e)
    : juce::Thread("Whale worker")
    , session(s)
    , capture(c)
    , exchange(e)
{
}

Worker::~Worker()
{
    stop();
}

void Worker::start()
{
    if (!isThreadRunning())
    {
        cancelRender.store(false);
        startThread(juce::Thread::Priority::normal);
    }
}

void Worker::stop()
{
    cancelRender.store(true);
    signalThreadShouldExit();
    notify();
    stopThread(10000);
}

bool Worker::isUpToDate() const noexcept
{
    return !analysing.load(std::memory_order_acquire) && capture.state() == dsp::CaptureBuffer::State::Idle &&
           renderedGeneration.load(std::memory_order_acquire) == session.editGeneration();
}

bool Worker::shouldStop() const
{
    return threadShouldExit() || cancelRender.load(std::memory_order_relaxed);
}

void Worker::run()
{
    while (!threadShouldExit())
    {
        runOnce();
        wait(isUpToDate() ? 20 : 2);
    }
}

void Worker::runOnce()
{
    const juce::ScopedLock sl(passLock);
    exchange.collectGarbage();
    capture.keepAhead(armed.load(std::memory_order_relaxed) ? kChunksAheadArmed : kChunksAheadIdle);
    collectCapture();
    if (analysePending())
        return;
    renderEdits();
}

void Worker::collectCapture()
{
    dsp::CaptureBuffer::Take take;
    if (!capture.collect(take, kChunksAheadIdle))
        return;
    if (take.channels.empty() || static_cast<double>(take.channels[0].size()) < kShortestTakeSeconds * take.rate)
        return;
    analysing.store(true, std::memory_order_release);
    session.addTake(take.startSeconds, TakeAudio::fromChannels(std::move(take.channels), take.rate));
}

bool Worker::analysePending()
{
    for (const auto& take : session.takes())
    {
        if (take.analysis != nullptr || take.audio == nullptr)
            continue;
        analysing.store(true, std::memory_order_release);
        activityValue.store(static_cast<int>(Activity::Analysing), std::memory_order_relaxed);
        auto analysis = std::make_shared<dsp::TakeAnalysis>();
        auto lastReport = juce::Time::getMillisecondCounter();
        const bool done = dsp::analyseTake(take.audio->mono.data(), take.audio->length(), take.audio->rate, *analysis,
                                           [&](float p)
                                           {
                                               progressValue.store(p, std::memory_order_relaxed);
                                               capture.keepAhead(armed.load() ? kChunksAheadArmed : kChunksAheadIdle);
                                               const auto now = juce::Time::getMillisecondCounter();
                                               if (now - lastReport > 50)
                                               {
                                                   session.setProgress(take.id, p);
                                                   lastReport = now;
                                               }
                                               return !shouldStop();
                                           });
        if (done)
            session.setAnalysis(take.id, std::move(analysis));
        return true;
    }
    analysing.store(false, std::memory_order_release);
    return false;
}

void Worker::publish(const Session::Snapshot& snapshot, std::uint64_t generation)
{
    auto map = std::make_unique<dsp::PlaybackMap>();
    map->generation = generation;
    for (const auto& take : snapshot.takes)
    {
        if (take.audio == nullptr)
            continue;
        dsp::TakePlayback playback;
        playback.id = take.id;
        playback.startSeconds = take.startSeconds;
        playback.rate = take.audio->rate;
        playback.length = take.audio->length();
        playback.original = std::shared_ptr<const dsp::ChannelData>(take.audio, &take.audio->channels);
        const auto cache = caches.find(take.id);
        if (cache != caches.end() && cache->second.audio == take.audio.get() && take.analysis != nullptr)
            playback.segments = cache->second.segments.segments();
        map->takes.push_back(std::move(playback));
    }
    exchange.publish(std::move(map));
}

void Worker::renderEdits()
{
    const auto generation = session.editGeneration();
    if (generation == renderedGeneration.load(std::memory_order_acquire))
    {
        activityValue.store(static_cast<int>(Activity::Idle), std::memory_order_relaxed);
        return;
    }
    const auto snapshot = session.snapshot();
    activityValue.store(static_cast<int>(Activity::Rendering), std::memory_order_relaxed);

    for (auto it = caches.begin(); it != caches.end();)
    {
        const bool present =
            std::any_of(snapshot.takes.begin(), snapshot.takes.end(),
                        [&](const TakeInfo& t) { return t.id == it->first && t.audio.get() == it->second.audio; });
        it = present ? std::next(it) : caches.erase(it);
    }

    auto lastPublish = juce::Time::getMillisecondCounter();
    bool complete = true;
    int takeIndex = 0;
    for (const auto& take : snapshot.takes)
    {
        progressValue.store(static_cast<float>(takeIndex++) /
                                static_cast<float>(std::max<std::size_t>(snapshot.takes.size(), 1)),
                            std::memory_order_relaxed);
        if (take.analysis == nullptr || take.audio == nullptr)
            continue;
        const dsp::EditPlan plan(*take.analysis, take.notes, snapshot.scale);
        dsp::SourceView view;
        view.numChannels = static_cast<int>(take.audio->channels.size());
        for (int c = 0; c < std::min(view.numChannels, 2); ++c)
            view.channels[c] = take.audio->channels[static_cast<std::size_t>(c)].data();
        view.mono = take.audio->mono.data();
        view.length = take.audio->length();
        auto& cache = caches[take.id];
        cache.audio = take.audio.get();
        const bool finished = cache.segments.update(
            view, *take.analysis, plan,
            [&]
            {
                const auto now = juce::Time::getMillisecondCounter();
                if (now - lastPublish >= kPublishIntervalMs)
                {
                    publish(snapshot, 0);
                    lastPublish = now;
                }
            },
            [&]
            {
                capture.keepAhead(armed.load() ? kChunksAheadArmed : kChunksAheadIdle);
                exchange.collectGarbage();
                return shouldStop() || session.editGeneration() != generation;
            },
            &cancelRender);
        if (!finished)
        {
            complete = false;
            break;
        }
    }
    publish(snapshot, complete ? generation : 0);
    if (complete)
    {
        renderedGeneration.store(generation, std::memory_order_release);
        activityValue.store(static_cast<int>(Activity::Idle), std::memory_order_relaxed);
    }
}
} // namespace whale
