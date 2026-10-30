#include <animalaudio/dsp/Oversampler.h>
#include <animalaudio/dsp/WindowedSinc.h>
#include <animalaudio/testing/SignalProbe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using animalaudio::dsp::Oversampler4x;
using animalaudio::testing::toneAmplitude;

namespace
{
std::vector<float> roundTrip(const std::vector<float>& input, std::vector<float>* oversampled = nullptr)
{
    Oversampler4x os;
    std::vector<float> out(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        float block[Oversampler4x::kFactor];
        os.upsample(input[i], block);
        if (oversampled != nullptr)
            oversampled->insert(oversampled->end(), block, block + Oversampler4x::kFactor);
        out[i] = os.downsample(block);
    }
    return out;
}
} // namespace

TEST_CASE("Oversampler4x round trip is a pure delay of kLatency samples")
{
    std::vector<float> impulse(256, 0.0f);
    impulse[10] = 1.0f;
    const auto out = roundTrip(impulse);
    std::size_t peak = 0;
    for (std::size_t i = 1; i < out.size(); ++i)
        if (std::abs(out[i]) > std::abs(out[peak]))
            peak = i;
    REQUIRE(peak == 10 + Oversampler4x::kLatency);
    for (int k = 1; k < 20; ++k)
        REQUIRE(out[peak - static_cast<std::size_t>(k)] ==
                Catch::Approx(out[peak + static_cast<std::size_t>(k)]).margin(1e-6));
}

TEST_CASE("Oversampler4x passband is flat")
{
    constexpr double rate = 48000.0;
    for (const double hz : {100.0, 1000.0, 10000.0, 18000.0})
    {
        const auto input = animalaudio::testing::sineWave(hz, rate, 16384, 0.5f);
        const auto out = roundTrip(input);
        const double gain = toneAmplitude(out, 4096, 8192, hz, rate) / 0.5;
        INFO(hz << " Hz gain " << gain);
        REQUIRE(std::abs(20.0 * std::log10(gain)) < (hz <= 10000.0 ? 0.01 : 0.1));
    }
}

TEST_CASE("Oversampler4x rejects images")
{
    constexpr double rate = 48000.0;
    const auto input = animalaudio::testing::sineWave(15000.0, rate, 8192, 0.5f);
    std::vector<float> oversampled;
    roundTrip(input, &oversampled);
    const double wanted = toneAmplitude(oversampled, 8192, 16384, 15000.0, rate * 4);
    const double image = toneAmplitude(oversampled, 8192, 16384, rate - 15000.0, rate * 4);
    REQUIRE(wanted == Catch::Approx(0.5).epsilon(0.01));
    REQUIRE(image / wanted < 1.0e-4);
}

TEST_CASE("SincReader interpolates a sine between samples")
{
    animalaudio::dsp::SincReader reader;
    constexpr int size = 1024;
    std::vector<float> ring(size);
    const double w = 2.0 * animalaudio::testing::kPi * 0.123;
    for (int i = 0; i < size; ++i)
        ring[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(w * i));
    animalaudio::dsp::SincReader::Weights weights;
    for (double position = 300.0; position < 340.0; position += 0.37)
    {
        reader.prepareRead(position, 1.0f, weights);
        const float value = animalaudio::dsp::SincReader::apply(weights, ring.data(), size - 1);
        REQUIRE(value == Catch::Approx(std::sin(w * position)).margin(2e-3));
    }
}
