#pragma once

#include "Model.h"

#include <functional>

namespace monkeydyne::dsp
{
/**
 * Finds the notes in a mono vocal take:
 *
 * 1. Pitch: offline pYIN with HMM decoding (`animalaudio::dsp::ProbabilisticYin`), one frame every ~5.8 ms. Voiced
 *    blips shorter than 23 ms are dropped; dropouts up to 58 ms inside loud, low-zero-crossing stretches (fast
 *    slides, where YIN's window sees two periods) are bridged by interpolating the pitch.
 * 2. Drift: the pitch through a zero-phase 4 Hz low-pass, which removes vibrato but keeps note steps and slow wander.
 * 3. Words: maximal voiced stretches. Phrases: words grouped across pauses shorter than 200 ms.
 * 4. Notes: inside a word, level dips of 6 dB (re-sung syllables) and changes of the drift's mean (PELT, glide
 *    frames weighted down) split notes; neighbours closer than 0.6 semitones merge again, and each boundary moves to
 *    the steepest point of the pitch curve near it. Notes inside a word are linked (soft separations).
 * 5. Consonants: loud unvoiced frames before a word (up to 200 ms) and after it (up to 150 ms) join its first / last
 *    note, split at the quietest point between two words.
 * 6. Pitch marks one period apart over every voiced stretch, phase locked to the waveform (`psola`), for the renderer.
 * 7. Render cuts at the quietest point of each pause between words: segments between cuts re-render independently.
 *
 * Allocates; for a background thread. `progress` (0..1) may return false to cancel (returns false, `out` is empty).
 */
bool analyseTake(const float* mono, std::int64_t length, double rate, TakeAnalysis& out,
                 const std::function<bool(float)>& progress = {});
} // namespace monkeydyne::dsp
