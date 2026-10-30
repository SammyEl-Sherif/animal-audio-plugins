#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace animalaudio::core
{
/** "+2 st", "-0.35 st", "0 st". */
inline juce::String formatSemitones(float value)
{
    const bool whole = std::abs(value - std::round(value)) < 0.005f;
    // juce::String(double, 0) prints every digit ("2.498E-16"), so whole values go through an int.
    const juce::String number =
        whole ? juce::String(juce::roundToInt(std::abs(value))) : juce::String(std::abs(value), 2);
    return (value > 0.0005f ? "+" : value < -0.0005f ? "-" : "") + number + " st";
}

/** "+12¢", "-3¢", "±0¢". */
inline juce::String formatCents(int cents)
{
    return (cents > 0   ? "+"
            : cents < 0 ? "-"
                        : juce::String(juce::CharPointer_UTF8("\xc2\xb1"))) +
           juce::String(std::abs(cents)) + juce::String(juce::CharPointer_UTF8("\xc2\xa2"));
}

/** Parts joined by spaced middle dots, for status lines: "SNAP  ·  CHROMATIC". */
inline juce::String joinWithDots(std::initializer_list<juce::String> parts)
{
    static const juce::String separator(juce::CharPointer_UTF8("  \xc2\xb7  "));
    juce::StringArray all;
    for (const auto& part : parts)
        all.add(part);
    return all.joinIntoString(separator);
}
} // namespace animalaudio::core
