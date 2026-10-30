#pragma once

#include <animalaudio/dsp/PitchTracker.h>
#include <animalaudio/dsp/WindowedSinc.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace parrot::dsp
{
/** Decides the pitch ratio of each voiced grain; the plugin's modes (shift, snap, robot) live behind it. */
class PitchMapper
{
public:
    virtual ~PitchMapper() = default;

    /** `elapsedSeconds` is the time since the previous voiced grain (large after silence or noise). */
    virtual float pitchRatio(float inputHz, float elapsedSeconds) noexcept = 0;
};

/**
 * Pitch and formant shifter for a monophonic voice: TD-PSOLA (Moulines & Charpentier, Speech Communication 9,
 * 1990) with the formant shift done by resampling each grain.
 *
 * - Pitch marks: one per period, from the PitchTracker's period, each aligned to the previous one by normalised
 *   cross-correlation so the marks stay phase locked to the waveform.
 * - Grains: two periods of input under a Hann window (asymmetric when neighbouring periods differ), centred on
 *   a mark. Synthesis marks are spaced one input period / pitch ratio apart and take the nearest analysis mark,
 *   so timing is preserved while the repetition rate (pitch) changes.
 * - Formant ratio b: each grain is read b times faster (and is 1/b as long), which scales its spectral envelope by
 *   b without touching the repetition rate. Reads use a band-limited sinc whose cutoff follows b.
 * - Unvoiced input (noise, breath, consonants) uses fixed 5 ms grains at their own time: no pitch change, only the
 *   formant resampling, so there is no buzz.
 *
 * The latency is fixed (worst case for the lowest pitch and the lowest formant ratio) and reported by
 * getLatencySamples(). prepare() allocates; process() does not.
 */
class VoiceShifter
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr float kMinHz = 75.0f;
    static constexpr float kMaxHz = 1000.0f;
    static constexpr float kMinFormantRatio = 0.5f;
    static constexpr float kMaxFormantRatio = 2.0f;
    static constexpr float kMinPitchRatio = 0.25f;
    static constexpr float kMaxPitchRatio = 4.0f;

    struct Diagnostics
    {
        std::int64_t grains = 0;
        std::int64_t voicedGrains = 0;
        std::int64_t lateGrains = 0;    // launched after their window had started
        std::int64_t futureReads = 0;   // would read input that has not arrived yet
        std::int64_t droppedGrains = 0; // no free grain slot
        std::int64_t blindGrains = 0;   // launched before the analysis mark nearest to them was known
    };

    void prepare(double sampleRate);
    void reset() noexcept;

    int getLatencySamples() const noexcept { return latency; }

    void setFormantRatio(float ratio) noexcept;

    void process(float* const* channels, int numChannels, int numSamples, PitchMapper& mapper) noexcept;

    /** Pitch of the input at the output time and the pitch it is being turned into; 0 while unvoiced. */
    float getInputHz() const noexcept { return inputHz.load(std::memory_order_relaxed); }
    float getOutputHz() const noexcept { return outputHz.load(std::memory_order_relaxed); }

    const Diagnostics& diagnostics() const noexcept { return stats; }

private:
    struct Estimate
    {
        std::int64_t centre = 0;
        float period = 0.0f;
        bool voiced = false;
    };

    struct Mark
    {
        double position = 0.0;
        float period = 0.0f;
        bool voiced = false;
    };

    struct Grain
    {
        double source = 0.0;
        double centre = 0.0;
        float beta = 1.0f;
        float invLeft = 1.0f;
        float invRight = 1.0f;
        float gain = 1.0f;
        bool active = false;
    };

    static constexpr int kEstimateSlots = 128;
    static constexpr int kMarkSlots = 1024;
    static constexpr int kGrainSlots = 128;
    static constexpr int kWindowSize = 1024;

    const Estimate& estimateNear(std::int64_t position) const noexcept;
    void placeMarks() noexcept;
    double alignToPrevious(double previous, double guess, float period) const noexcept;
    double strongestNear(double guess, float period, double notBefore) const noexcept;
    const Mark& markAt(std::int64_t index) const noexcept
    {
        return marks[static_cast<std::size_t>(index % kMarkSlots)];
    }
    float meanSpacing(std::int64_t index, float period) const noexcept;
    void launchGrains(std::int64_t now, PitchMapper& mapper) noexcept;
    void startGrain(const Grain& grain) noexcept;
    float monoAt(std::int64_t index) const noexcept { return mono[static_cast<std::size_t>(index & mask)]; }

    double rate = 48000.0;
    int latency = 0;
    int unvoicedSpacing = 240;
    int maxPeriod = 640;
    float launchLead = 800.0f;
    int markLag = 0;
    std::int64_t mask = 0;
    std::int64_t written = 0;

    std::array<std::vector<float>, kMaxChannels> history;
    std::vector<float> mono;
    animalaudio::dsp::PitchTracker tracker;
    animalaudio::dsp::SincReader reader;
    std::array<float, kWindowSize + 2> hann{};

    std::array<Estimate, kEstimateSlots> estimates{};
    std::int64_t estimateCount = 0;
    Estimate silentEstimate;

    std::array<Mark, kMarkSlots> marks{};
    std::int64_t markCount = 0;
    double nextMarkPosition = 0.0;
    std::int64_t cursor = 0;

    std::array<Grain, kGrainSlots> grains{};
    double nextSynthesis = 0.0;
    double lastVoicedSynthesis = -1.0e12;
    bool lastGrainVoiced = false;
    float formantRatio = 1.0f;

    std::atomic<float> inputHz{0.0f};
    std::atomic<float> outputHz{0.0f};
    Diagnostics stats;
};
} // namespace parrot::dsp
