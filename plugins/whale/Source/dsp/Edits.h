#pragma once

#include "Model.h"

#include <vector>

/**
 * Edits on a take's notes (sorted by source position). Timing edits keep the notes in order and never overlap:
 * a note linked to a moved one stretches to stay attached, an unlinked neighbour is a wall, and no note gets
 * shorter than 20 ms or stretched beyond 1/4x..4x. Every function returns what it actually applied.
 */
namespace whale::dsp::edit
{
inline constexpr double kShortestNoteSeconds = 0.02;
inline constexpr double kMinStretch = 0.25;
inline constexpr double kMaxStretch = 4.0;
inline constexpr float kSplitGlide = 0.03f;

int indexOf(const std::vector<Note>& notes, std::uint32_t id) noexcept;

/** Moves the selected notes by `delta` output samples, clamped to what the constraints allow. */
double moveTime(std::vector<Note>& notes, const std::vector<bool>& selected, double delta, double takeLength,
                double rate);

/** Drags one edge of a note to output position `position`; returns where it ended up. */
double moveEdge(std::vector<Note>& notes, int index, bool startEdge, double position, double takeLength, double rate);

/**
 * Cuts a note at output position `position` into two linked notes; the second gets `newId`, both keep the edits.
 * False if either part would be shorter than 20 ms.
 */
bool split(std::vector<Note>& notes, int index, double position, const TakeAnalysis& analysis, std::uint32_t newId);

/** Joins a note with the next one if they touch; the result keeps the first note's edits. */
bool merge(std::vector<Note>& notes, int index, const TakeAnalysis& analysis);

/** Soft (linked) or hard separation after note `index`; only touching notes can be linked. */
bool setLinked(std::vector<Note>& notes, int index, bool linked);

bool canLink(const std::vector<Note>& notes, int index) noexcept;

/** Restores pitch, snap, drift, vibrato, formant, gain, mute and glide to the recording (keeps splits). */
void resetSound(Note& note) noexcept;

/**
 * Puts the selected notes back at their recorded time. Linked neighbours (and anything they would then overlap)
 * are reset with them, so the notes stay attached and in order.
 */
void resetTiming(std::vector<Note>& notes, const std::vector<bool>& selected);
} // namespace whale::dsp::edit
