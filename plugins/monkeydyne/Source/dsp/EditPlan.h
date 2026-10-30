#pragma once

#include "Model.h"

#include <utility>
#include <vector>

namespace monkeydyne::dsp
{
/**
 * What the edited take sounds like, as functions of output time: where to read the source (time map), how far to
 * shift the pitch, the formant and the gain. The renderer and the note canvas both read it.
 *
 * Pitch: a note's target pitch is
 *     target(t) = centre' + drift * (slow(t) - centre) + vibrato * (sung(t) - slow(t)),
 * with centre' = centre + pitch, pulled `snap` of the way to the scale, `slow` the vibrato-free pitch. The shift is
 * target - sung. Between linked notes the two notes' shifts blend over the earlier note's `glide`, shaped by its
 * `curve` (smoothstep, warped earlier or later), so a moved note slides out of and into its neighbours.
 * A shaped transition also fades the drift and vibrato terms out near the boundary (fully within 80 ms or half
 * the glide, back over 40 ms): `slow` follows the sung slide (a 4 Hz low-pass smears it), so only removing both
 * replaces that slide with the designed one.
 *
 * Time: each note maps its source span linearly onto its output span; when it is stretched, the first 30 ms of the
 * voiced part (and the consonants before it) keep their speed and the rest absorbs the stretch. Gaps between notes
 * map linearly between their neighbours. A gap that was pulled open is silent, faded over 10 ms.
 *
 * Built on the message or worker thread (allocates); all queries are const and allocation-free. Keeps a pointer to
 * the analysis, which must outlive it.
 */
class EditPlan
{
public:
    EditPlan() = default;
    EditPlan(const TakeAnalysis& analysis, std::vector<Note> notes, const Scale& scale);

    const std::vector<Note>& notes() const noexcept { return notes_; }

    double sourceOf(double out) const noexcept;
    double outputOf(double source) const noexcept;

    float shiftAt(double out) const noexcept;   // semitones
    float formantAt(double out) const noexcept; // semitones
    float gainAt(double out) const noexcept;    // linear
    /** Pitch heard at `out` (MIDI), -1 where unvoiced. */
    float outputMidiAt(double out) const noexcept;
    float targetCentre(const Note& note) const noexcept;

    /** True when output [from, to) is the recording unchanged, so it needs no rendering. */
    bool isNeutral(double from, double to) const noexcept;

    /** Index of the note playing at `out`, or -1 in a gap. */
    int noteAt(double out) const noexcept;

private:
    struct Blend
    {
        int first = -1;
        int second = -1;
        float weight = 0.0f; // of `second`
    };

    Blend blendAt(double out, double minimumGlideSeconds) const noexcept;
    double glideSamples(const Note& a, const Note& b, double minimumGlideSeconds) const noexcept;
    /** 0..1: how much of note `index`'s own pitch movement stays at `out` (less near a shaped transition). */
    float movementKept(int index, double out) const noexcept;
    float noteShift(const Note& note, double source, float kept) const noexcept;
    float noteGain(int index) const noexcept;
    float gapGain(int before, double out) const noexcept;

    const TakeAnalysis* analysis = nullptr;
    std::vector<Note> notes_;
    Scale scale;
    std::vector<std::pair<double, double>> anchors; // (output, source), both non-decreasing
};

/** Source position of output time `out` inside `note` (attack kept at 1x when stretched). */
double noteSourceAt(const Note& note, double out, double rate) noexcept;

/** Transition weight 0..1 at u in [0, 1] for a curve in [-1, 1] (0 = smoothstep). */
float glideShape(float u, float curve) noexcept;
} // namespace monkeydyne::dsp
