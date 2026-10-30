#include "WhaleProcessor.h"

#include "WhaleEditor.h"
#include "engine/StateCodec.h"

#include <algorithm>
#include <cmath>

namespace whale
{
namespace
{
constexpr int kSubBlock = 64;
constexpr double kAuditionFadeSeconds = 0.005;
constexpr double kAuditionTailSeconds = 0.01;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ids::mix, "Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; })
            .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue() * 0.01f; })));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::compare, "Compare", false));
    return layout;
}
} // namespace

WhaleProcessor::WhaleProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "WhaleState", createParameterLayout())
    , worker(session, capture, exchange)
{
    mixParam = apvts.getRawParameterValue(ids::mix.getParamID());
    compareParam = apvts.getRawParameterValue(ids::compare.getParamID());
    mixSmoother.prepare(currentRate.load(), 15.0);
    mixSmoother.reset(mixParam->load());
    worker.start();
}

WhaleProcessor::~WhaleProcessor()
{
    worker.stop();
}

bool WhaleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return output == layouts.getMainInputChannelSet();
}

void WhaleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentRate.store(sampleRate);
    mixSmoother.prepare(sampleRate, 15.0);
    mixSmoother.reset(compareParam->load() > 0.5f ? 0.0f : mixParam->load());
    scratchSize = std::max(samplesPerBlock, 512);
    for (auto& s : scratch)
        s.assign(static_cast<std::size_t>(scratchSize), 0.0f);
    worker.start();
}

void WhaleProcessor::setTransferArmed(bool armed)
{
    transferArmed.store(armed, std::memory_order_relaxed);
    worker.setArmed(armed);
    worker.wake();
}

void WhaleProcessor::audition(std::uint32_t takeId, double fromSample, double seconds)
{
    auditionTakeRequest.store(takeId);
    auditionFromRequest.store(fromSample);
    auditionLengthRequest.store(seconds);
    auditionSerial.fetch_add(1);
}

void WhaleProcessor::stopAudition()
{
    audition(0, 0.0, 0.0);
}

double WhaleProcessor::auditionPosition(std::uint32_t& takeId) const noexcept
{
    takeId = auditionTakeNow.load(std::memory_order_relaxed);
    return auditionNow.load(std::memory_order_relaxed);
}

void WhaleProcessor::waitForWorker()
{
    for (int i = 0; i < 2000 && !worker.isUpToDate(); ++i)
        juce::Thread::sleep(5);
    for (int i = 0; i < 200; ++i)
    {
        const auto* map = exchange.acquire();
        if (map != nullptr && map->generation == session.editGeneration())
            break;
        juce::Thread::sleep(2);
    }
}

void WhaleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inputs = std::min(getTotalNumInputChannels(), buffer.getNumChannels());
    for (int channel = inputs; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, n);
    if (n <= 0)
        return;

    float peak = 0.0f;
    for (int channel = 0; channel < std::min(inputs, 2); ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, n));
    float previousPeak = inputPeak.load(std::memory_order_relaxed);
    while (peak > previousPeak && !inputPeak.compare_exchange_weak(previousPeak, peak, std::memory_order_relaxed))
    {
    }

    const double rate = currentRate.load(std::memory_order_relaxed);
    bool playing = false;
    bool hasTime = false;
    std::int64_t samplePos = 0;
    double seconds = 0.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            playing = position->getIsPlaying();
            if (const auto samples = position->getTimeInSamples())
            {
                samplePos = *samples;
                seconds = static_cast<double>(samplePos) / rate;
                hasTime = true;
            }
            else if (const auto time = position->getTimeInSeconds())
            {
                seconds = *time;
                samplePos = std::llround(seconds * rate);
                hasTime = true;
            }
        }
    hostPlaying.store(playing, std::memory_order_relaxed);
    if (hasTime)
        hostSeconds.store(seconds, std::memory_order_relaxed);

    if (isNonRealtime() && !worker.isUpToDate())
        waitForWorker();
    const auto* map = exchange.acquire();

    // Transfer: record while armed and playing; a jump or a stop ends the take.
    const bool armed = transferArmed.load(std::memory_order_relaxed);
    if (armed && playing && hasTime && inputs > 0)
    {
        if (capture.state() == dsp::CaptureBuffer::State::Idle && capture.begin(seconds, rate, inputs))
            nextCaptureSample = samplePos;
        if (capture.state() == dsp::CaptureBuffer::State::Capturing)
        {
            if (samplePos != nextCaptureSample)
            {
                capture.finish();
                transferArmed.store(false, std::memory_order_relaxed);
            }
            else
            {
                const float* in[2] = {buffer.getReadPointer(0), buffer.getReadPointer(std::min(1, inputs - 1))};
                capture.write(in, inputs, n);
                nextCaptureSample = samplePos + n;
            }
        }
    }
    else if (capture.state() == dsp::CaptureBuffer::State::Capturing)
    {
        capture.finish();
        if (!playing)
            transferArmed.store(false, std::memory_order_relaxed);
    }
    if (capture.state() == dsp::CaptureBuffer::State::Capturing)
        return; // the performance passes through while it is recorded

    mixSmoother.setTarget(
        compareParam->load(std::memory_order_relaxed) > 0.5f ? 0.0f : mixParam->load(std::memory_order_relaxed));
    if (map == nullptr)
        return;
    if (playing && hasTime)
    {
        auditionNow.store(-1.0, std::memory_order_relaxed);
        auditionEnd = auditionPos;
        renderTakes(*map, buffer, seconds, n);
    }
    else
    {
        for (int i = 0; i < n; ++i)
            mixSmoother.next();
        renderAudition(*map, buffer, n);
    }
}

void WhaleProcessor::renderTakes(const dsp::PlaybackMap& map, juce::AudioBuffer<float>& buffer, double seconds, int n)
{
    const double rate = currentRate.load(std::memory_order_relaxed);
    const int channels = std::min(buffer.getNumChannels(), 2);
    for (int start = 0; start < n; start += kSubBlock)
    {
        const int count = std::min(kSubBlock, n - start);
        float mix = 0.0f;
        for (int i = 0; i < count; ++i)
            mix = mixSmoother.next();
        for (const auto& take : map.takes)
        {
            if (std::abs(take.rate - rate) > 0.5 || take.original == nullptr)
                continue;
            const auto local = static_cast<std::int64_t>(std::llround((seconds - take.startSeconds) * rate)) + start;
            const std::int64_t from = std::max<std::int64_t>(local, 0);
            const std::int64_t to = std::min<std::int64_t>(local + count, take.length);
            if (from >= to)
                continue;
            const auto offset = static_cast<int>(from - local);
            float* out[2] = {buffer.getWritePointer(0, start + offset),
                             buffer.getWritePointer(std::min(1, channels - 1), start + offset)};
            dsp::readTake(take, from, static_cast<int>(to - from), out, channels, mix, fades);
        }
    }
}

void WhaleProcessor::renderAudition(const dsp::PlaybackMap& map, juce::AudioBuffer<float>& buffer, int n)
{
    const double rate = currentRate.load(std::memory_order_relaxed);
    const int serial = auditionSerial.load(std::memory_order_acquire);
    if (serial != auditionSeen)
    {
        auditionSeen = serial;
        auditionTake = auditionTakeRequest.load();
        auditionPos = static_cast<std::int64_t>(auditionFromRequest.load());
        auditionEnd = auditionPos + static_cast<std::int64_t>(auditionLengthRequest.load() * rate);
        auditionFadeIn = 0;
    }
    if (auditionPos >= auditionEnd || auditionTake == 0)
    {
        auditionNow.store(-1.0, std::memory_order_relaxed);
        return;
    }
    const dsp::TakePlayback* take = nullptr;
    for (const auto& t : map.takes)
        if (t.id == auditionTake && std::abs(t.rate - rate) <= 0.5)
            take = &t;
    if (take == nullptr || scratchSize <= 0)
    {
        auditionEnd = auditionPos;
        return;
    }
    const int channels = std::min(buffer.getNumChannels(), 2);
    const auto fadeIn = static_cast<std::int64_t>(kAuditionFadeSeconds * rate);
    const auto fadeOut = static_cast<std::int64_t>(kAuditionTailSeconds * rate);
    const float mix = mixSmoother.getCurrent();
    for (int start = 0; start < n && auditionPos < auditionEnd; start += scratchSize)
    {
        const int count = static_cast<int>(std::min<std::int64_t>(
            {static_cast<std::int64_t>(scratchSize), static_cast<std::int64_t>(n - start), auditionEnd - auditionPos}));
        float* tmp[2] = {scratch[0].data(), scratch[1].data()};
        dsp::readTake(*take, auditionPos, count, tmp, 2, mix, fades);
        for (int i = 0; i < count; ++i)
        {
            const auto position = auditionPos + i;
            float gain = std::min(1.0f, static_cast<float>(auditionFadeIn + i) /
                                            static_cast<float>(std::max<std::int64_t>(fadeIn, 1)));
            gain = std::min(gain, static_cast<float>(auditionEnd - position) /
                                      static_cast<float>(std::max<std::int64_t>(fadeOut, 1)));
            for (int c = 0; c < channels; ++c)
                buffer.getWritePointer(c)[start + i] +=
                    gain * scratch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
        }
        auditionPos += count;
        auditionFadeIn += count;
    }
    auditionTakeNow.store(auditionTake, std::memory_order_relaxed);
    auditionNow.store(auditionPos < auditionEnd ? static_cast<double>(auditionPos) : -1.0, std::memory_order_relaxed);
}

const juce::MemoryBlock& WhaleProcessor::flacFor(const std::shared_ptr<const TakeAudio>& audio)
{
    const juce::ScopedLock sl(flacLock);
    for (auto it = flacCache.begin(); it != flacCache.end();)
        it = it->second.first.expired() ? flacCache.erase(it) : std::next(it);
    auto& entry = flacCache[audio.get()];
    if (entry.second.isEmpty())
    {
        entry.first = audio;
        entry.second = state::encodeFlac(*audio);
    }
    return entry.second;
}

void WhaleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData = state::write(apvts.copyState(), session,
                            [this](const std::shared_ptr<const TakeAudio>& audio) -> const juce::MemoryBlock&
                            { return flacFor(audio); });
}

void WhaleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    state::Loaded loaded;
    if (state::read(data, sizeInBytes, loaded))
    {
        if (loaded.parameters.hasType(apvts.state.getType()))
            apvts.replaceState(loaded.parameters);
        session.restore(loaded.session, loaded.takes);
        worker.wake();
        return;
    }
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* WhaleProcessor::createEditor()
{
    return new WhaleEditor(*this);
}
} // namespace whale

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new whale::WhaleProcessor();
}
