#include "TurtleTimeEditor.h"
#include "TurtleTimeProcessor.h"
#include "dsp/TimePatterns.h"
#include "dsp/TurtleTimeEngine.h"

#include <animalaudio/core/BuildStamp.h>
#include <animalaudio/dsp/TempoSync.h>
#include <animalaudio/testing/TestKit.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using animalaudio::testing::ProcessorHarness;
using animalaudio::testing::writePngIfRequested;
using Catch::Approx;

namespace
{
constexpr double kSampleRate = 48000.0;

animalaudio::dsp::TransportInfo playingAt(double bpm, double ppq)
{
    animalaudio::dsp::TransportInfo transport;
    transport.playing = true;
    transport.hasBpm = true;
    transport.bpm = bpm;
    transport.hasPpq = true;
    transport.ppq = ppq;
    return transport;
}

std::vector<float> render(turtletime::dsp::TurtleTimeEngine& engine, const std::vector<float>& input, double bpm,
                          int blockSize, bool host, int firstSample = 0)
{
    std::vector<float> output(input.size());
    const double beatsPerSample = bpm / (60.0 * kSampleRate);
    int done = 0;
    while (done < static_cast<int>(input.size()))
    {
        const int count = std::min(blockSize, static_cast<int>(input.size()) - done);
        std::vector<float> block(input.begin() + done, input.begin() + done + count);
        float* channels[] = {block.data()};
        animalaudio::dsp::TransportInfo transport;
        if (host)
            transport = playingAt(bpm, static_cast<double>(firstSample + done) * beatsPerSample);
        engine.process(channels, 1, count, transport);
        std::copy(block.begin(), block.end(), output.begin() + done);
        done += count;
    }
    return output;
}

void prepareEngine(turtletime::dsp::TurtleTimeEngine& engine, int pattern, float mix, bool keepPitch = false)
{
    engine.setPatternIndex(pattern);
    engine.setMix(mix);
    engine.setKeepPitch(keepPitch);
    engine.prepare(kSampleRate, 512);
}

std::vector<float> sine(int count, float frequency, float amplitude)
{
    std::vector<float> samples(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        samples[static_cast<std::size_t>(i)] =
            amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * static_cast<float>(i) /
                                 static_cast<float>(kSampleRate));
    return samples;
}

int risingZeroCrossings(const std::vector<float>& samples, int from, int to)
{
    int crossings = 0;
    for (int i = from; i < to; ++i)
        if (samples[static_cast<std::size_t>(i - 1)] <= 0.0f && samples[static_cast<std::size_t>(i)] > 0.0f)
            ++crossings;
    return crossings;
}

float rms(const std::vector<float>& samples, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double>(samples[static_cast<std::size_t>(i)]) * samples[static_cast<std::size_t>(i)];
    return static_cast<float>(std::sqrt(sum / std::max(1, to - from)));
}

float maxStep(const std::vector<float>& samples)
{
    float largest = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

int patternWhere(float rate, double window, turtletime::dsp::TimeMode mode, double slice = 0.0)
{
    for (int i = 0; i < turtletime::dsp::kNumPatterns; ++i)
    {
        const auto& pattern = turtletime::dsp::kPatterns[static_cast<std::size_t>(i)];
        if (pattern.mode == mode && std::abs(pattern.rate - rate) < 1.0e-4f &&
            std::abs(pattern.windowBeats - window) < 1.0e-6 && std::abs(pattern.sliceBeats - slice) < 1.0e-6)
            return i;
    }
    FAIL("pattern not found");
    return 0;
}
} // namespace

TEST_CASE("turtletime: parameters exist")
{
    turtletime::TurtleTimeProcessor processor;
    REQUIRE(processor.getName() == "Turtle Time");
    REQUIRE(processor.apvts.getParameter("pattern") != nullptr);
    REQUIRE(processor.apvts.getParameter("mix") != nullptr);
    REQUIRE(processor.apvts.getParameter("keepPitch") != nullptr);
    REQUIRE(processor.apvts.getRawParameterValue("keepPitch")->load() < 0.5f);
    REQUIRE(processor.apvts.getParameter("fallbackBpm") == nullptr);
    REQUIRE(processor.getNumPrograms() == turtletime::dsp::kNumPatterns);
    REQUIRE(processor.getCurrentProgram() == turtletime::dsp::kDefaultPattern);
}

TEST_CASE("turtletime: patterns are named")
{
    REQUIRE(turtletime::dsp::kNumPatterns == 32);
    REQUIRE(static_cast<int>(turtletime::dsp::kPatterns.size()) == turtletime::dsp::kNumPatterns);
    for (int i = 0; i < turtletime::dsp::kNumPatterns; ++i)
    {
        const auto& pattern = turtletime::dsp::kPatterns[static_cast<std::size_t>(i)];
        REQUIRE(pattern.name.size() > 2);
        REQUIRE(pattern.blurb.size() > 8);
        REQUIRE(pattern.windowBeats > 0.0);
        REQUIRE(std::isfinite(pattern.rate));
        if (pattern.mode == turtletime::dsp::TimeMode::Repeat)
            REQUIRE(pattern.sliceBeats > 0.0);
        for (int j = i + 1; j < turtletime::dsp::kNumPatterns; ++j)
            REQUIRE(pattern.name != turtletime::dsp::kPatterns[static_cast<std::size_t>(j)].name);
    }
}

TEST_CASE("turtletime: state round trip")
{
    juce::MemoryBlock saved;
    {
        turtletime::TurtleTimeProcessor processor;
        processor.apvts.getParameter("pattern")->setValueNotifyingHost(
            processor.apvts.getParameter("pattern")->convertTo0to1(3.0f));
        processor.apvts.getParameter("mix")->setValueNotifyingHost(0.25f);
        processor.apvts.getParameter("keepPitch")->setValueNotifyingHost(1.0f);
        processor.getStateInformation(saved);
    }

    turtletime::TurtleTimeProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(restored.apvts.getRawParameterValue("mix")->load() == Approx(0.25f).margin(0.01f));
    REQUIRE(restored.apvts.getRawParameterValue("keepPitch")->load() > 0.5f);
    REQUIRE(restored.getCurrentProgram() == 3);
    REQUIRE(restored.getProgramName(3) == "Half 1/4");
}

TEST_CASE("turtletime: programs select patterns")
{
    turtletime::TurtleTimeProcessor processor;
    processor.setCurrentProgram(turtletime::dsp::kDefaultPattern);
    REQUIRE(processor.getCurrentProgram() == turtletime::dsp::kDefaultPattern);
    REQUIRE(processor.getProgramName(turtletime::dsp::kDefaultPattern) == "Half 1 Bar");
}

TEST_CASE("turtletime: mix zero stays dry")
{
    ProcessorHarness<turtletime::TurtleTimeProcessor> harness;
    harness.setParam(turtletime::ids::mix, 0.0f);
    harness.processor.prepareToPlay(kSampleRate, 256);

    juce::AudioBuffer<float> buffer(2, 1024);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float sample = std::sin(static_cast<float>(i) * 0.07f);
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample * 0.5f);
    }
    const juce::AudioBuffer<float> input = buffer;
    harness.processor.processBlock(buffer, harness.midi);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        REQUIRE(buffer.getSample(0, i) == Approx(input.getSample(0, i)).margin(0.0f));
        REQUIRE(buffer.getSample(1, i) == Approx(input.getSample(1, i)).margin(0.0f));
    }
}

TEST_CASE("turtletime: half speed reads the ramp at half rate")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, patternWhere(0.5f, 1.0, turtletime::dsp::TimeMode::CatchUp), 1.0f);
    constexpr double bpm = 240.0;
    constexpr int count = 4000;
    std::vector<float> input(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        input[static_cast<std::size_t>(i)] = 0.0002f * static_cast<float>(i);

    const auto output = render(engine, input, bpm, 256, true);
    for (int k = 4; k < 800; k += 2)
        REQUIRE(output[static_cast<std::size_t>(2 * k)] == Approx(input[static_cast<std::size_t>(k)]).margin(1.0e-4f));
}

TEST_CASE("turtletime: half speed drops a sine one octave")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, patternWhere(0.5f, 1.0, turtletime::dsp::TimeMode::CatchUp), 1.0f);
    constexpr double bpm = 240.0;
    constexpr int count = 12000;
    constexpr float frequency = 960.0f;
    std::vector<float> input(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        input[static_cast<std::size_t>(i)] = std::sin(2.0f * juce::MathConstants<float>::pi * frequency *
                                                      static_cast<float>(i) / static_cast<float>(kSampleRate));

    const auto output = render(engine, input, bpm, 512, true);
    int crossings = 0;
    for (int i = 3001; i < 9000; ++i)
        if (output[static_cast<std::size_t>(i - 1)] <= 0.0f && output[static_cast<std::size_t>(i)] > 0.0f)
            ++crossings;

    // 480 Hz over 6000 samples at 48 kHz is 60 rising zeros.
    REQUIRE(crossings == Approx(60).margin(2));
}

TEST_CASE("turtletime: keep pitch stretches time without dropping the pitch")
{
    constexpr double bpm = 240.0; // a half note == 24000 samples, one window
    constexpr int span = 24000;
    for (const float rate : {0.5f, 0.25f})
    {
        CAPTURE(rate);
        const int pattern = patternWhere(rate, 2.0, turtletime::dsp::TimeMode::CatchUp);

        turtletime::dsp::TurtleTimeEngine engine;
        prepareEngine(engine, pattern, 1.0f, true);
        const auto output = render(engine, sine(span, 960.0f, 0.8f), bpm, 512, true);
        // 960 Hz over 6000 samples at 48 kHz is 120 rising zeros; tape mode would give 120 * rate.
        REQUIRE(risingZeroCrossings(output, 3001, 9000) == Approx(120).margin(3));

        // A 100 ms burst now lasts 1 / rate times longer and is gone once the slow read position is past it.
        auto burst = sine(span, 960.0f, 0.8f);
        std::fill(burst.begin() + 4800, burst.end(), 0.0f);
        turtletime::dsp::TurtleTimeEngine stretched;
        prepareEngine(stretched, pattern, 1.0f, true);
        const auto held = render(stretched, burst, bpm, 512, true);
        const int burstEnd = static_cast<int>(4800.0f / rate);
        REQUIRE(rms(held, burstEnd / 2, burstEnd * 6 / 10) > 0.4f);
        REQUIRE(rms(held, burstEnd * 11 / 10 + 600, burstEnd * 11 / 10 + 1600) < 0.01f);
    }
}

TEST_CASE("turtletime: keep pitch holds a steady level across grains")
{
    constexpr double bpm = 240.0;
    constexpr int window = 12000;
    const float expected = 0.8f / std::sqrt(2.0f);
    for (const float rate : {0.5f, 0.25f})
        for (const int halfPeriod : {800, 480, 320, 200, 60, 16, 4}) // 30 Hz .. 6 kHz
        {
            const float frequency = static_cast<float>(kSampleRate) / static_cast<float>(2 * halfPeriod);
            CAPTURE(rate, frequency);
            turtletime::dsp::TurtleTimeEngine engine;
            prepareEngine(engine, patternWhere(rate, 1.0, turtletime::dsp::TimeMode::CatchUp), 1.0f, true);
            const auto output = render(engine, sine(3 * window, frequency, 0.8f), bpm, 256, true);

            // Whole half periods, so an unaltered sine measures exactly `expected` wherever the chunk starts.
            // The first window is skipped: right after prepare() there is no history for grains to align with.
            const int chunk = halfPeriod * ((480 + halfPeriod - 1) / halfPeriod);
            float lowest = 10.0f;
            float highest = 0.0f;
            for (int w = 1; w < 3; ++w)
                for (int start = w * window + 960; start + chunk <= (w + 1) * window; start += chunk)
                {
                    const float level = rms(output, start, start + chunk);
                    lowest = std::min(lowest, level);
                    highest = std::max(highest, level);
                }
            CAPTURE(lowest / expected, highest / expected);
            // Below ~50 Hz a period is longer than the alignment search, so some wobble remains.
            const float tolerance = frequency < 45.0f ? 0.15f : 0.05f;
            REQUIRE(lowest > expected * (1.0f - tolerance));
            REQUIRE(highest < expected * (1.0f + tolerance));
        }
}

TEST_CASE("turtletime: switching keep pitch mid-window stays click-free")
{
    constexpr double bpm = 240.0;
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, patternWhere(0.5f, 1.0, turtletime::dsp::TimeMode::CatchUp), 1.0f);
    const auto input = sine(24000, 440.0f, 0.8f);

    std::vector<float> output;
    constexpr int segment = 4000;
    for (int start = 0; start < 24000; start += segment)
    {
        engine.setKeepPitch((start / segment) % 2 == 1);
        const std::vector<float> part(input.begin() + start, input.begin() + start + segment);
        const auto rendered = render(engine, part, bpm, 300, true, start);
        output.insert(output.end(), rendered.begin(), rendered.end());
    }
    REQUIRE(maxStep(output) < 0.2f);
    REQUIRE(maxStep(output) == Approx(maxStep(input)).margin(0.1f));
}

TEST_CASE("turtletime: keep pitch does not depend on the block size")
{
    constexpr double bpm = 240.0;
    std::vector<float> input(15000);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = std::sin(static_cast<float>(i) * 0.017f) + 0.3f * std::sin(static_cast<float>(i) * 0.093f);

    const int pattern = patternWhere(0.5f, 1.0, turtletime::dsp::TimeMode::CatchUp);
    turtletime::dsp::TurtleTimeEngine fine;
    turtletime::dsp::TurtleTimeEngine coarse;
    prepareEngine(fine, pattern, 1.0f, true);
    prepareEngine(coarse, pattern, 1.0f, true);
    const auto a = render(fine, input, bpm, 1, true);
    const auto b = render(coarse, input, bpm, 173, true);
    for (std::size_t i = 0; i < input.size(); i += 10)
        REQUIRE(a[i] == Approx(b[i]).margin(0.02f));
}

TEST_CASE("turtletime: keep pitch leaves reverse and repeat alone")
{
    constexpr double bpm = 240.0;
    std::vector<float> input(30000);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = std::sin(static_cast<float>(i) * 0.01f);

    for (const int pattern : {patternWhere(-1.0f, 1.0, turtletime::dsp::TimeMode::CatchUp),
                              patternWhere(1.0f, 4.0, turtletime::dsp::TimeMode::Repeat, 0.5)})
    {
        CAPTURE(pattern);
        turtletime::dsp::TurtleTimeEngine tape;
        turtletime::dsp::TurtleTimeEngine kept;
        prepareEngine(tape, pattern, 1.0f, false);
        prepareEngine(kept, pattern, 1.0f, true);
        REQUIRE(render(tape, input, bpm, 512, true) == render(kept, input, bpm, 512, true));
    }
}

TEST_CASE("turtletime: pitch label follows the toggle")
{
    using turtletime::dsp::patternAt;
    using turtletime::dsp::pitchLabel;
    const int half = patternWhere(0.5f, 4.0, turtletime::dsp::TimeMode::CatchUp);
    const int slow = patternWhere(0.25f, 4.0, turtletime::dsp::TimeMode::CatchUp);
    const int reverse = patternWhere(-1.0f, 4.0, turtletime::dsp::TimeMode::CatchUp);
    REQUIRE(pitchLabel(patternAt(half), false) == "1 OCTAVE DOWN");
    REQUIRE(pitchLabel(patternAt(slow), false) == "2 OCTAVES DOWN");
    REQUIRE(pitchLabel(patternAt(half), true) == "PITCH KEPT");
    REQUIRE(pitchLabel(patternAt(reverse), false).empty());
    REQUIRE(pitchLabel(patternAt(reverse), true).empty());
}

TEST_CASE("turtletime: reverse plays the previous window backwards")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, patternWhere(-1.0f, 4.0, turtletime::dsp::TimeMode::CatchUp), 1.0f);
    constexpr double bpm = 240.0;
    constexpr int window = 48000;
    std::vector<float> input(static_cast<std::size_t>(window * 2));
    for (int i = 0; i < window * 2; ++i)
        input[static_cast<std::size_t>(i)] = std::sin(static_cast<float>(i) * 0.01f);

    const auto output = render(engine, input, bpm, 512, true);
    for (int n = 800; n < window; n += 17)
        REQUIRE(output[static_cast<std::size_t>(window + n)] ==
                Approx(input[static_cast<std::size_t>(window - n)]).margin(1.0e-4f));
}

TEST_CASE("turtletime: repeat retriggers inside the block")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, patternWhere(1.0f, 4.0, turtletime::dsp::TimeMode::Repeat, 0.5), 1.0f);
    constexpr double bpm = 1440.0; // 0.5 beats == 1000 samples
    constexpr int count = 3000;
    std::vector<float> input(static_cast<std::size_t>(count), -0.8f);
    for (int i = 0; i < 1000; ++i)
        input[static_cast<std::size_t>(i)] = 0.8f;

    const auto output = render(engine, input, bpm, 2048, true);
    REQUIRE(output[500] == Approx(0.8f).margin(0.02f));
    REQUIRE(output[1500] == Approx(0.8f).margin(0.02f));
    REQUIRE(output[2500] == Approx(0.8f).margin(0.02f));
}

TEST_CASE("turtletime: jumps stay click-free")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, patternWhere(0.5f, 1.0, turtletime::dsp::TimeMode::CatchUp), 1.0f);
    constexpr double bpm = 240.0;
    constexpr int count = 24000; // two 1-beat windows, so the snap is included
    std::vector<float> input(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        input[static_cast<std::size_t>(i)] = 0.8f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f *
                                                             static_cast<float>(i) / static_cast<float>(kSampleRate));

    const auto output = render(engine, input, bpm, 300, true);
    float maxDelta = 0.0f;
    for (int i = 1; i < count; ++i)
        maxDelta =
            std::max(maxDelta, std::abs(output[static_cast<std::size_t>(i)] - output[static_cast<std::size_t>(i - 1)]));

    REQUIRE(std::isfinite(maxDelta));
    REQUIRE(maxDelta < 0.2f);
}

TEST_CASE("turtletime: block size does not change the render")
{
    constexpr double bpm = 240.0;
    constexpr int count = 15000;
    std::vector<float> input(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        input[static_cast<std::size_t>(i)] = std::sin(static_cast<float>(i) * 0.017f);

    turtletime::dsp::TurtleTimeEngine fine;
    turtletime::dsp::TurtleTimeEngine coarse;
    const int pattern = patternWhere(0.5f, 1.0, turtletime::dsp::TimeMode::CatchUp);
    prepareEngine(fine, pattern, 1.0f);
    prepareEngine(coarse, pattern, 1.0f);
    const auto a = render(fine, input, bpm, 1, true);
    const auto b = render(coarse, input, bpm, 173, true);
    for (int i = 0; i < count; i += 10)
        REQUIRE(a[static_cast<std::size_t>(i)] == Approx(b[static_cast<std::size_t>(i)]).margin(0.02f));
}

TEST_CASE("turtletime: stopped transport stays dry and parks on the playhead")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, turtletime::dsp::kDefaultPattern, 1.0f);

    constexpr int count = 512;
    std::vector<float> input(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        input[static_cast<std::size_t>(i)] = 0.001f * static_cast<float>(i);

    auto stopped = input;
    float* channels[] = {stopped.data()};
    animalaudio::dsp::TransportInfo transport;
    transport.playing = false;
    transport.hasBpm = true;
    transport.bpm = 96.0;
    transport.hasPpq = true;
    transport.ppq = 1.0;
    engine.process(channels, 1, count, transport);

    for (int i = 0; i < count; ++i)
        REQUIRE(stopped[static_cast<std::size_t>(i)] == Approx(input[static_cast<std::size_t>(i)]).margin(0.0f));
    REQUIRE_FALSE(engine.isHostSynced());
    REQUIRE(engine.getPlayheadPhase() == Approx(0.25f).margin(1.0e-4f));
    REQUIRE(engine.getReadMarker() == Approx(engine.getPlayheadPhase()).margin(1.0e-4f));

    transport.ppq = 2.0;
    stopped = input;
    engine.process(channels, 1, count, transport);
    REQUIRE(engine.getPlayheadPhase() == Approx(0.5f).margin(1.0e-4f));
    REQUIRE(stopped[100] == Approx(input[100]).margin(0.0f));

    transport.playing = true;
    transport.ppq = 0.0;
    engine.process(channels, 1, count, transport);
    REQUIRE(engine.isHostSynced());
    for (float sample : stopped)
        REQUIRE(std::isfinite(sample));
}

TEST_CASE("turtletime: output stays finite")
{
    turtletime::dsp::TurtleTimeEngine engine;
    prepareEngine(engine, 0, 0.7f);
    constexpr int count = 20000;
    std::vector<float> input(static_cast<std::size_t>(count));
    unsigned state = 1u;
    for (int i = 0; i < count; ++i)
    {
        state = state * 1664525u + 1013904223u;
        input[static_cast<std::size_t>(i)] = static_cast<float>(static_cast<int>(state >> 16) % 2000 - 1000) / 1000.0f;
    }
    const auto output = render(engine, input, 120.0, 64, true);
    for (float sample : output)
    {
        REQUIRE(std::isfinite(sample));
        REQUIRE(std::abs(sample) < 4.0f);
    }
}

TEST_CASE("turtletime: editor renders headlessly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    turtletime::TurtleTimeProcessor processor;
    processor.setPlayConfigDetails(2, 2, kSampleRate, 512);
    processor.prepareToPlay(kSampleRate, 512);

    animalaudio::testing::FakePlayHead playHead;
    juce::AudioPlayHead::PositionInfo position;
    position.setIsPlaying(true);
    position.setBpm(120.0);
    position.setPpqPosition(0.0);
    playHead.position = position;
    processor.setPlayHead(&playHead);

    juce::MidiBuffer midi;
    double ppq = 0.0;
    const double beatsPerSample = 120.0 / (60.0 * kSampleRate);
    constexpr int beat = 24000; // 120 BPM at 48 kHz
    unsigned noise = 7u;
    for (int block = 0; block < 4 * beat * 7 / 8 / 512; ++block)
    {
        position.setPpqPosition(ppq);
        playHead.position = position;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < 512; ++i)
        {
            // A small drum loop: a kick on every beat and a hat on every 1/8, so the display has shape.
            const int n = i + block * 512;
            const float kickT = static_cast<float>(n % beat) / static_cast<float>(kSampleRate);
            const float kick =
                0.9f * std::exp(-kickT * 9.0f) *
                std::sin(2.0f * juce::MathConstants<float>::pi * 52.0f * kickT * (1.0f + std::exp(-kickT * 30.0f)));
            noise = noise * 1664525u + 1013904223u;
            const float white = static_cast<float>(static_cast<int>(noise >> 9) % 2000 - 1000) / 1000.0f;
            const float hat = 0.35f * white * std::exp(-static_cast<float>(n % (beat / 2)) / 900.0f);
            buffer.setSample(0, i, kick + hat);
            buffer.setSample(1, i, kick + hat * 0.8f);
        }
        processor.processBlock(buffer, midi);
        ppq += beatsPerSample * 512.0;
    }
    REQUIRE(processor.isHostSynced());

    std::unique_ptr<turtletime::TurtleTimeEditor> editor(
        static_cast<turtletime::TurtleTimeEditor*>(processor.createEditor()));
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    REQUIRE(image.getWidth() == turtletime::TurtleTimeEditor::kWidth * 2);
    REQUIRE(image.getHeight() == turtletime::TurtleTimeEditor::kHeight * 2);

    int bright = 0;
    for (int y = 0; y < image.getHeight(); y += 4)
        for (int x = 0; x < image.getWidth(); x += 4)
            if (image.getPixelAt(x, y).getBrightness() > 0.55f)
                ++bright;
    REQUIRE(bright > 80);
    writePngIfRequested(image, "turtletime-ui.png");
}

TEST_CASE("turtletime: editor pitch buttons set keep pitch")
{
    juce::ScopedJuceInitialiser_GUI gui;
    turtletime::TurtleTimeProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* keep = dynamic_cast<juce::Button*>(editor->findChildWithID("keepPitch"));
    auto* drop = dynamic_cast<juce::Button*>(editor->findChildWithID("dropPitch"));
    REQUIRE(keep != nullptr);
    REQUIRE(drop != nullptr);
    REQUIRE(drop->getToggleState());
    REQUIRE_FALSE(keep->getToggleState());

    keep->onClick();
    REQUIRE(processor.apvts.getRawParameterValue("keepPitch")->load() > 0.5f);
    REQUIRE(keep->getToggleState());
    REQUIRE_FALSE(drop->getToggleState());

    drop->onClick();
    REQUIRE(processor.apvts.getRawParameterValue("keepPitch")->load() < 0.5f);
    REQUIRE(drop->getToggleState());
    REQUIRE_FALSE(keep->getToggleState());
}

TEST_CASE("turtletime: build stamp is baked in")
{
    REQUIRE(animalaudio::core::buildStampShort().isNotEmpty());
}
