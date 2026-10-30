#pragma once

#include <cmath>
#include <string>

namespace animalaudio::dsp
{
/** "C4" for MIDI 60, sharps only. */
inline std::string noteName(int midiNote)
{
    static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int octave = midiNote / 12 - 1;
    return std::string(names[((midiNote % 12) + 12) % 12]) + std::to_string(octave);
}

inline float hzToMidi(float hz) noexcept
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

inline float midiToHz(float midi) noexcept
{
    return 440.0f * std::exp2((midi - 69.0f) / 12.0f);
}

/** True for the black keys (C#, D#, F#, G#, A#). */
inline bool isSharp(int midiNote) noexcept
{
    const int pc = ((midiNote % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace animalaudio::dsp
