#include <animalaudio/dsp/TubeDrive.h>
#include <animalaudio/testing/SignalProbe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using animalaudio::dsp::KorenTriode;
using animalaudio::dsp::TubeDrive;
using animalaudio::testing::rms;
using animalaudio::testing::toneAmplitude;

namespace
{
std::vector<float> drive(float amount, const std::vector<float>& input, double rate, int blockSize = 512)
{
    TubeDrive tube;
    tube.prepare(rate);
    tube.setDrive(amount);
    tube.reset();
    std::vector<float> out = input;
    for (std::size_t done = 0; done < out.size(); done += static_cast<std::size_t>(blockSize))
    {
        float* channel[] = {out.data() + done};
        tube.process(channel, 1, static_cast<int>(std::min<std::size_t>(blockSize, out.size() - done)));
    }
    return out;
}

double harmonic(const std::vector<float>& x, double hz, int k, double rate)
{
    return toneAmplitude(x, x.size() / 2, x.size() / 2, hz * k, rate);
}

double thd(const std::vector<float>& x, double hz, double rate)
{
    double sum = 0.0;
    for (int k = 2; k * hz < 0.45 * rate; ++k)
        sum += std::pow(harmonic(x, hz, k, rate), 2.0);
    return std::sqrt(sum) / harmonic(x, hz, 1, rate);
}
} // namespace

TEST_CASE("Koren 12AX7 matches the datasheet operating point")
{
    const KorenTriode tube;
    // RCA 12AX7: Vp 250 V, Vg -2 V -> Ip 1.2 mA, gm 1.6 mA/V, mu 100.
    const double ip = tube.plateCurrent(-2.0, 250.0);
    REQUIRE(ip == Catch::Approx(1.2e-3).epsilon(0.15));
    const double gm = (tube.plateCurrent(-1.99, 250.0) - tube.plateCurrent(-2.01, 250.0)) / 0.02;
    const double rp = 2.0 / (tube.plateCurrent(-2.0, 251.0) - tube.plateCurrent(-2.0, 249.0));
    REQUIRE(gm == Catch::Approx(1.6e-3).epsilon(0.25));
    REQUIRE(gm * rp == Catch::Approx(100.0).epsilon(0.15));
    REQUIRE(tube.plateCurrent(-10.0, 250.0) < 1.0e-6); // cut off
}

TEST_CASE("Grid diode is continuous through its knee")
{
    const animalaudio::dsp::GridDiode diode;
    REQUIRE(diode.current(-1.0) == 0.0);
    const double low = diode.vGamma - diode.kn;
    const double high = diode.vGamma + diode.kn;
    REQUIRE(diode.current(low + 1e-9) == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(diode.current(high - 1e-9) == Catch::Approx(diode.current(high + 1e-9)).margin(1e-9));
    REQUIRE(diode.current(2.0) == Catch::Approx((2.0 - diode.vGamma) / diode.rgk));
}

TEST_CASE("Triode stage sits at a classic operating point with ~60x gain")
{
    const auto& stage = TubeDrive::model();
    REQUIRE(stage.quiescentCathode() > 0.8);
    REQUIRE(stage.quiescentCathode() < 2.0);
    REQUIRE(stage.quiescentPlate() > 120.0);
    REQUIRE(stage.quiescentPlate() < 220.0);
    const double gain = (stage.staticTransfer(0.001f) - stage.staticTransfer(-0.001f)) / 0.002;
    REQUIRE(gain < -45.0);
    REQUIRE(gain > -75.0);
    // Asymmetric: positive swings hit grid conduction long before negative swings reach cutoff.
    REQUIRE(std::abs(stage.staticTransfer(4.0f)) < std::abs(stage.staticTransfer(-1.0f)) * 4.0f);
}

TEST_CASE("TubeDrive at zero is a clean delay")
{
    constexpr double rate = 48000.0;
    const auto input = animalaudio::testing::whiteNoise(4096, 0.5f, 3);
    const auto out = drive(0.0f, input, rate);
    for (std::size_t i = TubeDrive::kLatency; i < input.size(); ++i)
        REQUIRE(out[i] == input[i - TubeDrive::kLatency]);
}

TEST_CASE("TubeDrive adds harmonics with drive, even ones included")
{
    constexpr double rate = 48000.0;
    constexpr double hz = 1000.0;
    const auto input = animalaudio::testing::sineWave(hz, rate, 32768, 0.25f);
    const double light = thd(drive(0.25f, input, rate), hz, rate);
    const auto mediumOut = drive(0.5f, input, rate);
    const double medium = thd(mediumOut, hz, rate);
    const double heavy = thd(drive(1.0f, input, rate), hz, rate);
    INFO("THD light " << light << " medium " << medium << " heavy " << heavy);
    REQUIRE(light < 0.02);
    REQUIRE(light < medium);
    REQUIRE(medium < heavy);
    REQUIRE(heavy > 0.1);
    REQUIRE(harmonic(mediumOut, hz, 2, rate) / harmonic(mediumOut, hz, 1, rate) > 0.003);
}

TEST_CASE("TubeDrive keeps the level of a -12 dBFS sine")
{
    constexpr double rate = 48000.0;
    const auto input = animalaudio::testing::sineWave(440.0, rate, 48000, 0.25f);
    for (const float amount : {0.1f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        const auto out = drive(amount, input, rate);
        const double db = 20.0 * std::log10(rms(out, 24000, 48000) / rms(input, 24000, 48000));
        INFO("drive " << amount << " level " << db << " dB");
        REQUIRE(std::abs(db) < 3.0);
    }
}

TEST_CASE("TubeDrive aliasing stays far below the signal")
{
    constexpr double rate = 44100.0;
    constexpr double hz = 7000.0;
    const auto out = drive(1.0f, animalaudio::testing::sineWave(hz, rate, 32768, 0.5f), rate);
    const double fundamental = harmonic(out, hz, 1, rate);
    for (const double alias : {rate - 4 * hz, rate - 5 * hz, rate - 6 * hz})
    {
        const double level = toneAmplitude(out, out.size() / 2, out.size() / 2, alias, rate) / fundamental;
        INFO("alias at " << alias << " Hz: " << 20.0 * std::log10(level) << " dB");
        REQUIRE(level < 1.0e-3);
    }
}

TEST_CASE("TubeDrive is block-size independent and finite when slammed")
{
    constexpr double rate = 48000.0;
    auto input = animalaudio::testing::whiteNoise(9000, 4.0f, 11);
    for (std::size_t i = 6000; i < 7000; ++i)
        input[i] = 3.0f;
    const auto a = drive(1.0f, input, rate, 1);
    const auto b = drive(1.0f, input, rate, 512);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(std::isfinite(a[i]));
        REQUIRE(a[i] == b[i]);
    }
}

TEST_CASE("TubeDrive heat follows the drive")
{
    constexpr double rate = 48000.0;
    const auto input = animalaudio::testing::sineWave(220.0, rate, 4800, 0.25f);
    auto heatFor = [&](float amount)
    {
        TubeDrive tube;
        tube.prepare(rate);
        tube.setDrive(amount);
        tube.reset();
        auto buffer = input;
        float* channel[] = {buffer.data()};
        tube.process(channel, 1, static_cast<int>(buffer.size()));
        return tube.getHeat();
    };
    REQUIRE(heatFor(0.0f) == 0.0f);
    REQUIRE(heatFor(0.3f) < heatFor(0.8f));
}
