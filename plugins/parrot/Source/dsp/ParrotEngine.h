#pragma once

#include "VoiceShifter.h"

#include <animalaudio/dsp/Smoothing.h>
#include <animalaudio/dsp/TubeDrive.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace parrot::dsp
{
enum class Mode
{
    Shift = 0, // transpose by the PITCH knob
    Snap,      // transpose, then pull to the nearest semitone
    Robot      // hold one note (NOTE selector, or the MIDI note held), PITCH transposes it
};

inline constexpr int kNumModes = 3;
inline constexpr int kLowestNote = 36;  // C2 (middle C = C4 = MIDI 60)
inline constexpr int kNumNotes = 48;    // C2 .. B5
inline constexpr int kDefaultNote = 48; // C3
inline constexpr float kMaxShift = 12.0f;
inline constexpr float kMaxGlideMs = 500.0f;

std::string noteName(int midiNote);
float hzToMidi(float hz) noexcept;

/** PITCH as applied: whole semitones unless FINE. */
inline float appliedPitch(float pitch, bool fine) noexcept
{
    return fine ? pitch : std::round(pitch);
}

/** FORMANT as applied: its own value (whole semitones unless FINE), plus the applied PITCH when linked, +-12. */
inline float appliedFormant(float pitch, float formant, bool fine, bool link) noexcept
{
    const float own = fine ? formant : std::round(formant);
    return std::clamp(own + (link ? appliedPitch(pitch, fine) : 0.0f), -kMaxShift, kMaxShift);
}

/**
 * Parrot's signal path: VoiceShifter (pitch + formant) -> TubeDrive -> mix with the dry signal delayed by the
 * same latency. The mode logic decides each voiced grain's pitch ratio.
 */
class ParrotEngine : private PitchMapper
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kTraceSize = 512;

    void prepare(double sampleRate);
    void reset() noexcept;

    int getLatencySamples() const noexcept { return latency; }

    void setPitch(float semitones) noexcept { pitch = semitones; }
    void setFormant(float semitones) noexcept { formant = semitones; }
    void setFine(bool on) noexcept { fine = on; }
    void setLink(bool on) noexcept { link = on; }
    void setMode(Mode m) noexcept { mode = m; }
    void setNote(int midiNote) noexcept { note = midiNote; }
    void setGlide(float milliseconds) noexcept { glideSeconds = std::max(0.0f, milliseconds) * 0.001f; }
    void setDrive(float amount) noexcept { tube.setDrive(amount); }
    void setMix(float amount) noexcept { mixSmoother.setTarget(std::clamp(amount, 0.0f, 1.0f)); }

    void noteOn(int midiNote) noexcept;
    void noteOff(int midiNote) noexcept;
    void allNotesOff() noexcept;

    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    float appliedPitch() const noexcept { return dsp::appliedPitch(pitch, fine); }
    float appliedFormant() const noexcept { return dsp::appliedFormant(pitch, formant, fine, link); }

    float getInputHz() const noexcept { return shifter.getInputHz(); }
    float getOutputHz() const noexcept { return shifter.getOutputHz(); }
    float getHeat() const noexcept { return tube.getHeat(); }
    int getHeldNote() const noexcept { return heldNote.load(std::memory_order_relaxed); }
    const VoiceShifter& getShifter() const noexcept { return shifter; }

    /** Pitch trace for the display: MIDI note numbers (fractional), < 0 while unvoiced. Index 0 = newest. */
    int getTraceCount() const noexcept { return traceCount.load(std::memory_order_acquire); }
    float traceInput(int age) const noexcept { return traceAt(traceIn, age); }
    float traceOutput(int age) const noexcept { return traceAt(traceOut, age); }
    double getTraceInterval() const noexcept { return traceIntervalSeconds; }

private:
    float pitchRatio(float inputHz, float elapsedSeconds) noexcept override;
    float traceAt(const std::array<std::atomic<float>, kTraceSize>& trace, int age) const noexcept;
    void pushTrace() noexcept;

    static constexpr int kChunk = 256;
    static constexpr int kMaxHeld = 16;

    VoiceShifter shifter;
    animalaudio::dsp::TubeDrive tube;
    animalaudio::dsp::OnePoleSmoother mixSmoother;
    animalaudio::dsp::OnePoleSmoother levelSmoother;
    static constexpr float kMinLevelGain = 0.63f; // -4 dB
    static constexpr float kMaxLevelGain = 1.58f; // +4 dB
    float levelCoeff = 0.0f;
    float inputPower = 0.0f;
    float outputPower = 0.0f;
    std::array<float, kChunk> referencePower{};

    double rate = 48000.0;
    int latency = 0;
    std::array<std::vector<float>, kMaxChannels> dryLine;
    std::int64_t dryMask = 0;
    std::int64_t dryWritten = 0;
    std::array<std::array<float, kChunk>, kMaxChannels> dryChunk{};

    float pitch = 0.0f;
    float formant = 0.0f;
    bool fine = false;
    bool link = false;
    Mode mode = Mode::Shift;
    int note = kDefaultNote;
    float glideSeconds = 0.0f;

    int snapTarget = 0;
    float snapCorrection = 0.0f;
    float robotPitch = 0.0f;

    std::array<int, kMaxHeld> held{};
    int heldCount = 0;
    std::atomic<int> heldNote{-1};

    std::array<std::atomic<float>, kTraceSize> traceIn{};
    std::array<std::atomic<float>, kTraceSize> traceOut{};
    std::atomic<int> traceCount{0};
    int traceInterval = 480;
    int sinceTrace = 0;
    double traceIntervalSeconds = 0.01;
};
} // namespace parrot::dsp
