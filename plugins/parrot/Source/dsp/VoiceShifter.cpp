#include "VoiceShifter.h"

#include <algorithm>
#include <cmath>

namespace parrot::dsp
{
namespace
{
std::size_t nextPowerOfTwo(std::int64_t n)
{
    std::size_t size = 1;
    while (static_cast<std::int64_t>(size) < n)
        size <<= 1;
    return size;
}
} // namespace

void VoiceShifter::prepare(double sampleRate)
{
    rate = sampleRate;
    tracker.prepare(sampleRate, kMinHz, kMaxHz);
    maxPeriod = tracker.getMaxPeriod();
    unvoicedSpacing = std::max(16, static_cast<int>(std::lround(0.005 * sampleRate)));
    markLag = std::max(tracker.getCentreLag() + tracker.getHop() - maxPeriod / 2, maxPeriod / 2 + 4);

    // Every grain is launched 1.25 of the longest periods / formant ratio ahead of its centre, so whatever follows
    // (any period, or unvoiced) can still fade in on time. By then the nearest analysis mark (up to half a period
    // later), its successor (up to 1.25 periods after that) and the alignment slack of a pending mark (1/8 period)
    // must already be placed, markLag behind the input.
    launchLead = std::max(1.25f * static_cast<float>(maxPeriod), static_cast<float>(unvoicedSpacing));
    const double periods = 1.25 / kMinFormantRatio + 0.5 + 1.25 + 0.125;
    latency = markLag + static_cast<int>(std::ceil(maxPeriod * periods)) +
              animalaudio::dsp::SincReader::reachFor(kMaxFormantRatio) + 8;

    const auto size = nextPowerOfTwo(latency + 4 * maxPeriod + 1024);
    mask = static_cast<std::int64_t>(size) - 1;
    for (auto& channel : history)
        channel.assign(size, 0.0f);
    mono.assign(size, 0.0f);
    reset();
}

void VoiceShifter::reset() noexcept
{
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);
    std::fill(mono.begin(), mono.end(), 0.0f);
    tracker.reset();
    written = 0;
    estimateCount = 0;
    markCount = 0;
    nextMarkPosition = 0;
    cursor = 0;
    for (auto& grain : grains)
        grain.active = false;
    nextSynthesis = 0.0;
    lastVoicedSynthesis = -1.0e12;
    lastGrainVoiced = false;
    stats = {};
    inputHz.store(0.0f, std::memory_order_relaxed);
    outputHz.store(0.0f, std::memory_order_relaxed);
}

void VoiceShifter::setFormantRatio(float ratio) noexcept
{
    formantRatio = std::clamp(ratio, kMinFormantRatio, kMaxFormantRatio);
}

const VoiceShifter::Estimate& VoiceShifter::estimateNear(std::int64_t position) const noexcept
{
    if (estimateCount == 0)
        return silentEstimate;
    const std::int64_t oldest = std::max<std::int64_t>(0, estimateCount - kEstimateSlots);
    std::int64_t best = estimateCount - 1;
    auto distance = [&](std::int64_t index)
    { return std::abs(estimates[static_cast<std::size_t>(index % kEstimateSlots)].centre - position); };
    std::int64_t bestDistance = distance(best);
    for (std::int64_t i = best - 1; i >= oldest; --i)
    {
        const std::int64_t d = distance(i);
        if (d >= bestDistance)
            break;
        best = i;
        bestDistance = d;
    }
    return estimates[static_cast<std::size_t>(best % kEstimateSlots)];
}

void VoiceShifter::placeMarks() noexcept
{
    const auto signal = [this](std::int64_t index) { return monoAt(index); };
    const auto front = static_cast<double>(written - 1 - markLag);
    const float shortest = static_cast<float>(rate / kMaxHz) * 0.9f;
    const float longest = static_cast<float>(maxPeriod) * 1.05f;
    while (nextMarkPosition <= front)
    {
        const double guess = nextMarkPosition;
        const Estimate& estimate = estimateNear(std::llround(guess));
        Mark mark;
        mark.voiced = estimate.voiced && estimate.period >= shortest && estimate.period <= longest;
        mark.period = mark.voiced ? estimate.period : static_cast<float>(unvoicedSpacing);
        mark.position = guess;
        if (mark.voiced)
        {
            const Mark* previous = markCount > 0 ? &markAt(markCount - 1) : nullptr;
            const bool continuous = previous != nullptr && previous->voiced &&
                                    std::abs(previous->period - mark.period) < 0.2f * mark.period &&
                                    guess - previous->position < 1.5 * static_cast<double>(mark.period);
            if (continuous)
                mark.position =
                    animalaudio::dsp::psola::alignToPrevious(signal, previous->position, guess, mark.period);
            else
                mark.position = animalaudio::dsp::psola::strongestNear(
                    signal, guess, mark.period, previous != nullptr ? previous->position + 1.0 : guess);
        }
        marks[static_cast<std::size_t>(markCount % kMarkSlots)] = mark;
        ++markCount;
        nextMarkPosition =
            mark.position + std::max(1.0, mark.voiced ? static_cast<double>(mark.period) : unvoicedSpacing);
    }
}

float VoiceShifter::meanSpacing(std::int64_t index, float period) const noexcept
{
    const std::int64_t oldest = std::max<std::int64_t>(0, markCount - kMarkSlots);
    std::int64_t last = index;
    if (last + 1 < markCount && markAt(last + 1).voiced)
        ++last;
    std::int64_t first = index;
    while (first > oldest && last - first < 4 && markAt(first - 1).voiced)
        --first;
    if (last == first)
        return period;
    const float mean =
        static_cast<float>(markAt(last).position - markAt(first).position) / static_cast<float>(last - first);
    return std::clamp(mean, 0.75f * period, 1.25f * period);
}

void VoiceShifter::startGrain(const Grain& grain) noexcept
{
    for (auto& slot : grains)
    {
        if (!slot.active)
        {
            slot = grain;
            slot.active = true;
            ++stats.grains;
            return;
        }
    }
    ++stats.droppedGrains;
}

void VoiceShifter::launchGrains(std::int64_t now, PitchMapper& mapper) noexcept
{
    const auto reach = static_cast<float>(animalaudio::dsp::SincReader::reachFor(formantRatio));
    while (true)
    {
        const double ts = nextSynthesis;
        const std::int64_t oldestMark = std::max<std::int64_t>(0, markCount - kMarkSlots);
        cursor = std::max(cursor, oldestMark);
        while (cursor + 1 < markCount && markAt(cursor + 1).position <= ts)
            ++cursor;

        std::int64_t chosen = -1;
        if (markCount > 0)
        {
            chosen = cursor;
            if (cursor + 1 < markCount && markAt(cursor + 1).position - ts < ts - markAt(cursor).position)
                chosen = cursor + 1;
        }
        const bool voiced = chosen >= 0 && markAt(chosen).voiced;
        if (voiced && !lastGrainVoiced)
        {
            // Entering a voiced stretch: start on an analysis mark, so 1x pitch reproduces the input in place.
            if (markAt(chosen).position < ts && chosen + 1 < markCount && markAt(chosen + 1).voiced)
                ++chosen;
            if (markAt(chosen).position >= ts)
            {
                nextSynthesis = markAt(chosen).position;
                lastGrainVoiced = true;
                continue;
            }
        }
        const float beta = formantRatio;

        Grain grain;
        grain.beta = beta;
        grain.centre = ts;
        float left = static_cast<float>(unvoicedSpacing);
        float right = left;
        double step = 0.0;
        if (voiced)
        {
            const Mark& mark = markAt(chosen);
            const float period = mark.period;
            if (chosen > oldestMark && markAt(chosen - 1).voiced)
                left = std::clamp(static_cast<float>(mark.position - markAt(chosen - 1).position), 0.75f * period,
                                  1.25f * period);
            else
                left = period;
            if (chosen + 1 < markCount && markAt(chosen + 1).voiced)
                right = std::clamp(static_cast<float>(markAt(chosen + 1).position - mark.position), 0.75f * period,
                                   1.25f * period);
            else
                right = period;

            if (ts - launchLead / beta > static_cast<double>(now))
                break;

            if (chosen + 1 >= markCount && nextMarkPosition - 0.125 * period < ts + 0.5 * period)
                ++stats.blindGrains; // a nearer mark may still be placed: the latency budget was too small

            const float inputPitch = static_cast<float>(rate) / period;
            const float alpha =
                std::clamp(mapper.pitchRatio(inputPitch, static_cast<float>((ts - lastVoicedSynthesis) / rate)),
                           kMinPitchRatio, kMaxPitchRatio);
            grain.source = mark.position;
            grain.gain = std::sqrt(beta / alpha);
            // At 1x the exact spacing keeps synthesis on the analysis marks (the input comes back unchanged);
            // otherwise the mean spacing of the neighbouring periods avoids an integer-rounding pitch bias.
            step = (std::abs(alpha - 1.0f) < 1.0e-6f ? right : meanSpacing(chosen, period)) / alpha;
            lastVoicedSynthesis = ts;
            lastGrainVoiced = true;
            inputHz.store(inputPitch, std::memory_order_relaxed);
            outputHz.store(inputPitch * alpha, std::memory_order_relaxed);
            ++stats.voicedGrains;
        }
        else
        {
            left = right = static_cast<float>(unvoicedSpacing) / beta;
            if (ts - launchLead / beta > static_cast<double>(now))
                break;
            grain.source = ts;
            grain.gain = 1.0f;
            step = left;
            lastGrainVoiced = false;
            left *= beta;
            right *= beta;
            inputHz.store(0.0f, std::memory_order_relaxed);
            outputHz.store(0.0f, std::memory_order_relaxed);
        }

        if (ts - left / beta < static_cast<double>(now) - 1.0)
        {
            // A sudden jump to a much longer period: squeeze the fade-in so it still starts from silence.
            ++stats.lateGrains;
            left = std::max(1.0f, static_cast<float>(ts - static_cast<double>(now)) * beta);
        }
        grain.invLeft = beta / left;
        grain.invRight = beta / right;

        const double ahead =
            (grain.source - ts) + (beta >= 1.0f ? (beta - 1.0f) * right / beta : (1.0f - beta) * left / beta) + reach;
        if (ahead > static_cast<double>(latency))
            ++stats.futureReads;

        startGrain(grain);
        nextSynthesis = ts + step;
    }
}

void VoiceShifter::process(float* const* channels, int numChannels, int numSamples, PitchMapper& mapper) noexcept
{
    const int count = std::min(numChannels, kMaxChannels);
    if (count <= 0)
        return;
    const float monoScale = 1.0f / static_cast<float>(count);
    animalaudio::dsp::SincReader::Weights weights;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto slot = static_cast<std::size_t>(written & mask);
        float sum = 0.0f;
        for (int ch = 0; ch < count; ++ch)
        {
            const float x = channels[ch][i];
            history[static_cast<std::size_t>(ch)][slot] = x;
            sum += x;
        }
        const float m = sum * monoScale;
        mono[slot] = m;
        if (tracker.push(m))
        {
            const auto& latest = tracker.latest();
            estimates[static_cast<std::size_t>(estimateCount % kEstimateSlots)] = {latest.centre, latest.period,
                                                                                   latest.voiced};
            ++estimateCount;
        }
        ++written;

        placeMarks();
        const std::int64_t now = written - 1 - latency;
        launchGrains(now, mapper);

        float out[kMaxChannels] = {};
        for (auto& grain : grains)
        {
            if (!grain.active)
                continue;
            const double dt = static_cast<double>(now) - grain.centre;
            const float u = animalaudio::dsp::psola::HannTable::position(grain, dt);
            if (u >= 1.0f)
            {
                grain.active = false;
                continue;
            }
            if (u <= -1.0f)
                continue;
            const float w = hann.value(u) * grain.gain;
            reader.prepareRead(grain.source + grain.beta * dt, grain.beta, weights);
            for (int ch = 0; ch < count; ++ch)
                out[ch] += w * animalaudio::dsp::SincReader::apply(weights,
                                                                   history[static_cast<std::size_t>(ch)].data(), mask);
        }
        for (int ch = 0; ch < count; ++ch)
            channels[ch][i] = out[ch];
    }
}
} // namespace parrot::dsp
