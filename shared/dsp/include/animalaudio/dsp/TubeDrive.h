#pragma once

#include "Oversampler.h"
#include "Smoothing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace animalaudio::dsp
{
/**
 * Koren's triode plate-current model (N. Koren, "Improved VT models for SPICE simulations", 1996).
 * Defaults are the 12AX7 constants Cohen & Hélie fitted to their own plate-curve measurements
 * ("Real-time simulation of a guitar power amplifier", AES 129th Convention, 2010, table 2, tube 1).
 */
struct KorenTriode
{
    double mu = 106.0;
    double ex = 1.46;
    double kg1 = 1572.0;
    double kp = 464.0;
    double vct = 0.49;
    double kvb = 179.0;

    /** Plate current (A) for grid-cathode and plate-cathode voltages (V). */
    double plateCurrent(double vgk, double vpk) const noexcept
    {
        if (vpk <= 0.0)
            return 0.0;
        const double inner = kp * (1.0 / mu + (vgk + vct) / std::sqrt(kvb + vpk * vpk));
        const double softplus = inner > 40.0 ? inner : std::log1p(std::exp(inner));
        const double e1 = vpk / kp * softplus;
        return e1 > 0.0 ? 2.0 * std::pow(e1, ex) / kg1 : 0.0;
    }
};

/** Grid-cathode conduction (Cohen & Hélie, eqs. 2-3): off below Vγ - Kn, resistive above Vγ + Kn, quadratic knee. */
struct GridDiode
{
    double vGamma = 0.35;
    double rgk = 1300.0;
    double kn = 0.5;

    double current(double vgk) const noexcept
    {
        if (vgk < vGamma - kn)
            return 0.0;
        if (vgk > vGamma + kn)
            return (vgk - vGamma) / rgk;
        const double a = 1.0 / (4.0 * kn * rgk);
        const double b = (kn - vGamma) / (2.0 * kn * rgk);
        const double c = -a * (vGamma - kn) * (vGamma - kn) - b * (vGamma - kn);
        return a * vgk * vgk + b * vgk + c;
    }
};

/** Common-cathode 12AX7 stage: 100k plate load, 1.5k / 22 uF bypassed cathode, 68k grid stopper. */
struct TriodeCircuit
{
    double supply = 250.0;
    double plateR = 100.0e3;
    double cathodeR = 1.5e3;
    double cathodeC = 22.0e-6;
    double gridR = 68.0e3;
};

/**
 * One stage solved for the audio thread. Two monotonic implicit equations are solved by bisection at
 * construction and tabulated: the grid node (source -> grid stopper -> conducting grid) and the plate load line.
 * The cathode is left to the caller as a state (it moves slowly through the bypass capacitor).
 */
class TriodeStageModel
{
public:
    explicit TriodeStageModel(const KorenTriode& tube = {}, const GridDiode& grid = {},
                              const TriodeCircuit& values = {})
        : diode(grid)
        , circuitValues(values)
    {
        const double rk = values.cathodeR;
        const double rp = values.plateR;
        ip0 = bisect(0.0, values.supply / (rp + rk),
                     [&](double ip) { return ip - tube.plateCurrent(-rk * ip, values.supply - (rp + rk) * ip); });
        vk0 = rk * ip0;

        gridLow = static_cast<float>(grid.vGamma - grid.kn);
        gridTable.resize(kGridSize + 1);
        for (int i = 0; i <= kGridSize; ++i)
        {
            const double u = gridLow + static_cast<double>(kGridSpan) * i / kGridSize;
            gridTable[static_cast<std::size_t>(i)] = static_cast<float>(
                bisect(gridLow, u, [&](double v) { return v + values.gridR * grid.current(v) - u; }));
        }

        const double supplyAboveCathode = values.supply - vk0;
        plateTable.resize(kPlateSize + 1);
        for (int i = 0; i <= kPlateSize; ++i)
        {
            const double vgk = kPlateLow + static_cast<double>(kPlateHigh - kPlateLow) * i / kPlateSize;
            plateTable[static_cast<std::size_t>(i)] = static_cast<float>(
                bisect(0.0, supplyAboveCathode / rp,
                       [&](double ip) { return ip - tube.plateCurrent(vgk, supplyAboveCathode - rp * ip); }));
        }
        ip0 = plateCurrent(static_cast<float>(-vk0));
    }

    double quiescentCathode() const noexcept { return vk0; }
    double quiescentPlateCurrent() const noexcept { return ip0; }
    double quiescentPlate() const noexcept { return circuitValues.supply - circuitValues.plateR * ip0; }
    const TriodeCircuit& circuit() const noexcept { return circuitValues; }
    const GridDiode& grid() const noexcept { return diode; }

    /** Grid-cathode voltage for `u` = source minus cathode voltage (grid current drops the rest on the stopper). */
    float gridVoltage(float u) const noexcept
    {
        if (u <= gridLow)
            return u;
        return lookup(gridTable, (u - gridLow) * (kGridSize / kGridSpan), kGridSize);
    }

    /** Plate current (A) on the load line for a grid-cathode voltage. */
    float plateCurrent(float vgk) const noexcept
    {
        return lookup(plateTable, (vgk - kPlateLow) * (kPlateSize / (kPlateHigh - kPlateLow)), kPlateSize);
    }

    /** AC plate voltage (V, inverting) for a source voltage with the cathode at rest. */
    float staticTransfer(float source) const noexcept
    {
        const float vgk = gridVoltage(source - static_cast<float>(vk0));
        return -static_cast<float>(circuitValues.plateR) * (plateCurrent(vgk) - static_cast<float>(ip0));
    }

    /** Source voltage where the grid starts to conduct, with the cathode at rest. */
    double gridConductionOnset() const noexcept { return vk0 + gridLow; }

private:
    static constexpr int kGridSize = 4096;
    static constexpr float kGridSpan = 400.0f;
    static constexpr int kPlateSize = 8192;
    static constexpr float kPlateLow = -40.0f;
    static constexpr float kPlateHigh = 12.0f;

    template <class F>
    static double bisect(double low, double high, F&& f)
    {
        for (int i = 0; i < 60; ++i)
        {
            const double mid = 0.5 * (low + high);
            if (f(mid) > 0.0)
                high = mid;
            else
                low = mid;
        }
        return 0.5 * (low + high);
    }

    static float lookup(const std::vector<float>& table, float index, int size) noexcept
    {
        if (index <= 0.0f)
            return table.front();
        if (index >= static_cast<float>(size))
            return table.back();
        const int i = static_cast<int>(index);
        const float t = index - static_cast<float>(i);
        return table[static_cast<std::size_t>(i)] +
               t * (table[static_cast<std::size_t>(i + 1)] - table[static_cast<std::size_t>(i)]);
    }

    GridDiode diode;
    TriodeCircuit circuitValues;
    double ip0 = 0.0;
    double vk0 = 0.0;
    float gridLow = 0.0f;
    std::vector<float> gridTable;
    std::vector<float> plateTable;
};

/**
 * Two cascaded 12AX7 common-cathode stages, 4x oversampled. Each stage simulates its cathode bypass capacitor
 * (so the bias shifts with level: asymmetric clipping moves the average current) and grid current through the
 * stopper (soft clipping on positive swings, cutoff on negative ones: the even harmonics). A DC-blocking coupling
 * network and a fixed divider sit between the stages. Output is level-matched for a -12 dBFS sine and crossfaded
 * from the clean (latency-matched) signal near drive 0, so drive 0 is exactly clean.
 */
class TubeDrive
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kLatency = Oversampler4x::kLatency;
    static constexpr double kInterstageGain = 1.0 / 40.0;

    TubeDrive()
    {
        const auto& stage = model();
        for (int i = 0; i <= kTableSize; ++i)
        {
            const float amount = static_cast<float>(i) / kTableSize;
            inputGain[static_cast<std::size_t>(i)] = static_cast<float>(inputVolts(amount));
            makeup[static_cast<std::size_t>(i)] = static_cast<float>(1.0 / staticLevel(stage, inputVolts(amount)));
        }
    }

    /** Source volts per digital full scale at the first grid. */
    static double inputVolts(float drive) noexcept
    {
        return 0.3 * std::pow(10.0, 2.5 * static_cast<double>(std::clamp(drive, 0.0f, 1.0f)));
    }

    static const TriodeStageModel& model()
    {
        static const TriodeStageModel instance;
        return instance;
    }

    /** RMS out / RMS in of the static two-stage curve for a -12 dBFS sine (DC removed between stages). */
    static double staticLevel(const TriodeStageModel& stage, double volts)
    {
        constexpr int n = 1024;
        constexpr double amplitude = 0.25;
        std::vector<double> first(n);
        double mean = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x = amplitude * std::sin(2.0 * 3.14159265358979323846 * i / n);
            first[static_cast<std::size_t>(i)] = stage.staticTransfer(static_cast<float>(volts * x));
            mean += first[static_cast<std::size_t>(i)] / n;
        }
        std::vector<double> second(n);
        double secondMean = 0.0;
        for (int i = 0; i < n; ++i)
        {
            second[static_cast<std::size_t>(i)] =
                stage.staticTransfer(static_cast<float>(kInterstageGain * (first[static_cast<std::size_t>(i)] - mean)));
            secondMean += second[static_cast<std::size_t>(i)] / n;
        }
        double power = 0.0;
        for (const double v : second)
            power += (v - secondMean) * (v - secondMean) / n;
        return std::sqrt(power) / (amplitude / std::sqrt(2.0));
    }

    void prepare(double sampleRate)
    {
        const double osRate = sampleRate * Oversampler4x::kFactor;
        const auto& values = model().circuit();
        cathodeStep = static_cast<float>(1.0 / (osRate * values.cathodeC));
        couplingCoeff = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * 7.2 / osRate));
        dcCoeff = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * 10.0 / sampleRate));
        driveSmoother.prepare(sampleRate, 25.0);
        wetSmoother.prepare(sampleRate, 15.0);
        heatDecay = static_cast<float>(std::exp(-1.0 / (0.15 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        driveSmoother.reset(drive);
        wetSmoother.reset(drive > kCleanBelow ? 1.0f : 0.0f);
        for (auto& c : channels)
            resetChannel(c);
        heatEnvelope = 0.0f;
        heat.store(0.0f, std::memory_order_relaxed);
    }

    void setDrive(float amount) noexcept
    {
        drive = std::clamp(amount, 0.0f, 1.0f);
        driveSmoother.setTarget(drive);
        wetSmoother.setTarget(drive > kCleanBelow ? 1.0f : 0.0f);
    }

    /** 0..1: how far past grid conduction the first stage is driven (for meters / glow). */
    float getHeat() const noexcept { return heat.load(std::memory_order_relaxed); }

    void process(float* const* audio, int numChannels, int numSamples) noexcept
    {
        const int count = std::min(numChannels, kMaxChannels);
        const bool idle = wetSmoother.getCurrent() == 0.0f && wetSmoother.getTarget() == 0.0f;
        if (idle)
        {
            for (int ch = 0; ch < count; ++ch)
            {
                auto& c = channels[static_cast<std::size_t>(ch)];
                if (!c.stale)
                    resetChannel(c);
                for (int i = 0; i < numSamples; ++i)
                    audio[ch][i] = delayClean(c, audio[ch][i]);
            }
            for (int i = 0; i < numSamples; ++i)
                driveSmoother.next();
            heatEnvelope *= std::pow(heatDecay, static_cast<float>(numSamples));
            heat.store(0.0f, std::memory_order_relaxed);
            return;
        }

        const auto& stage = model();
        const float onset = static_cast<float>(stage.gridConductionOnset());
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float d = driveSmoother.next() * kTableSize;
            const int index = std::min(static_cast<int>(d), kTableSize - 1);
            const float t = d - static_cast<float>(index);
            const auto at = [&](const std::array<float, kTableSize + 1>& table)
            {
                return table[static_cast<std::size_t>(index)] +
                       t * (table[static_cast<std::size_t>(index + 1)] - table[static_cast<std::size_t>(index)]);
            };
            const float volts = at(inputGain);
            const float gain = at(makeup);
            const float wet = wetSmoother.next();

            for (int ch = 0; ch < count; ++ch)
            {
                auto& c = channels[static_cast<std::size_t>(ch)];
                c.stale = false;
                const float x = audio[ch][i];
                const float clean = delayClean(c, x);
                float os[Oversampler4x::kFactor];
                c.oversampler.upsample(x * volts, os);
                for (float& v : os)
                {
                    peak = std::max(peak, std::abs(v));
                    const float first = runStage(stage, c.cathode[0], v);
                    const float coupled = couplingCoeff * (c.couplingOut + first - c.couplingIn);
                    c.couplingIn = first;
                    c.couplingOut = coupled;
                    v = runStage(stage, c.cathode[1], static_cast<float>(kInterstageGain) * coupled);
                }
                const float y = c.oversampler.downsample(os);
                const float blocked = dcCoeff * (c.dcOut + y - c.dcIn);
                c.dcIn = y;
                c.dcOut = blocked;
                audio[ch][i] = clean + wet * (blocked * gain - clean);
            }
        }
        heatEnvelope = std::max(peak, heatEnvelope * std::pow(heatDecay, static_cast<float>(numSamples)));
        heat.store(std::clamp(std::log2(1.0f + heatEnvelope / onset) * 0.5f, 0.0f, 1.0f) * wetSmoother.getCurrent(),
                   std::memory_order_relaxed);
    }

private:
    static constexpr int kTableSize = 128;
    static constexpr float kCleanBelow = 0.0005f;

    struct Channel
    {
        Oversampler4x oversampler;
        std::array<float, 2> cathode{};
        float couplingIn = 0.0f;
        float couplingOut = 0.0f;
        float dcIn = 0.0f;
        float dcOut = 0.0f;
        std::array<float, kLatency> cleanDelay{};
        int cleanPos = 0;
        bool stale = true;
    };

    void resetChannel(Channel& c) noexcept
    {
        c.oversampler.reset();
        c.cathode.fill(static_cast<float>(model().quiescentCathode()));
        c.couplingIn = c.couplingOut = c.dcIn = c.dcOut = 0.0f;
        c.stale = true;
    }

    static float delayClean(Channel& c, float x) noexcept
    {
        const float out = c.cleanDelay[static_cast<std::size_t>(c.cleanPos)];
        c.cleanDelay[static_cast<std::size_t>(c.cleanPos)] = x;
        c.cleanPos = c.cleanPos + 1 == kLatency ? 0 : c.cleanPos + 1;
        return out;
    }

    float runStage(const TriodeStageModel& stage, float& cathode, float source) const noexcept
    {
        const auto& values = stage.circuit();
        const float u = source - cathode;
        const float vgk = stage.gridVoltage(u);
        const float gridCurrent = (u - vgk) / static_cast<float>(values.gridR);
        const float plateCurrent = stage.plateCurrent(vgk);
        cathode += cathodeStep * (plateCurrent + gridCurrent - cathode / static_cast<float>(values.cathodeR));
        cathode = std::clamp(cathode, 0.0f, 50.0f);
        return -static_cast<float>(values.plateR) * (plateCurrent - static_cast<float>(stage.quiescentPlateCurrent()));
    }

    std::array<float, kTableSize + 1> inputGain{};
    std::array<float, kTableSize + 1> makeup{};
    std::array<Channel, kMaxChannels> channels;
    OnePoleSmoother driveSmoother;
    OnePoleSmoother wetSmoother;
    float drive = 0.0f;
    float cathodeStep = 0.0f;
    float couplingCoeff = 0.0f;
    float dcCoeff = 0.0f;
    float heatDecay = 0.0f;
    float heatEnvelope = 0.0f;
    std::atomic<float> heat{0.0f};
};
} // namespace animalaudio::dsp
