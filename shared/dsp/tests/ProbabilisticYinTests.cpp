#include <animalaudio/dsp/ChangePoints.h>
#include <animalaudio/dsp/Notes.h>
#include <animalaudio/dsp/ProbabilisticYin.h>
#include <animalaudio/testing/SignalProbe.h>
#include <animalaudio/testing/VoiceSynth.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cmath>
#include <vector>

using animalaudio::dsp::hzToMidi;
using animalaudio::dsp::ProbabilisticYin;
using animalaudio::dsp::segmentConstantMean;
using animalaudio::testing::kPi;
using animalaudio::testing::SungNote;

namespace
{
double midiOf(float hz)
{
    return static_cast<double>(hzToMidi(hz));
}
} // namespace

TEST_CASE("ProbabilisticYin: steady vowels are tracked to within a few cents across the voice range")
{
    const double rate = GENERATE(44100.0, 48000.0, 96000.0);
    const double hz = GENERATE(82.4, 110.0, 196.0, 440.0, 880.0);
    const auto signal = animalaudio::testing::vowel([hz](double) { return hz; }, 1.0, rate);
    const auto track = ProbabilisticYin::analyse(signal.data(), static_cast<std::int64_t>(signal.size()), rate);
    int voiced = 0;
    int checked = 0;
    double worstCents = 0.0;
    for (const auto& f : track.frames)
    {
        const double t = f.centre / rate;
        if (t < 0.05 || t > 0.95)
            continue;
        ++checked;
        if (f.hz <= 0.0f)
            continue;
        ++voiced;
        worstCents = std::max(worstCents, std::abs(1200.0 * std::log2(f.hz / hz)));
    }
    INFO("rate " << rate << " hz " << hz << " voiced " << voiced << "/" << checked << " worst " << worstCents);
    REQUIRE(voiced == checked);
    REQUIRE(worstCents < 3.0);
}

TEST_CASE("ProbabilisticYin: a sung phrase with vibrato, leaps, glides, consonants and rests")
{
    const double rate = GENERATE(44100.0, 48000.0);
    // A3 (vibrato) -> E4 legato glide -> rest -> "s" + A4 -> octave leap down to A3 -> "t" + D3 with breathy vibrato.
    const std::vector<SungNote> notes{{57.0, 0.10, 0.60, 35.0, 5.5, 0.0, 0.0, 0.0, 1.0f},
                                      {64.0, 0.70, 0.45, 20.0, 6.0, -15.0, 0.0, 0.08, 0.9f},
                                      {69.0, 1.45, 0.40, 0.0, 5.5, 10.0, 0.08, 0.0, 0.8f},
                                      {57.0, 1.85, 0.35, 0.0, 5.5, 0.0, 0.0, 0.03, 1.0f},
                                      {50.0, 2.40, 0.70, 50.0, 5.0, 0.0, 0.06, 0.0, 0.7f}};
    const auto signal = animalaudio::testing::sing(notes, 3.3, rate);
    const auto track = ProbabilisticYin::analyse(signal.data(), static_cast<std::int64_t>(signal.size()), rate);

    int voicedChecked = 0;
    int wrongPitch = 0;
    int missedVoicing = 0;
    int glideFrames = 0;
    int glideOctaveErrors = 0;
    int silentChecked = 0;
    int falseVoicing = 0;
    double worstCents = 0.0;
    for (const auto& f : track.frames)
    {
        const double t = f.centre / rate;
        // Ground truth over a +-12 ms neighbourhood: skip frames that straddle an edge.
        const auto a = animalaudio::testing::sungAt(notes, t - 0.012);
        const auto b = animalaudio::testing::sungAt(notes, t);
        const auto c = animalaudio::testing::sungAt(notes, t + 0.012);
        if (a.midi > 0.0 && b.midi > 0.0 && c.midi > 0.0 && b.amplitude > 0.5)
        {
            // Inside a fast slide the period changes within YIN's window: only octave errors count there.
            const bool sliding = std::abs(c.midi - a.midi) / 0.024 > 30.0;
            if (sliding)
            {
                ++glideFrames;
                glideOctaveErrors += f.hz > 0.0f && std::abs(midiOf(f.hz) - b.midi) > 3.0 ? 1 : 0;
                continue;
            }
            ++voicedChecked;
            if (f.hz <= 0.0f)
            {
                ++missedVoicing;
                continue;
            }
            const double cents = std::abs(midiOf(f.hz) - b.midi) * 100.0;
            worstCents = std::max(worstCents, cents);
            wrongPitch += cents > 25.0 ? 1 : 0;
        }
        else if (a.midi < 0.0 && b.midi < 0.0 && c.midi < 0.0)
        {
            ++silentChecked;
            falseVoicing += f.hz > 0.0f ? 1 : 0;
        }
    }
    INFO("rate " << rate << " voiced " << voicedChecked << " missed " << missedVoicing << " wrong " << wrongPitch
                 << " worst " << worstCents << " cents; glide frames " << glideFrames << " octave errors "
                 << glideOctaveErrors << "; unvoiced " << silentChecked << " false " << falseVoicing);
    REQUIRE(voicedChecked > 300);
    REQUIRE(wrongPitch == 0);
    REQUIRE(missedVoicing <= voicedChecked / 100);
    REQUIRE(glideOctaveErrors == 0);
    REQUIRE(silentChecked > 50);
    REQUIRE(falseVoicing <= silentChecked / 20);
}

TEST_CASE("ProbabilisticYin: noise and silence are unvoiced")
{
    const double rate = 48000.0;
    auto signal = animalaudio::testing::whiteNoise(static_cast<std::size_t>(rate), 0.3f, 3);
    signal.resize(static_cast<std::size_t>(rate * 1.5), 0.0f);
    const auto track = ProbabilisticYin::analyse(signal.data(), static_cast<std::int64_t>(signal.size()), rate);
    int voiced = 0;
    for (const auto& f : track.frames)
        voiced += f.hz > 0.0f ? 1 : 0;
    INFO("voiced frames " << voiced << " of " << track.frames.size());
    REQUIRE(voiced <= static_cast<int>(track.frames.size()) / 50);
}

TEST_CASE("ProbabilisticYin: frames are evenly spaced and the analysis is fast")
{
    const double rate = 48000.0;
    const auto signal = animalaudio::testing::vowel([](double t) { return 150.0 + 60.0 * std::sin(t); }, 20.0, rate);
    const auto start = std::chrono::steady_clock::now();
    const auto track = ProbabilisticYin::analyse(signal.data(), static_cast<std::int64_t>(signal.size()), rate);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(track.hop > 0);
    REQUIRE(track.frames.size() == static_cast<std::size_t>(signal.size() / static_cast<std::size_t>(track.hop) + 1));
    for (std::size_t i = 0; i < track.frames.size(); ++i)
        REQUIRE(track.frames[i].centre == static_cast<double>(i * static_cast<std::size_t>(track.hop)));
    INFO("20 s analysed in " << seconds << " s");
    CHECK(seconds < 4.0);
}

TEST_CASE("ProbabilisticYin: cancelling returns an empty track")
{
    const auto signal = animalaudio::testing::vowel([](double) { return 200.0; }, 3.0, 48000.0);
    const auto track = ProbabilisticYin::analyse(signal.data(), static_cast<std::int64_t>(signal.size()), 48000.0, {},
                                                 [](float p) { return p < 0.2f; });
    REQUIRE(track.frames.empty());
}

TEST_CASE("segmentConstantMean finds steps and ignores vibrato")
{
    std::vector<float> values;
    std::vector<float> weights;
    std::uint32_t state = 5;
    const auto noise = [&]
    {
        state = state * 1664525u + 1013904223u;
        return (static_cast<float>(state >> 8) / static_cast<float>(1u << 24) - 0.5f) * 0.2f;
    };
    const float levels[] = {57.0f, 59.0f, 59.0f, 64.0f, 62.5f};
    const int lengths[] = {80, 60, 0, 120, 70};
    for (int s = 0; s < 5; ++s)
        for (int i = 0; i < lengths[s]; ++i)
        {
            // +-0.5 semitone vibrato on top of each level
            values.push_back(levels[s] + 0.5f * static_cast<float>(std::sin(2.0 * kPi * i / 30.0)) + noise());
            weights.push_back(1.0f);
        }
    const auto starts = segmentConstantMean(values, weights, 8.0, 10);
    REQUIRE(starts.size() == 4);
    REQUIRE(starts[0] == 0);
    REQUIRE(std::abs(starts[1] - 80) <= 3);
    REQUIRE(std::abs(starts[2] - 140) <= 3);
    REQUIRE(std::abs(starts[3] - 260) <= 3);

    // Too short to split: one segment.
    REQUIRE(segmentConstantMean({1.0f, 2.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, 1.0, 2).size() == 1);
}
