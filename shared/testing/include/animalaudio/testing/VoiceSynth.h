#pragma once

// JUCE-free synthetic voices for pitch / formant tests: a vowel with any pitch contour, a sung phrase of notes
// with consonants and breaths, and measurements of where a voice's formants sit. Header only.

#include "SignalProbe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace animalaudio::testing
{
/** Spectral envelope of the vowel /a/: three formant resonances (Peterson & Barney, adult male) and a floor. */
inline double vowelEnvelope(double hz)
{
    struct Formant
    {
        double hz, bandwidth, gain;
    };
    static constexpr Formant formants[] = {{730.0, 90.0, 1.0}, {1090.0, 110.0, 0.5}, {2440.0, 170.0, 0.25}};
    double a = 0.0;
    for (const auto& formant : formants)
    {
        const double x = (hz - formant.hz) / (0.5 * formant.bandwidth);
        a += formant.gain / (1.0 + x * x);
    }
    return a + 0.02;
}

/** Vowel /a/: harmonics of `pitchHz(t)` (up to 6 kHz) under vowelEnvelope, scaled to a peak of `peak`. */
inline std::vector<float> vowel(const std::function<double(double)>& pitchHz, double seconds, double rate,
                                float peak = 0.5f)
{
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
            v += vowelEnvelope(k * f0) * std::sin(k * phase);
        out[i] = static_cast<float>(v);
        largest = std::max(largest, std::abs(out[i]));
    }
    for (auto& v : out)
        v *= peak / largest;
    return out;
}

inline double midiToHz(double midi)
{
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

/** One sung note of a phrase: pitch, timing, vibrato and an optional consonant (noise burst) before it. */
struct SungNote
{
    double midi = 60.0;
    double start = 0.0;        // seconds, start of the voiced part
    double length = 0.4;       // seconds of voicing
    double vibratoCents = 0.0; // depth (+-)
    double vibratoHz = 5.5;
    double detuneCents = 0.0;       // constant offset from `midi`
    double consonant = 0.0;         // seconds of noise just before `start` (0: none)
    double glideFromPrevious = 0.0; // seconds of portamento from the previous note (legato when > 0)
    float level = 1.0f;
};

/** Pitch (MIDI) and amplitude of a sung phrase at time `t`; pitch is -1 where nothing is sung. */
struct SungSample
{
    double midi = -1.0;
    double amplitude = 0.0;
};

inline SungSample sungAt(const std::vector<SungNote>& notes, double t)
{
    for (std::size_t n = 0; n < notes.size(); ++n)
    {
        const auto& note = notes[n];
        const double local = t - note.start;
        if (local < 0.0 || local >= note.length)
            continue;
        const SungNote* previous = n > 0 ? &notes[n - 1] : nullptr;
        double midi = note.midi + note.detuneCents / 100.0 +
                      note.vibratoCents / 100.0 * std::sin(2.0 * kPi * note.vibratoHz * local);
        if (previous != nullptr && note.glideFromPrevious > 0.0 && local < note.glideFromPrevious)
        {
            const double u = local / note.glideFromPrevious;
            const double s = u * u * (3.0 - 2.0 * u);
            const double from = previous->midi + previous->detuneCents / 100.0;
            midi = from + (midi - from) * s;
        }
        const double attack = note.glideFromPrevious > 0.0 ? 1.0 : std::min(1.0, local / 0.015);
        const bool legatoNext = n + 1 < notes.size() && notes[n + 1].glideFromPrevious > 0.0;
        const double release = legatoNext ? 1.0 : std::min(1.0, (note.length - local) / 0.03);
        return {midi, note.level * std::max(0.0, std::min(attack, release))};
    }
    return {};
}

/**
 * A monophonic sung phrase: each note is the /a/ vowel with a 15 ms attack and 30 ms release (or a legato slide
 * from the previous note), consonants are band-passed noise bursts, everything else is silence. Peak ~`peak`.
 */
inline std::vector<float> sing(const std::vector<SungNote>& notes, double seconds, double rate, float peak = 0.5f,
                               std::uint32_t seed = 7)
{
    const auto count = static_cast<std::size_t>(seconds * rate);
    std::vector<double> pitch(count, -1.0);
    std::vector<double> amp(count, 0.0);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto sample = sungAt(notes, static_cast<double>(i) / rate);
        pitch[i] = sample.midi;
        amp[i] = sample.amplitude;
    }

    std::vector<float> out(count, 0.0f);
    double phase = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (pitch[i] < 0.0)
            continue;
        const double f0 = midiToHz(pitch[i]);
        phase += 2.0 * kPi * f0 / rate;
        double v = 0.0;
        for (int k = 1; k * f0 < 0.45 * rate && k * f0 < 6000.0; ++k)
            v += vowelEnvelope(k * f0) * std::sin(k * phase);
        out[i] = static_cast<float>(0.12 * amp[i] * v);
    }

    // Consonants: noise, two-pole band-pass around 4.5 kHz, with a quick swell.
    std::uint32_t state = seed;
    for (const auto& note : notes)
    {
        if (note.consonant <= 0.0)
            continue;
        const auto begin = static_cast<std::size_t>(std::max(0.0, (note.start - note.consonant) * rate));
        const auto end = std::min(count, static_cast<std::size_t>(note.start * rate));
        const double w = 2.0 * kPi * 4500.0 / rate;
        const double r = 0.93;
        double y1 = 0.0;
        double y2 = 0.0;
        for (std::size_t i = begin; i < end; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const double noise = static_cast<double>(state >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
            const double y = noise + 2.0 * r * std::cos(w) * y1 - r * r * y2;
            y2 = y1;
            y1 = y;
            const double u =
                static_cast<double>(i - begin) / static_cast<double>(std::max<std::size_t>(1, end - begin));
            out[i] += static_cast<float>(0.02 * y * std::sin(kPi * u) * note.level);
        }
    }

    float largest = 0.0f;
    for (const float v : out)
        largest = std::max(largest, std::abs(v));
    if (largest > 0.0f)
        for (auto& v : out)
            v *= peak / largest;
    return out;
}

/** Spectral envelope in dB on a quarter-semitone grid (300 Hz .. 4 kHz), from the harmonics of `f0`. */
inline std::vector<double> envelopeDb(const std::vector<float>& x, std::size_t begin, std::size_t count, double f0,
                                      double rate)
{
    std::vector<double> freqs;
    std::vector<double> levels;
    for (int k = 1; k * f0 < 6000.0; ++k)
    {
        freqs.push_back(std::log2(k * f0));
        levels.push_back(20.0 * std::log10(toneAmplitude(x, begin, count, k * f0, rate) + 1.0e-9));
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
inline double formantShift(const std::vector<double>& input, const std::vector<double>& output)
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
} // namespace animalaudio::testing
