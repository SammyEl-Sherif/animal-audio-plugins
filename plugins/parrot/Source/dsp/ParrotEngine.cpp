#include "ParrotEngine.h"

namespace parrot::dsp
{
std::string noteName(int midiNote)
{
    static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int octave = midiNote / 12 - 1;
    return std::string(names[((midiNote % 12) + 12) % 12]) + std::to_string(octave);
}

float hzToMidi(float hz) noexcept
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

void ParrotEngine::prepare(double sampleRate)
{
    rate = sampleRate;
    shifter.prepare(sampleRate);
    tube.prepare(sampleRate);
    latency = shifter.getLatencySamples() + animalaudio::dsp::TubeDrive::kLatency;

    std::size_t size = 1;
    while (size < static_cast<std::size_t>(latency + 1))
        size <<= 1;
    dryMask = static_cast<std::int64_t>(size) - 1;
    for (auto& line : dryLine)
        line.assign(size, 0.0f);

    mixSmoother.prepare(sampleRate, 15.0);
    levelCoeff = static_cast<float>(std::exp(-1.0 / (0.3 * sampleRate)));
    levelSmoother.prepare(sampleRate, 20.0);
    traceInterval = std::max(1, static_cast<int>(std::lround(sampleRate * 0.01)));
    traceIntervalSeconds = traceInterval / sampleRate;
    reset();
}

void ParrotEngine::reset() noexcept
{
    shifter.reset();
    tube.reset();
    for (auto& line : dryLine)
        std::fill(line.begin(), line.end(), 0.0f);
    dryWritten = 0;
    mixSmoother.reset(mixSmoother.getTarget());
    inputPower = 0.0f;
    outputPower = 0.0f;
    levelSmoother.reset(1.0f);
    snapTarget = 0;
    snapCorrection = 0.0f;
    robotPitch = 0.0f;
    for (int i = 0; i < kTraceSize; ++i)
    {
        traceIn[static_cast<std::size_t>(i)].store(-1.0f, std::memory_order_relaxed);
        traceOut[static_cast<std::size_t>(i)].store(-1.0f, std::memory_order_relaxed);
    }
    traceCount.store(0, std::memory_order_release);
    sinceTrace = 0;
}

void ParrotEngine::noteOn(int midiNote) noexcept
{
    noteOff(midiNote);
    if (heldCount == kMaxHeld)
    {
        std::copy(held.begin() + 1, held.end(), held.begin());
        --heldCount;
    }
    held[static_cast<std::size_t>(heldCount++)] = midiNote;
    heldNote.store(midiNote, std::memory_order_relaxed);
}

void ParrotEngine::noteOff(int midiNote) noexcept
{
    int kept = 0;
    for (int i = 0; i < heldCount; ++i)
        if (held[static_cast<std::size_t>(i)] != midiNote)
            held[static_cast<std::size_t>(kept++)] = held[static_cast<std::size_t>(i)];
    heldCount = kept;
    heldNote.store(heldCount > 0 ? held[static_cast<std::size_t>(heldCount - 1)] : -1, std::memory_order_relaxed);
}

void ParrotEngine::allNotesOff() noexcept
{
    heldCount = 0;
    heldNote.store(-1, std::memory_order_relaxed);
}

float ParrotEngine::pitchRatio(float inputHz, float elapsedSeconds) noexcept
{
    const float shift = appliedPitch();
    if (mode == Mode::Shift)
        return std::exp2(shift / 12.0f);

    const float input = hzToMidi(inputHz);
    const bool fresh = elapsedSeconds > 0.08f;
    const float follow = glideSeconds > 0.0f ? 1.0f - std::exp(-elapsedSeconds / glideSeconds) : 1.0f;

    if (mode == Mode::Snap)
    {
        const float desired = input + shift;
        if (fresh || std::abs(desired - static_cast<float>(snapTarget)) > 0.6f)
            snapTarget = static_cast<int>(std::lround(desired));
        const float correction = static_cast<float>(snapTarget) - desired;
        snapCorrection = fresh ? correction : snapCorrection + (correction - snapCorrection) * follow;
        return std::exp2((shift + snapCorrection) / 12.0f);
    }

    const int heldMidi = heldNote.load(std::memory_order_relaxed);
    const float target = static_cast<float>(heldMidi >= 0 ? heldMidi : note) + shift;
    robotPitch = fresh ? target : robotPitch + (target - robotPitch) * follow;
    return std::exp2((robotPitch - input) / 12.0f);
}

float ParrotEngine::traceAt(const std::array<std::atomic<float>, kTraceSize>& trace, int age) const noexcept
{
    const int count = traceCount.load(std::memory_order_acquire);
    if (age < 0 || age >= std::min(count, kTraceSize))
        return -1.0f;
    return trace[static_cast<std::size_t>((count - 1 - age) % kTraceSize)].load(std::memory_order_relaxed);
}

void ParrotEngine::pushTrace() noexcept
{
    const float in = shifter.getInputHz();
    const float out = shifter.getOutputHz();
    const int count = traceCount.load(std::memory_order_relaxed);
    const auto slot = static_cast<std::size_t>(count % kTraceSize);
    traceIn[slot].store(in > 0.0f ? hzToMidi(in) : -1.0f, std::memory_order_relaxed);
    traceOut[slot].store(out > 0.0f ? hzToMidi(out) : -1.0f, std::memory_order_relaxed);
    const int next = count + 1 < (1 << 30) ? count + 1 : kTraceSize;
    traceCount.store(next, std::memory_order_release);
}

void ParrotEngine::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    const int count = std::min(numChannels, kMaxChannels);
    if (count <= 0)
        return;

    shifter.setFormantRatio(std::exp2(appliedFormant() / 12.0f));

    for (int offset = 0; offset < numSamples; offset += kChunk)
    {
        const int n = std::min(kChunk, numSamples - offset);
        float* chunk[kMaxChannels] = {};
        for (int ch = 0; ch < count; ++ch)
            chunk[ch] = channels[ch] + offset;

        const int shifterLatency = shifter.getLatencySamples();
        for (int i = 0; i < n; ++i)
        {
            const std::int64_t index = dryWritten + i;
            float power = 0.0f;
            for (int ch = 0; ch < count; ++ch)
            {
                auto& line = dryLine[static_cast<std::size_t>(ch)];
                line[static_cast<std::size_t>(index & dryMask)] = chunk[ch][i];
                dryChunk[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] =
                    line[static_cast<std::size_t>((index - latency) & dryMask)];
                const float aligned = line[static_cast<std::size_t>((index - shifterLatency) & dryMask)];
                power += aligned * aligned;
            }
            referencePower[static_cast<std::size_t>(i)] = power;
        }
        dryWritten += n;

        shifter.process(chunk, count, n, *this);

        // Grains add up coherently at some ratios (an octave) and not at others; follow the input's level slowly.
        for (int i = 0; i < n; ++i)
        {
            float power = 0.0f;
            for (int ch = 0; ch < count; ++ch)
                power += chunk[ch][i] * chunk[ch][i];
            inputPower = referencePower[static_cast<std::size_t>(i)] +
                         levelCoeff * (inputPower - referencePower[static_cast<std::size_t>(i)]);
            outputPower = power + levelCoeff * (outputPower - power);
            constexpr float floor = 1.0e-7f;
            levelSmoother.setTarget(
                std::clamp(std::sqrt((inputPower + floor) / (outputPower + floor)), kMinLevelGain, kMaxLevelGain));
            const float gain = levelSmoother.next();
            for (int ch = 0; ch < count; ++ch)
                chunk[ch][i] *= gain;
        }

        tube.process(chunk, count, n);

        for (int i = 0; i < n; ++i)
        {
            const float wet = mixSmoother.next();
            for (int ch = 0; ch < count; ++ch)
            {
                const float dry = dryChunk[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
                chunk[ch][i] = dry + wet * (chunk[ch][i] - dry);
            }
            if (++sinceTrace >= traceInterval)
            {
                sinceTrace = 0;
                pushTrace();
            }
        }
    }
}
} // namespace parrot::dsp
