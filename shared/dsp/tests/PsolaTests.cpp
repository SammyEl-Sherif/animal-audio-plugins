#include <animalaudio/dsp/Notes.h>
#include <animalaudio/dsp/Psola.h>
#include <animalaudio/dsp/Yin.h>
#include <animalaudio/testing/SignalProbe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace animalaudio::dsp;
using animalaudio::testing::kPi;

TEST_CASE("Psola: Hann grains reaching to their neighbours overlap-add to one")
{
    psola::HannTable hann;
    // Marks with uneven spacing, as a voice with jitter has.
    const std::vector<double> marks{100.0, 190.5, 283.25, 371.0, 466.75, 560.0};
    std::vector<psola::Grain> grains;
    for (std::size_t i = 1; i + 1 < marks.size(); ++i)
    {
        psola::Grain g;
        g.centre = marks[i];
        g.invLeft = 1.0f / static_cast<float>(marks[i] - marks[i - 1]);
        g.invRight = 1.0f / static_cast<float>(marks[i + 1] - marks[i]);
        grains.push_back(g);
    }
    for (int t = 191; t < 466; ++t)
    {
        float sum = 0.0f;
        for (const auto& g : grains)
        {
            const float u = psola::HannTable::position(g, static_cast<double>(t) - g.centre);
            if (std::abs(u) < 1.0f)
                sum += hann.value(u);
        }
        INFO("t " << t);
        REQUIRE(sum == Catch::Approx(1.0f).margin(1.0e-3));
    }
}

TEST_CASE("Psola: marks aligned to the previous one land one period later, to a fraction of a sample")
{
    const double period = 211.37; // samples
    std::vector<float> wave(20000);
    for (std::size_t i = 0; i < wave.size(); ++i)
    {
        const double phase = 2.0 * kPi * static_cast<double>(i) / period;
        wave[i] =
            static_cast<float>(std::sin(phase) + 0.5 * std::sin(2.0 * phase + 0.4) + 0.25 * std::sin(3.0 * phase));
    }
    const auto at = [&](std::int64_t i)
    { return i >= 0 && i < static_cast<std::int64_t>(wave.size()) ? wave[static_cast<std::size_t>(i)] : 0.0f; };
    double mark = psola::strongestNear(at, 1000.0, static_cast<float>(period), 0.0);
    const double first = mark;
    for (int k = 1; k <= 60; ++k)
        mark = psola::alignToPrevious(at, mark, mark + period + 3.0, static_cast<float>(period));
    REQUIRE((mark - first) / 60.0 == Catch::Approx(period).margin(0.02));
}

TEST_CASE("Yin primitives: the normalised difference dips to ~0 at the period")
{
    const int period = 37;
    std::vector<float> x(400);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(i) / period));
    std::vector<float> d(102);
    std::vector<float> n(102);
    yin::difference(x.data(), 200, 100, d.data());
    yin::normalise(d.data(), 100, n.data());
    REQUIRE(n[0] == 1.0f);
    REQUIRE(n[period] < 0.01f);
    REQUIRE(yin::dipDepth(n.data(), period) < 0.01f);
    REQUIRE(n[period / 2] > 1.0f);
}

TEST_CASE("Notes: names and conversions")
{
    REQUIRE(noteName(60) == "C4");
    REQUIRE(noteName(69) == "A4");
    REQUIRE(noteName(61) == "C#4");
    REQUIRE(hzToMidi(440.0f) == Catch::Approx(69.0f));
    REQUIRE(midiToHz(57.0f) == Catch::Approx(220.0f));
    REQUIRE(isSharp(61));
    REQUIRE_FALSE(isSharp(64));
}
