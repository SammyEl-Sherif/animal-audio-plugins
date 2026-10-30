#pragma once

#include <cstdint>
#include <vector>

/**
 * Monkeydyne's data model, JUCE-free. A take is one stretch of captured audio. Analysis (Analysis.h) turns it into a
 * pitch track, pitch marks and notes; the user then edits the notes. Times are in samples from the take start:
 * "source" positions point into the captured audio, "output" positions are where they play after time edits.
 */
namespace monkeydyne::dsp
{
struct AnalysisFrame
{
    float midi = -1.0f;  // sung pitch, -1 where unvoiced
    float drift = -1.0f; // slow pitch (vibrato removed), -1 where unvoiced
    float levelDb = -120.0f;
    float voicing = 0.0f; // pYIN voicing probability
};

/** One glottal period of a voiced stretch ("island"); marks of one island are a period apart. */
struct PitchMark
{
    double position = 0.0;
    float period = 0.0f;
    int island = 0;
};

/** A note as analysis found it. Spans include the consonants around the voiced core. */
struct DetectedNote
{
    double start = 0.0;
    double end = 0.0;
    double voicedStart = 0.0;
    double voicedEnd = 0.0;
    float centre = 60.0f; // pitch centre, MIDI
    int word = 0;         // voiced island (syllable / word)
    int phrase = 0;       // group of words between pauses
    bool linkedToNext = false;
};

struct TakeAnalysis
{
    double rate = 48000.0;
    int hop = 280;
    std::int64_t length = 0;
    float floorDb = -60.0f;
    std::vector<AnalysisFrame> frames; // frame i is centred on sample i * hop
    std::vector<PitchMark> marks;      // sorted by position
    std::vector<DetectedNote> notes;   // sorted by start
    std::vector<double> cuts;          // render-segment boundaries in the source (first 0, last length)

    float midiAt(double sample) const noexcept; // interpolated between voiced frames, -1 if unvoiced
    float driftAt(double sample) const noexcept;
    float levelDbAt(double sample) const noexcept;
    /** Pitch centre of a stretch: level-weighted mean of its steady voiced frames, or -1 if none are voiced. */
    float centreBetween(double start, double end) const noexcept;
    /** Index of the mark nearest to `sample` (-1 if there are none). */
    int nearestMark(double sample) const noexcept;
};

/** Where SNAP pulls a note: the nearest semitone, or the nearest degree of a major / minor scale. */
struct Scale
{
    enum class Mode
    {
        Chromatic = 0,
        Major,
        Minor
    };

    int key = 0; // pitch class of the tonic, 0 = C
    Mode mode = Mode::Chromatic;

    bool contains(int midiNote) const noexcept;
    float nearest(float midi) const noexcept;
};

inline constexpr float kDefaultGlide = 0.05f; // seconds, transition between linked notes
inline constexpr float kMaxGlide = 0.6f;

/** A note and everything the user did to it. */
struct Note
{
    std::uint32_t id = 0;

    // Source: from analysis, or from splitting / merging.
    double srcStart = 0.0;
    double srcEnd = 0.0;
    double voicedStart = 0.0;
    double voicedEnd = 0.0;
    float centre = 60.0f;
    int word = 0;
    int phrase = 0;
    bool linkedToNext = false; // soft separation: shared edge, pitch / formant / gain transitions

    // Edits.
    double outStart = 0.0;
    double outEnd = 0.0;
    float pitch = 0.0f;   // semitones the user moved the note
    float snap = 0.0f;    // 0..1: how far the centre is pulled onto the scale
    float drift = 1.0f;   // slow pitch wander kept: 1 as sung, 0 straight
    float vibrato = 1.0f; // fast pitch movement kept: 1 as sung, 0 none, up to 2
    float formant = 0.0f; // semitones
    float gainDb = 0.0f;
    bool muted = false;
    // Transition into the next linked note. As sung (`shaped` false) the recording's own slide stays and only
    // the difference between the two notes' moves is blended over `glide`. Shaped, the pitch ramps from one note
    // to the next over `glide` along `curve`, and the sung slide around it is removed.
    float glide = kDefaultGlide; // seconds
    float curve = 0.0f;          // -1 (early) .. 1 (late)
    bool shaped = false;

    double srcLength() const noexcept { return srcEnd - srcStart; }
    double outLength() const noexcept { return outEnd - outStart; }
    bool isStretched() const noexcept;
    bool shapesTransition() const noexcept { return linkedToNext && shaped; }
    /** True when nothing about the note's sound or timing differs from the recording. */
    bool isUnedited() const noexcept;
};

Note noteFromDetection(const DetectedNote& detected, std::uint32_t id);

/** Notes of a take as analysis found them, unedited, ids from `firstId`. */
std::vector<Note> notesFromAnalysis(const TakeAnalysis& analysis, std::uint32_t firstId);
} // namespace monkeydyne::dsp
