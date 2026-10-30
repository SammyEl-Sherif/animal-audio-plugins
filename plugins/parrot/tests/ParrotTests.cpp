#include "ParrotEditor.h"
#include "ParrotProcessor.h"
#include "dsp/ParrotEngine.h"

#include <animalaudio/core/BuildStamp.h>
#include <animalaudio/testing/SignalProbe.h>
#include <animalaudio/testing/TestKit.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

using animalaudio::testing::centsBetween;
using animalaudio::testing::kPi;
using animalaudio::testing::peakFrequency;
using animalaudio::testing::rms;
using animalaudio::testing::toneAmplitude;
using animalaudio::testing::writePngIfRequested;
using parrot::dsp::Mode;
using parrot::dsp::ParrotEngine;

namespace
{
constexpr double kRate = 48000.0;

/** Vowel /a/: harmonics of `pitchHz(t)` under three formant resonances (Peterson & Barney, adult male). */
std::vector<float> vowel(const std::function<double(double)>& pitchHz, double seconds, double rate = kRate,
                         float peak = 0.5f)
{
    struct Formant
    {
        double hz, bandwidth, gain;
    };
    static constexpr Formant formants[] = {{730.0, 90.0, 1.0}, {1090.0, 110.0, 0.5}, {2440.0, 170.0, 0.25}};
    auto envelope = [](double f)
    {
        double a = 0.0;
        for (const auto& formant : formants)
        {
            const double x = (f - formant.hz) / (0.5 * formant.bandwidth);
            a += formant.gain / (1.0 + x * x);
        }
        return a + 0.02;
    };
    const auto count = static_cast<std::size_t>(seconds * rate);
    std::vector<float> out(count);
    double phase = 0.0;
    float largest = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        const double f0 = pitchHz(static_cast<double>(i) / rate);
        phase += 2.0 * kPi * f0 / rate;
        double v = 0.0;
        for (int k = 1; k * f0 < 0.45 * rate && k * f0 < 6000.0; ++k)
            v += envelope(k * f0) * std::sin(k * phase);
        out[i] = static_cast<float>(v);
        largest = std::max(largest, std::abs(out[i]));
    }
    for (auto& v : out)
        v *= peak / largest;
    return out;
}

std::vector<float> steadyVowel(double hz, double seconds, double rate = kRate)
{
    return vowel([hz](double) { return hz; }, seconds, rate);
}

struct Settings
{
    float pitch = 0.0f;
    float formant = 0.0f;
    bool fine = true;
    bool link = false;
    Mode mode = Mode::Shift;
    int note = parrot::dsp::kDefaultNote;
    float glide = 0.0f;
    float drive = 0.0f;
    float mix = 1.0f;
};

void apply(ParrotEngine& engine, const Settings& s)
{
    engine.setPitch(s.pitch);
    engine.setFormant(s.formant);
    engine.setFine(s.fine);
    engine.setLink(s.link);
    engine.setMode(s.mode);
    engine.setNote(s.note);
    engine.setGlide(s.glide);
    engine.setDrive(s.drive);
    engine.setMix(s.mix);
}

std::vector<float> render(ParrotEngine& engine, const std::vector<float>& input, int blockSize = 512,
                          const std::function<void(ParrotEngine&, std::size_t)>& beforeBlock = {})
{
    std::vector<float> out = input;
    for (std::size_t done = 0; done < out.size(); done += static_cast<std::size_t>(blockSize))
    {
        if (beforeBlock)
            beforeBlock(engine, done);
        float* channel[] = {out.data() + done};
        engine.process(channel, 1, static_cast<int>(std::min(static_cast<std::size_t>(blockSize), out.size() - done)));
    }
    return out;
}

std::vector<float> process(const Settings& settings, const std::vector<float>& input, double rate = kRate,
                           int blockSize = 512)
{
    ParrotEngine engine;
    apply(engine, settings);
    engine.prepare(rate);
    return render(engine, input, blockSize);
}

/** Spectral envelope in dB on a quarter-semitone grid (300 Hz .. 4 kHz), from the harmonics of `f0`. */
std::vector<double> envelopeDb(const std::vector<float>& x, std::size_t begin, std::size_t count, double f0)
{
    std::vector<double> freqs;
    std::vector<double> levels;
    for (int k = 1; k * f0 < 6000.0; ++k)
    {
        freqs.push_back(std::log2(k * f0));
        levels.push_back(20.0 * std::log10(toneAmplitude(x, begin, count, k * f0, kRate) + 1.0e-9));
    }
    std::vector<double> grid;
    for (double f = std::log2(300.0); f <= std::log2(4000.0); f += 1.0 / 48.0)
    {
        std::size_t i = 1;
        while (i + 1 < freqs.size() && freqs[i] < f)
            ++i;
        const double t = std::clamp((f - freqs[i - 1]) / (freqs[i] - freqs[i - 1]), 0.0, 1.0);
        grid.push_back(levels[i - 1] + t * (levels[i] - levels[i - 1]));
    }
    return grid;
}

/** How many semitones the output's formants moved relative to the input's (best envelope alignment). */
double formantShift(const std::vector<double>& input, const std::vector<double>& output)
{
    double best = 0.0;
    double bestScore = -1.0e300;
    for (int shift = -14 * 4; shift <= 14 * 4; ++shift)
    {
        double sum = 0.0;
        int n = 0;
        for (int i = 0; i < static_cast<int>(output.size()); ++i)
        {
            const int j = i - shift;
            if (j < 0 || j >= static_cast<int>(input.size()))
                continue;
            sum += -std::abs(output[static_cast<std::size_t>(i)] - input[static_cast<std::size_t>(j)]);
            ++n;
        }
        if (n < static_cast<int>(output.size()) / 3)
            continue;
        const double score = sum / n;
        if (score > bestScore)
        {
            bestScore = score;
            best = shift / 4.0;
        }
    }
    return best;
}

/** Tail of a render, after latency and settling. */
struct Tail
{
    std::size_t begin;
    std::size_t count;
};

bool identical(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

Tail tailOf(const std::vector<float>& x, double seconds = 0.5)
{
    const auto count = static_cast<std::size_t>(seconds * kRate);
    return {x.size() - count, count};
}
} // namespace

TEST_CASE("parrot: shift lands on the interval within a few cents")
{
    const double input = GENERATE(110.0, 196.0, 330.0);
    const float semitones = GENERATE(-12.0f, -7.0f, -3.0f, 0.0f, 4.0f, 7.0f, 12.0f);
    Settings s;
    s.pitch = semitones;
    const auto out = process(s, steadyVowel(input, 1.5));
    const auto tail = tailOf(out);
    const double expected = input * std::pow(2.0, semitones / 12.0);
    const double measured = peakFrequency(out, tail.begin, tail.count, kRate, expected * 0.97, expected * 1.03, 1.0);
    INFO("input " << input << " shift " << semitones << " expected " << expected << " measured " << measured);
    REQUIRE(std::abs(centsBetween(measured, expected)) < 1.0);
    // A real partial sits there: well above the spectrum three window bins away.
    const double away = expected + 6.0 / (static_cast<double>(tail.count) / kRate);
    REQUIRE(toneAmplitude(out, tail.begin, tail.count, expected, kRate) >
            4.0 * toneAmplitude(out, tail.begin, tail.count, away, kRate));
}

TEST_CASE("parrot: level holds across shifts")
{
    const float semitones = GENERATE(-12.0f, -5.0f, 0.0f, 7.0f, 12.0f);
    const bool link = GENERATE(false, true);
    Settings s;
    s.pitch = semitones;
    s.link = link;
    const auto input = steadyVowel(196.0, 1.5);
    const auto out = process(s, input);
    const auto tail = tailOf(out);
    const double db = 20.0 * std::log10(rms(out, tail.begin, out.size()) / rms(input, tail.begin, input.size()));
    INFO("shift " << semitones << " link " << link << " level " << db << " dB");
    REQUIRE(std::abs(db) < 0.5);
}

TEST_CASE("parrot: formants stay put unless FORMANT or LINK moves them")
{
    const auto input = steadyVowel(120.0, 1.5);
    const auto inTail = tailOf(input);
    const auto reference = envelopeDb(input, inTail.begin, inTail.count, 120.0);

    auto shiftFor = [&](float pitch, float formant, bool link)
    {
        Settings s;
        s.pitch = pitch;
        s.formant = formant;
        s.link = link;
        const auto out = process(s, input);
        const auto tail = tailOf(out);
        return formantShift(reference, envelopeDb(out, tail.begin, tail.count, 120.0 * std::pow(2.0, pitch / 12.0)));
    };

    const double pitchUp = shiftFor(7.0f, 0.0f, false);
    const double pitchDown = shiftFor(-7.0f, 0.0f, false);
    const double formantUp = shiftFor(0.0f, 7.0f, false);
    const double formantDown = shiftFor(0.0f, -7.0f, false);
    const double linked = shiftFor(5.0f, 0.0f, true);
    const double both = shiftFor(-5.0f, 3.0f, false);
    INFO("formant moved (st): pitch +7 " << pitchUp << ", pitch -7 " << pitchDown << ", formant +7 " << formantUp
                                         << ", formant -7 " << formantDown << ", linked +5 " << linked
                                         << ", pitch -5 formant +3 " << both);
    REQUIRE(std::abs(pitchUp) <= 0.5);
    REQUIRE(std::abs(pitchDown) <= 0.5);
    REQUIRE(std::abs(formantUp - 7.0) <= 0.5);
    REQUIRE(std::abs(formantDown + 7.0) <= 0.5);
    REQUIRE(std::abs(linked - 5.0) <= 0.5);
    REQUIRE(std::abs(both - 3.0) <= 0.5);
}

TEST_CASE("parrot: noise passes without a pitch buzz")
{
    Settings s;
    s.pitch = 7.0f;
    s.formant = 3.0f;
    const auto input = animalaudio::testing::whiteNoise(static_cast<std::size_t>(kRate), 0.3f, 5);
    const auto out = process(s, input);
    const auto tail = tailOf(out);
    const double db = 20.0 * std::log10(rms(out, tail.begin, out.size()) / rms(input, tail.begin, input.size()));
    INFO("noise level " << db << " dB");
    REQUIRE(std::abs(db) < 3.0);

    double energy = 0.0;
    for (std::size_t i = tail.begin; i < out.size(); ++i)
        energy += static_cast<double>(out[i]) * out[i];
    for (int lag = 120; lag <= 600; lag += 4)
    {
        double sum = 0.0;
        for (std::size_t i = tail.begin; i + static_cast<std::size_t>(lag) < out.size(); ++i)
            sum += static_cast<double>(out[i]) * out[i + static_cast<std::size_t>(lag)];
        INFO("lag " << lag);
        REQUIRE(std::abs(sum / energy) < 0.1);
    }
}

TEST_CASE("parrot: snap pulls a sharp note onto the semitone")
{
    Settings s;
    s.mode = Mode::Snap;
    const double sharp = 440.0 * std::pow(2.0, 0.42 / 12.0);
    const auto out = process(s, steadyVowel(sharp, 1.5));
    const auto tail = tailOf(out);
    const double measured = peakFrequency(out, tail.begin, tail.count, kRate, 420.0, 470.0, 1.0);
    INFO("measured " << measured);
    REQUIRE(std::abs(centsBetween(measured, 440.0)) < 5.0);
}

TEST_CASE("parrot: robot flattens vibrato onto the note")
{
    Settings s;
    s.mode = Mode::Robot;
    const auto input =
        vowel([](double t) { return 196.0 * std::pow(2.0, 0.5 / 12.0 * std::sin(2.0 * kPi * 5.5 * t)); }, 1.5);
    const auto out = process(s, input);
    const auto tail = tailOf(out);
    const double c3 = 440.0 * std::pow(2.0, (48 - 69) / 12.0);
    const double measured = peakFrequency(out, tail.begin, tail.count, kRate, c3 * 0.9, c3 * 1.1, 1.0);
    INFO("measured " << measured << " expected " << c3);
    REQUIRE(std::abs(centsBetween(measured, c3)) < 5.0);
    // Flat: the input's vibrato sidebands are gone.
    REQUIRE(toneAmplitude(out, tail.begin, tail.count, c3, kRate) >
            4.0 * toneAmplitude(out, tail.begin, tail.count, c3 * std::pow(2.0, 0.4 / 12.0), kRate));
}

TEST_CASE("parrot: a held MIDI note steers robot mode")
{
    animalaudio::testing::ProcessorHarness<parrot::ParrotProcessor> harness(kRate, 512);
    harness.setParam(parrot::ids::mode, 2.0f);
    const auto input = steadyVowel(196.0, 1.5);
    std::vector<float> out(input.size());
    for (std::size_t done = 0; done < input.size(); done += 512)
    {
        const int n = static_cast<int>(std::min<std::size_t>(512, input.size() - done));
        juce::AudioBuffer<float> block(2, n);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, input.data() + done, n);
        juce::MidiBuffer midi;
        if (done == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        harness.processor.processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + static_cast<long>(done));
    }
    REQUIRE(harness.processor.getEngine().getHeldNote() == 57);
    const auto tail = tailOf(out);
    const double measured = peakFrequency(out, tail.begin, tail.count, kRate, 200.0, 240.0, 1.0);
    REQUIRE(std::abs(centsBetween(measured, 220.0)) < 5.0);
}

TEST_CASE("parrot: without FINE the knobs move in semitones")
{
    Settings s;
    s.pitch = 2.4f;
    s.fine = false;
    const auto out = process(s, steadyVowel(196.0, 1.5));
    const auto tail = tailOf(out);
    const double expected = 196.0 * std::pow(2.0, 2.0 / 12.0);
    const double measured = peakFrequency(out, tail.begin, tail.count, kRate, expected * 0.97, expected * 1.03, 1.0);
    REQUIRE(std::abs(centsBetween(measured, expected)) < 1.0);

    ParrotEngine engine;
    apply(engine, s);
    REQUIRE(engine.appliedPitch() == Catch::Approx(2.0f).margin(1e-6));
    engine.setFine(true);
    REQUIRE(engine.appliedPitch() == Catch::Approx(2.4f));
    engine.setLink(true);
    engine.setFormant(11.0f);
    REQUIRE(engine.appliedFormant() == Catch::Approx(12.0f).margin(1e-6));
}

TEST_CASE("parrot: neutral settings reproduce the input, so a 50% mix does not comb")
{
    ParrotEngine engine;
    Settings s;
    s.mix = 0.5f;
    apply(engine, s);
    engine.prepare(kRate);
    const auto input = steadyVowel(196.0, 1.5);
    const auto out = render(engine, input);
    const int latency = engine.getLatencySamples();
    const auto tail = tailOf(out);
    double error = 0.0;
    double energy = 0.0;
    for (std::size_t i = tail.begin; i < out.size(); ++i)
    {
        const double reference = input[i - static_cast<std::size_t>(latency)];
        error += (out[i] - reference) * (out[i] - reference);
        energy += reference * reference;
    }
    INFO("residual " << 10.0 * std::log10(error / energy) << " dB");
    REQUIRE(error / energy < 1.0e-3);
}

TEST_CASE("parrot: no grain is ever late or reads ahead of the input")
{
    const double rate = GENERATE(44100.0, 48000.0, 96000.0);
    const float formant = GENERATE(-12.0f, 0.0f, 12.0f);
    const float pitch = GENERATE(-12.0f, 12.0f);
    // Lowest to highest voice and back (a fast glide), alternating with noise.
    auto voice = vowel(
        [](double t)
        {
            const double cycle = std::fmod(t, 0.8) / 0.8;
            return 75.0 * std::pow(2.0, 3.6 * (cycle < 0.5 ? 2.0 * cycle : 2.0 - 2.0 * cycle));
        },
        1.6, rate);
    const auto noise = animalaudio::testing::whiteNoise(voice.size(), 0.3f, 9);
    for (std::size_t i = 0; i < voice.size(); ++i)
        if (static_cast<std::size_t>(static_cast<double>(i) / rate * 10.0) % 4 == 3)
            voice[i] = noise[i];

    for (const Mode mode : {Mode::Shift, Mode::Robot})
    {
        ParrotEngine engine;
        Settings s;
        s.pitch = pitch;
        s.formant = formant;
        s.mode = mode;
        apply(engine, s);
        engine.prepare(rate);
        const auto out = render(engine, voice, 333);
        const auto& stats = engine.getShifter().diagnostics();
        INFO("rate " << rate << " pitch " << pitch << " formant " << formant << " mode " << static_cast<int>(mode)
                     << " grains " << stats.grains << " late " << stats.lateGrains << " future " << stats.futureReads
                     << " dropped " << stats.droppedGrains << " blind " << stats.blindGrains);
        REQUIRE(stats.voicedGrains > 50);
        REQUIRE(stats.lateGrains == 0);
        REQUIRE(stats.futureReads == 0);
        REQUIRE(stats.droppedGrains == 0);
        REQUIRE(stats.blindGrains == 0);
        for (const float v : out)
            REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("parrot: cpu cost", "[.bench]")
{
    Settings s;
    s.pitch = 5.0f;
    s.formant = -3.0f;
    s.drive = 0.5f;
    const auto mono = vowel([](double t) { return 150.0 + 60.0 * std::sin(t); }, 10.0, 48000.0);
    ParrotEngine engine;
    apply(engine, s);
    engine.prepare(48000.0);
    std::vector<float> left(mono), right(mono);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i + 512 <= mono.size(); i += 512)
    {
        float* channels[] = {left.data() + i, right.data() + i};
        engine.process(channels, 2, 512);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    WARN("10 s of stereo at 48 kHz took " << seconds << " s (" << 100.0 * seconds / 10.0 << "% of a core)");
}

TEST_CASE("parrot: block size does not change the render")
{
    Settings s;
    s.pitch = 5.0f;
    s.formant = -3.0f;
    s.drive = 0.4f;
    const auto input = vowel([](double t) { return 180.0 + 40.0 * t; }, 0.8);
    const auto a = process(s, input, kRate, 1);
    const auto b = process(s, input, kRate, 512);
    const auto c = process(s, input, kRate, 37);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(identical(a[i], b[i]));
        REQUIRE(identical(a[i], c[i]));
    }
}

TEST_CASE("parrot: parameter jumps stay click-free")
{
    const auto input = steadyVowel(196.0, 2.0);
    ParrotEngine engine;
    apply(engine, Settings{});
    engine.prepare(kRate);
    const auto steady = render(engine, input);

    ParrotEngine jumping;
    apply(jumping, Settings{});
    jumping.prepare(kRate);
    const auto out = render(jumping, input, 512,
                            [](ParrotEngine& e, std::size_t done)
                            {
                                const auto step = done / 9600;
                                e.setPitch(step % 2 == 0 ? 0.0f : 12.0f);
                                e.setFormant(step % 3 == 0 ? 0.0f : -9.0f);
                                e.setMix(step % 4 == 3 ? 0.3f : 1.0f);
                            });
    const double reference = animalaudio::testing::largestStep(steady, 24000, steady.size());
    const double worst = animalaudio::testing::largestStep(out, 24000, out.size());
    INFO("steady step " << reference << " worst step " << worst);
    REQUIRE(worst < 2.0 * reference);
}

TEST_CASE("parrot: drive saturates the voice at a matched level")
{
    Settings clean;
    Settings hot;
    hot.drive = 0.8f;
    const auto input = steadyVowel(220.0, 1.0);
    const auto a = process(clean, input);
    const auto b = process(hot, input);
    const auto tail = tailOf(a);
    const double db = 20.0 * std::log10(rms(b, tail.begin, b.size()) / rms(a, tail.begin, a.size()));
    INFO("drive level change " << db << " dB");
    REQUIRE(std::abs(db) < 3.0);
    double difference = 0.0;
    for (std::size_t i = tail.begin; i < a.size(); ++i)
        difference += (a[i] - b[i]) * (a[i] - b[i]);
    REQUIRE(difference > 1.0);
}

TEST_CASE("parrot: processor reports the engine latency")
{
    const double rate = GENERATE(44100.0, 48000.0, 96000.0);
    animalaudio::testing::ProcessorHarness<parrot::ParrotProcessor> harness(rate, 256);
    REQUIRE(harness.processor.getLatencySamples() == harness.processor.getEngine().getLatencySamples());
    REQUIRE(harness.processor.getLatencySamples() > 0);
    REQUIRE(harness.processor.getLatencySamples() < static_cast<int>(rate * 0.1));
}

TEST_CASE("parrot: parameters exist with their defaults")
{
    parrot::ParrotProcessor processor;
    auto value = [&](const juce::ParameterID& id)
    {
        auto* p = processor.apvts.getParameter(id.getParamID());
        REQUIRE(p != nullptr);
        return p->convertFrom0to1(p->getValue());
    };
    REQUIRE(value(parrot::ids::pitch) == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(value(parrot::ids::formant) == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(value(parrot::ids::fine) < 0.5f);
    REQUIRE(value(parrot::ids::link) < 0.5f);
    REQUIRE(juce::roundToInt(value(parrot::ids::mode)) == 0);
    REQUIRE(juce::roundToInt(value(parrot::ids::note)) == parrot::dsp::kDefaultNote - parrot::dsp::kLowestNote);
    REQUIRE(value(parrot::ids::glide) == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(value(parrot::ids::drive) == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(value(parrot::ids::mix) == Catch::Approx(1.0f).margin(1e-6));
    REQUIRE(processor.acceptsMidi());
    REQUIRE(parrot::dsp::noteName(60) == "C4");
    REQUIRE(parrot::dsp::noteName(parrot::dsp::kDefaultNote) == "C3");
}

TEST_CASE("parrot: state round trip")
{
    animalaudio::testing::ProcessorHarness<parrot::ParrotProcessor> a;
    a.setParam(parrot::ids::pitch, 5.0f);
    a.setParam(parrot::ids::formant, -3.5f);
    a.setParam(parrot::ids::link, 1.0f);
    a.setParam(parrot::ids::mode, 1.0f);
    a.setParam(parrot::ids::drive, 0.6f);
    juce::MemoryBlock state;
    a.processor.getStateInformation(state);

    animalaudio::testing::ProcessorHarness<parrot::ParrotProcessor> b;
    b.processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto value = [&](const juce::ParameterID& id)
    {
        auto* p = b.processor.apvts.getParameter(id.getParamID());
        return p->convertFrom0to1(p->getValue());
    };
    REQUIRE(value(parrot::ids::pitch) == Catch::Approx(5.0f));
    REQUIRE(value(parrot::ids::formant) == Catch::Approx(-3.5f));
    REQUIRE(value(parrot::ids::link) > 0.5f);
    REQUIRE(juce::roundToInt(value(parrot::ids::mode)) == 1);
    REQUIRE(value(parrot::ids::drive) == Catch::Approx(0.6f).margin(0.001));
}

TEST_CASE("parrot: editor renders headlessly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    animalaudio::testing::ProcessorHarness<parrot::ParrotProcessor> harness(kRate, 512);
    harness.setParam(parrot::ids::pitch, 7.0f);
    harness.setParam(parrot::ids::formant, -3.0f);
    harness.setParam(parrot::ids::drive, 0.45f);

    // A short sung line (A3, C4, E4, D4) with portamento and vibrato, so the pitch roll has something to show.
    const auto phrase = vowel(
        [](double t)
        {
            static constexpr double notes[] = {57.0, 60.0, 64.0, 62.0, 62.0};
            const double position = std::min(t / 0.9, 3.999);
            const auto index = static_cast<std::size_t>(position);
            const double slide = std::clamp((position - static_cast<double>(index)) * 9.0 - 8.0, 0.0, 1.0);
            const double note = notes[index] + (notes[index + 1] - notes[index]) * slide +
                                0.3 * std::sin(2.0 * kPi * 5.5 * t) * std::min(1.0, t);
            return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        },
        3.4);
    juce::AudioBuffer<float> block(2, 512);
    for (std::size_t done = 0; done + 512 <= phrase.size(); done += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, phrase.data() + done, 512);
        harness.processor.processBlock(block, harness.midi);
    }

    std::unique_ptr<parrot::ParrotEditor> editor(static_cast<parrot::ParrotEditor*>(harness.processor.createEditor()));
    for (int i = 0; i < 90; ++i)
        editor->refresh();
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    REQUIRE(image.getWidth() == parrot::ParrotEditor::kWidth * 2);
    REQUIRE(image.getHeight() == parrot::ParrotEditor::kHeight * 2);

    int bright = 0;
    int scarlet = 0;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
        {
            const auto pixel = image.getPixelAt(x, y);
            bright += pixel.getBrightness() > 0.55f ? 1 : 0;
            const bool inDisplay = x > 560 && x < 1330 && y > 300 && y < 650;
            scarlet += inDisplay && pixel.getFloatRed() > 0.8f && pixel.getFloatGreen() < 0.45f ? 1 : 0;
        }
    REQUIRE(bright > 400);
    REQUIRE(scarlet > 150); // the output trace
    writePngIfRequested(image, "parrot-ui.png");
}

TEST_CASE("parrot: editor controls drive their parameters")
{
    juce::ScopedJuceInitialiser_GUI gui;
    parrot::ParrotProcessor processor;
    std::unique_ptr<parrot::ParrotEditor> editor(static_cast<parrot::ParrotEditor*>(processor.createEditor()));
    const auto get = [&](const juce::ParameterID& id)
    {
        const auto* param = processor.apvts.getParameter(id.getParamID());
        return param->convertFrom0to1(param->getValue());
    };

    editor->getPitchKnob().setValue(5.0, juce::sendNotificationSync);
    REQUIRE(get(parrot::ids::pitch) == Catch::Approx(5.0f));
    editor->getFormantKnob().setValue(-4.0, juce::sendNotificationSync);
    REQUIRE(get(parrot::ids::formant) == Catch::Approx(-4.0f));

    animalaudio::testing::click(editor->getFineButton());
    REQUIRE(get(parrot::ids::fine) > 0.5f);
    animalaudio::testing::click(editor->getLinkButton());
    REQUIRE(get(parrot::ids::link) > 0.5f);
    animalaudio::testing::click(editor->getModePills().getPill(2));
    REQUIRE(processor.getMode() == Mode::Robot);

    const float note = get(parrot::ids::note);
    animalaudio::testing::click(editor->getNoteUpButton());
    REQUIRE(get(parrot::ids::note) == Catch::Approx(note + 1.0f));
    animalaudio::testing::click(editor->getNoteDownButton());
    animalaudio::testing::click(editor->getNoteDownButton());
    REQUIRE(get(parrot::ids::note) == Catch::Approx(note - 1.0f));

    auto* pitch = processor.apvts.getParameter(parrot::ids::pitch.getParamID());
    pitch->setValueNotifyingHost(pitch->convertTo0to1(-7.0f));
    REQUIRE(editor->getPitchKnob().getValue() == Catch::Approx(-7.0));
}

TEST_CASE("parrot: build stamp is baked in")
{
    REQUIRE(animalaudio::core::buildStampShort().isNotEmpty());
}
