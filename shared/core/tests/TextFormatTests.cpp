#include <animalaudio/core/TextFormat.h>

#include <catch2/catch_test_macros.hpp>

using animalaudio::core::formatSemitones;

TEST_CASE("text format: semitones")
{
    CHECK(formatSemitones(2.0f) == "+2 st");
    CHECK(formatSemitones(-0.35f) == "-0.35 st");
    CHECK(formatSemitones(0.0f) == "0 st");
    CHECK(formatSemitones(-0.0f) == "0 st");
    // Values that are whole only up to float error (a slider stepping back to zero) still read as whole numbers.
    CHECK(formatSemitones(2.498e-16f) == "0 st");
    CHECK(formatSemitones(1.9961f) == "+2 st");
    CHECK(formatSemitones(-12.0001f) == "-12 st");
    CHECK(formatSemitones(1.994f) == "+1.99 st");
}
