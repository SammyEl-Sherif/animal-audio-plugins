#pragma once

#include "dsp/Capture.h"
#include "dsp/Playback.h"
#include "engine/PlaybackExchange.h"
#include "engine/Session.h"
#include "engine/Worker.h"

#include <animalaudio/dsp/Smoothing.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

namespace whale
{
namespace ids
{
inline const juce::ParameterID mix{"mix", 1};
inline const juce::ParameterID compare{"compare", 1};
} // namespace ids

/**
 * Whale: a note-based vocal editor. Live has no ARA, so audio comes in by transfer: arm TRANSFER and play, and
 * the passage is recorded at its place on the timeline. Afterwards, while the host plays over a take, Whale outputs
 * the edited take instead of its input (the input passes through everywhere else). With the transport stopped,
 * clicking a note auditions it.
 *
 * Threads: the audio thread captures (CaptureBuffer) and plays (PlaybackExchange); the worker records, analyses and
 * renders; the editor edits the Session. processBlock never allocates or locks; during an offline render
 * (freeze / export) it waits for the worker so the bounce has every edit.
 */
class WhaleProcessor : public juce::AudioProcessor
{
public:
    WhaleProcessor();
    ~WhaleProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Whale"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    Session& getSession() noexcept { return session; }
    Worker& getWorker() noexcept { return worker; }

    // Transfer.
    void setTransferArmed(bool armed);
    bool isTransferArmed() const noexcept { return transferArmed.load(std::memory_order_relaxed); }
    bool isCapturing() const noexcept { return capture.state() == dsp::CaptureBuffer::State::Capturing; }
    double capturedSeconds() const noexcept
    {
        return static_cast<double>(capture.capturedFrames()) / currentRate.load();
    }

    // Host transport as last seen by the audio thread.
    double getHostSeconds() const noexcept { return hostSeconds.load(std::memory_order_relaxed); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(std::memory_order_relaxed); }
    double getSampleRate() const noexcept { return currentRate.load(std::memory_order_relaxed); }
    /** Peak input level since the last call (UI meter). */
    float takeInputPeak() noexcept { return inputPeak.exchange(0.0f, std::memory_order_relaxed); }

    // Audition with the transport stopped: plays the edited take from `fromSample` (take output time).
    void audition(std::uint32_t takeId, double fromSample, double seconds);
    void stopAudition();
    /** Where the audition is now (take output samples), or -1. */
    double auditionPosition(std::uint32_t& takeId) const noexcept;

private:
    void renderTakes(const dsp::PlaybackMap& map, juce::AudioBuffer<float>& buffer, double seconds, int n);
    void renderAudition(const dsp::PlaybackMap& map, juce::AudioBuffer<float>& buffer, int n);
    void waitForWorker();
    const juce::MemoryBlock& flacFor(const std::shared_ptr<const TakeAudio>& audio);

    Session session;
    dsp::CaptureBuffer capture;
    PlaybackExchange exchange;
    Worker worker;
    dsp::FadeTable fades;

    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* compareParam = nullptr;
    animalaudio::dsp::OnePoleSmoother mixSmoother;

    std::atomic<double> currentRate{44100.0};
    std::atomic<bool> transferArmed{false};
    std::atomic<double> hostSeconds{0.0};
    std::atomic<bool> hostPlaying{false};
    std::atomic<float> inputPeak{0.0f};
    std::int64_t nextCaptureSample = 0; // audio thread

    // UI -> audio audition request, and the audio thread's progress through it.
    std::atomic<std::uint32_t> auditionTakeRequest{0};
    std::atomic<double> auditionFromRequest{0.0};
    std::atomic<double> auditionLengthRequest{0.0};
    std::atomic<int> auditionSerial{0};
    std::atomic<double> auditionNow{-1.0};
    std::atomic<std::uint32_t> auditionTakeNow{0};
    int auditionSeen = 0;
    std::uint32_t auditionTake = 0;
    std::int64_t auditionPos = 0;
    std::int64_t auditionEnd = 0;
    std::int64_t auditionFadeIn = 0;

    std::vector<float> scratch[2];
    int scratchSize = 0;

    std::map<const TakeAudio*, std::pair<std::weak_ptr<const TakeAudio>, juce::MemoryBlock>> flacCache;
    juce::CriticalSection flacLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WhaleProcessor)
};
} // namespace whale
