#pragma once

#include <array>
#include <string_view>

namespace turtletime::dsp
{
/** How a pattern moves the read pointer inside each musical window. */
enum class TimeMode
{
    CatchUp, // restart at "now" on each window and play at `rate` (half-time, reverse)
    Repeat   // capture one slice and replay it until the window ends
};

struct TimePattern
{
    std::string_view name;
    std::string_view blurb;
    float rate;         // samples the read pointer advances per output sample; negative plays backwards
    double windowBeats; // musical length between catch-up / recapture points (quarter-note beats)
    double sliceBeats;  // Repeat only: length of the looped slice
    TimeMode mode;
};

/** Patterns form a matrix: one row per behaviour, one column per musical length. Index = row * 8 + column. */
inline constexpr int kPatternsPerGroup = 8;
inline constexpr int kNumGroups = 4;
inline constexpr int kNumPatterns = kPatternsPerGroup * kNumGroups;
inline constexpr int kDefaultPattern = 6; // Half 1 Bar

inline constexpr std::array<std::string_view, kNumGroups> kGroupNames{"HALF", "SLOW", "REVERSE", "REPEAT"};
inline constexpr std::array<std::string_view, kNumGroups> kGroupDetails{"0.5X SPEED", "0.25X SPEED", "BACKWARDS",
                                                                        "SLICE LOOP"};
inline constexpr std::array<std::string_view, kPatternsPerGroup> kLengthNames{"1/32", "1/16", "1/8",   "1/4",
                                                                              "3/8",  "1/2",  "1 BAR", "2 BAR"};

inline constexpr std::array<TimePattern, kNumPatterns> kPatterns{{
    {"Half 1/32", "Half speed, retriggered every 1/32 note.", 0.5f, 0.125, 0.0, TimeMode::CatchUp},
    {"Half 1/16", "Half speed, retriggered every 1/16 note.", 0.5f, 0.25, 0.0, TimeMode::CatchUp},
    {"Half 1/8", "Half speed, retriggered every 1/8 note.", 0.5f, 0.5, 0.0, TimeMode::CatchUp},
    {"Half 1/4", "Half speed, retriggered every beat.", 0.5f, 1.0, 0.0, TimeMode::CatchUp},
    {"Half 3/8", "Half speed, retriggered every dotted quarter.", 0.5f, 1.5, 0.0, TimeMode::CatchUp},
    {"Half 1/2", "Half speed, retriggered every half note.", 0.5f, 2.0, 0.0, TimeMode::CatchUp},
    {"Half 1 Bar", "Half speed across one bar. The classic half-time.", 0.5f, 4.0, 0.0, TimeMode::CatchUp},
    {"Half 2 Bar", "Half speed across two bars.", 0.5f, 8.0, 0.0, TimeMode::CatchUp},

    {"Slow 1/32", "Quarter speed, retriggered every 1/32 note.", 0.25f, 0.125, 0.0, TimeMode::CatchUp},
    {"Slow 1/16", "Quarter speed, retriggered every 1/16 note.", 0.25f, 0.25, 0.0, TimeMode::CatchUp},
    {"Slow 1/8", "Quarter speed, retriggered every 1/8 note.", 0.25f, 0.5, 0.0, TimeMode::CatchUp},
    {"Slow 1/4", "Quarter speed, retriggered every beat.", 0.25f, 1.0, 0.0, TimeMode::CatchUp},
    {"Slow 3/8", "Quarter speed, retriggered every dotted quarter.", 0.25f, 1.5, 0.0, TimeMode::CatchUp},
    {"Slow 1/2", "Quarter speed, retriggered every half note.", 0.25f, 2.0, 0.0, TimeMode::CatchUp},
    {"Slow 1 Bar", "Quarter speed across one bar.", 0.25f, 4.0, 0.0, TimeMode::CatchUp},
    {"Slow 2 Bar", "Quarter speed across two bars.", 0.25f, 8.0, 0.0, TimeMode::CatchUp},

    {"Reverse 1/32", "Plays backwards, retriggered every 1/32 note.", -1.0f, 0.125, 0.0, TimeMode::CatchUp},
    {"Reverse 1/16", "Plays backwards, retriggered every 1/16 note.", -1.0f, 0.25, 0.0, TimeMode::CatchUp},
    {"Reverse 1/8", "Plays backwards, retriggered every 1/8 note.", -1.0f, 0.5, 0.0, TimeMode::CatchUp},
    {"Reverse 1/4", "Plays backwards, retriggered every beat.", -1.0f, 1.0, 0.0, TimeMode::CatchUp},
    {"Reverse 3/8", "Plays backwards, retriggered every dotted quarter.", -1.0f, 1.5, 0.0, TimeMode::CatchUp},
    {"Reverse 1/2", "Plays backwards, retriggered every half note.", -1.0f, 2.0, 0.0, TimeMode::CatchUp},
    {"Reverse 1 Bar", "Plays the last bar backwards.", -1.0f, 4.0, 0.0, TimeMode::CatchUp},
    {"Reverse 2 Bar", "Plays the last two bars backwards.", -1.0f, 8.0, 0.0, TimeMode::CatchUp},

    {"Repeat 1/32", "Loops the first 1/32 note of every bar.", 1.0f, 4.0, 0.125, TimeMode::Repeat},
    {"Repeat 1/16", "Loops the first 1/16 note of every bar.", 1.0f, 4.0, 0.25, TimeMode::Repeat},
    {"Repeat 1/8", "Loops the first 1/8 note of every bar.", 1.0f, 4.0, 0.5, TimeMode::Repeat},
    {"Repeat 1/4", "Loops the first beat of every bar.", 1.0f, 4.0, 1.0, TimeMode::Repeat},
    {"Repeat 3/8", "Loops the first dotted quarter of every bar.", 1.0f, 4.0, 1.5, TimeMode::Repeat},
    {"Repeat 1/2", "Loops the first half note of every bar.", 1.0f, 4.0, 2.0, TimeMode::Repeat},
    {"Repeat 1 Bar", "Loops one bar across two bars.", 1.0f, 8.0, 4.0, TimeMode::Repeat},
    {"Repeat 2 Bar", "Loops two bars across four bars.", 1.0f, 16.0, 8.0, TimeMode::Repeat},
}};

inline const TimePattern& patternAt(int index) noexcept
{
    if (index < 0)
        index = 0;
    if (index >= kNumPatterns)
        index = kNumPatterns - 1;
    return kPatterns[static_cast<std::size_t>(index)];
}

/** Only slowed-down playback lowers the pitch (reverse and repeat run at 1x), so only it can keep the pitch instead. */
inline constexpr bool slowsDown(float rate) noexcept
{
    return rate > 0.0f && rate < 1.0f;
}

/** What a pattern does to pitch, for the readout; empty when it plays at 1x. */
inline constexpr std::string_view pitchLabel(const TimePattern& pattern, bool keepPitch) noexcept
{
    if (!slowsDown(pattern.rate))
        return {};
    if (keepPitch)
        return "PITCH KEPT";
    return pattern.rate >= 0.5f ? "1 OCTAVE DOWN" : "2 OCTAVES DOWN";
}
} // namespace turtletime::dsp
