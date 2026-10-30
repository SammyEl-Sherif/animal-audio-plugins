#pragma once

#include <animalaudio/dsp/Notes.h>
#include <animalaudio/dsp/ProbabilisticYin.h>
#include <animalaudio/testing/VoiceSynth.h>

#include <cmath>
#include <vector>

namespace monkeydyne::testing
{
// Three words: "la-lee" (C4 -> E4 legato), a pause, "go" (G4, sharp), a short gap, "a-ri-a" (A3 -> D4 -> F4).
inline const std::vector<animalaudio::testing::SungNote>& phrase()
{
    static const std::vector<animalaudio::testing::SungNote> notes{
        {60.0, 0.10, 0.45, 0.0, 5.5, 0.0, 0.06, 0.0, 1.0f},  {64.0, 0.55, 0.40, 40.0, 5.5, 0.0, 0.0, 0.06, 0.9f},
        {67.0, 1.40, 0.35, 0.0, 5.5, 20.0, 0.08, 0.0, 0.8f}, {57.0, 1.83, 0.42, 0.0, 5.5, 0.0, 0.0, 0.0, 1.0f},
        {62.0, 2.25, 0.40, 0.0, 5.5, 0.0, 0.0, 0.04, 0.9f},  {65.0, 2.65, 0.40, 30.0, 6.0, 0.0, 0.0, 0.05, 0.8f}};
    return notes;
}

inline constexpr double kPhraseSeconds = 4.2;

/** Mean sung pitch (MIDI) of x between two times, from pYIN, voiced frames only; -1 if none. */
inline double measuredMidi(const std::vector<float>& x, double fromSeconds, double toSeconds, double rate)
{
    const auto track = animalaudio::dsp::ProbabilisticYin::analyse(x.data(), static_cast<std::int64_t>(x.size()), rate);
    double sum = 0.0;
    int n = 0;
    for (const auto& f : track.frames)
    {
        const double t = f.centre / rate;
        if (t >= fromSeconds && t < toSeconds && f.hz > 0.0f)
        {
            sum += animalaudio::dsp::hzToMidi(f.hz);
            ++n;
        }
    }
    return n > 0 ? sum / n : -1.0;
}

inline bool same(double a, double b, double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance;
}
} // namespace monkeydyne::testing
