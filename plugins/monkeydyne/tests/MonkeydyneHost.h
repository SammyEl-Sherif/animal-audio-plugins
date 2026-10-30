#pragma once

#include "MonkeydyneProcessor.h"
#include "MonkeydyneTestSupport.h"

#include <animalaudio/testing/TestKit.h>
#include <animalaudio/testing/VoiceSynth.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>

namespace monkeydyne::testing
{
inline constexpr double kRate = 48000.0;
inline constexpr int kBlock = 512;
inline constexpr std::int64_t kTakeStart = 48000; // the phrase is recorded one second into the song

/** A host around the processor: a playhead it moves, and mono audio pushed through in blocks. */
struct Host
{
    animalaudio::testing::ProcessorHarness<MonkeydyneProcessor> harness{kRate, kBlock};
    MonkeydyneProcessor& processor = harness.processor;

    void setTransport(bool playing, std::int64_t at)
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setIsPlaying(playing);
        info.setTimeInSamples(at);
        info.setTimeInSeconds(static_cast<double>(at) / kRate);
        info.setBpm(120.0);
        harness.playHead.position = info;
    }

    /** Plays `input` from timeline sample `at` (or with the transport stopped) and returns the left output. */
    std::vector<float> play(const std::vector<float>& input, std::int64_t at, bool playing = true, int block = kBlock)
    {
        std::vector<float> out(input.size());
        juce::MidiBuffer midi;
        for (std::size_t done = 0; done < input.size(); done += static_cast<std::size_t>(block))
        {
            const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), input.size() - done));
            setTransport(playing, at + static_cast<std::int64_t>(done));
            juce::AudioBuffer<float> buffer(2, n);
            for (int c = 0; c < 2; ++c)
                buffer.copyFrom(c, 0, input.data() + done, n);
            processor.processBlock(buffer, midi);
            std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + n,
                      out.begin() + static_cast<std::ptrdiff_t>(done));
        }
        return out;
    }

    void stop(std::int64_t at) { play(std::vector<float>(static_cast<std::size_t>(kBlock), 0.0f), at, false); }

    /** Waits until the worker has analysed every take and published every edit. */
    void settle()
    {
        for (int i = 0; i < 4000 && !processor.getWorker().isUpToDate(); ++i)
            juce::Thread::sleep(5);
        REQUIRE(processor.getWorker().isUpToDate());
        // Let the audio thread pick up the final map.
        stop(0);
    }

    /** Records the sung phrase by transfer and waits for its analysis. */
    std::vector<float> recordPhrase()
    {
        const auto voice = animalaudio::testing::sing(phrase(), kPhraseSeconds, kRate);
        processor.setTransferArmed(true);
        processor.getWorker().runOnce();
        const auto out = play(voice, kTakeStart);
        stop(kTakeStart + static_cast<std::int64_t>(voice.size()));
        REQUIRE_FALSE(processor.isTransferArmed());
        settle();
        return out;
    }

    TakeInfo onlyTake() const
    {
        const auto takes = processor.getSession().takes();
        REQUIRE(takes.size() == 1);
        return takes.front();
    }

    /** Applies `edit` to the take's notes as one undoable step. */
    void editNotes(const std::function<void(std::vector<dsp::Note>&)>& edit)
    {
        auto take = onlyTake();
        processor.getSession().checkpoint();
        edit(take.notes);
        processor.getSession().setNotes(take.id, take.notes);
    }

    /** What plays over the take region with silence coming in. */
    std::vector<float> playback()
    {
        return play(std::vector<float>(static_cast<std::size_t>(kPhraseSeconds * kRate), 0.0f), kTakeStart);
    }
};

/** Index of the note whose source span holds `seconds` (into the take), -1 if none. */
inline int noteAtSeconds(const std::vector<dsp::Note>& notes, double seconds)
{
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].srcStart <= seconds * kRate && seconds * kRate < notes[i].srcEnd)
            return static_cast<int>(i);
    return -1;
}
} // namespace monkeydyne::testing
