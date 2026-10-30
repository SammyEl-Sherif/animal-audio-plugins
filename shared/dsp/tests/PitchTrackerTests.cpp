#include <animalaudio/dsp/PitchTracker.h>
#include <animalaudio/testing/SignalProbe.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using animalaudio::dsp::PitchTracker;
using animalaudio::testing::centsBetween;
using animalaudio::testing::kPi;

namespace
{
/** Band-limited sawtooth-like tone (1/k harmonics): rich like a voice, with a weak fundamental share. */
std::vector<float> harmonicTone(double hz, double sampleRate, std::size_t count)
{
    std::vector<float> out(count, 0.0f);
    for (int k = 1; k * hz < 0.45 * sampleRate; ++k)
        for (std::size_t i = 0; i < count; ++i)
            out[i] += 0.3f / static_cast<float>(k) *
                      static_cast<float>(std::sin(2.0 * kPi * k * hz * static_cast<double>(i) / sampleRate));
    return out;
}

struct Run
{
    std::vector<PitchTracker::Estimate> estimates;
};

Run track(PitchTracker& tracker, const std::vector<float>& signal)
{
    Run run;
    for (const float x : signal)
        if (tracker.push(x))
            run.estimates.push_back(tracker.latest());
    return run;
}
} // namespace

TEST_CASE("PitchTracker finds harmonic tones within a few cents")
{
    const double rate = GENERATE(44100.0, 48000.0, 96000.0);
    const double hz = GENERATE(80.0, 110.0, 196.0, 330.0, 523.25, 880.0);
    PitchTracker tracker;
    tracker.prepare(rate);
    const auto run = track(tracker, harmonicTone(hz, rate, static_cast<std::size_t>(rate * 0.6)));

    int voiced = 0;
    double worst = 0.0;
    for (const auto& e : run.estimates)
    {
        if (e.centre < static_cast<std::int64_t>(rate * 0.1))
            continue;
        REQUIRE(e.voiced);
        ++voiced;
        worst = std::max(worst, std::abs(centsBetween(rate / e.period, hz)));
    }
    INFO("rate " << rate << " hz " << hz << " worst " << worst << " cents");
    REQUIRE(voiced > 20);
    REQUIRE(worst < 3.0);
}

TEST_CASE("PitchTracker calls noise and silence unvoiced")
{
    PitchTracker tracker;
    tracker.prepare(48000.0);
    const auto noise = track(tracker, animalaudio::testing::whiteNoise(48000, 0.4f, 7));
    int voiced = 0;
    for (const auto& e : noise.estimates)
        voiced += e.voiced ? 1 : 0;
    REQUIRE(voiced <= static_cast<int>(noise.estimates.size()) / 50);

    tracker.reset();
    const auto silence = track(tracker, std::vector<float>(48000, 0.0f));
    for (const auto& e : silence.estimates)
        REQUIRE_FALSE(e.voiced);
}

TEST_CASE("PitchTracker follows a glide")
{
    constexpr double rate = 48000.0;
    const auto count = static_cast<std::size_t>(rate * 1.2);
    std::vector<float> signal(count);
    std::vector<double> frequency(count);
    double phase = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        frequency[i] = 150.0 * std::pow(2.0, std::min(1.0, t)); // one octave per second
        phase += 2.0 * kPi * frequency[i] / rate;
        float v = 0.0f;
        for (int k = 1; k <= 8; ++k)
            v += 0.3f / static_cast<float>(k) * static_cast<float>(std::sin(k * phase));
        signal[i] = v;
    }
    PitchTracker tracker;
    tracker.prepare(rate);
    const auto run = track(tracker, signal);
    int checked = 0;
    int close = 0;
    for (const auto& e : run.estimates)
    {
        if (e.centre < static_cast<std::int64_t>(rate * 0.1))
            continue;
        ++checked;
        if (e.voiced && std::abs(centsBetween(rate / e.period, frequency[static_cast<std::size_t>(e.centre)])) < 25.0)
            ++close;
    }
    REQUIRE(checked > 100);
    REQUIRE(close >= checked * 95 / 100);
}

TEST_CASE("PitchTracker estimate centres trail the input by the advertised lag")
{
    PitchTracker tracker;
    tracker.prepare(48000.0);
    std::int64_t index = 0;
    for (const float x : harmonicTone(200.0, 48000.0, 24000))
    {
        if (tracker.push(x))
        {
            const auto lag = index - tracker.latest().centre;
            REQUIRE(lag >= 0);
            REQUIRE(lag <= tracker.getCentreLag());
        }
        ++index;
    }
}
