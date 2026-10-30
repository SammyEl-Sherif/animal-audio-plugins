#pragma once

#include "TimePatterns.h"

#include <animalaudio/dsp/CycleScope.h>
#include <animalaudio/dsp/Smoothing.h>
#include <animalaudio/dsp/TempoSync.h>

#include <atomic>
#include <vector>

namespace turtletime::dsp
{
/**
 * Tempo-locked time warp: record into a circular buffer at 1x and read it back at a pattern's rate.
 *
 * On each musical window the read pointer snaps (catch-up) or a captured slice repeats. A short
 * crossfade hides the jump. The window locks to host PPQ while the transport is playing. Stopped,
 * the input is passed dry (the buffer still records) and the UI phase parks on the host playhead.
 * Playback resumes with a snap. Memory is allocated only in prepare().
 *
 * Keep pitch: slowed-down patterns are read as two Hann grains played at 1x, half a grain apart, whose
 * start points follow the slow read position (overlap-add time stretch). Each new grain start is nudged
 * to the offset whose recent past best matches the grain it overlaps, so steady tones add in phase.
 * Grains never read past the write head.
 */
class TurtleTimeEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kScopeBins = 192;
    using Scope = animalaudio::dsp::CycleScope<kScopeBins>;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset() noexcept;

    void setPatternIndex(int index) noexcept;
    void setMix(float newMix) noexcept;
    /** Slowed-down patterns keep the original pitch instead of dropping it. Switching crossfades. */
    void setKeepPitch(bool shouldKeepPitch) noexcept;

    void process(float* const* channels, int numChannels, int numSamples,
                 const animalaudio::dsp::TransportInfo& transport) noexcept;

    int getPatternIndex() const noexcept { return patternIndex; }
    bool getKeepPitch() const noexcept { return keepPitch; }
    float getPlayheadPhase() const noexcept { return playheadPhase.load(std::memory_order_relaxed); }
    float getReadMarker() const noexcept { return readMarker.load(std::memory_order_relaxed); }
    bool isHostSynced() const noexcept { return hostSynced.load(std::memory_order_relaxed) != 0; }
    /** Host tempo from the last block, or 0 when the host reports none. */
    double getHostBpm() const noexcept { return hostBpmSeen.load(std::memory_order_relaxed); }
    /** Input / output peaks across the current window, binned by window phase. Frozen while stopped. */
    const Scope& getInputScope() const noexcept { return inputScope; }
    const Scope& getOutputScope() const noexcept { return outputScope; }

private:
    /** One read head: interpolated reads at `rate` (tape), or two grains that follow `position` (keep pitch). */
    struct Reader
    {
        double position = 0.0;
        float rate = 1.0f;
        bool grains = false;
        int grainStart[2]{};
        int grainAge[2]{};
    };

    void startReader(Reader& reader, double position, float rate) noexcept;
    float readSample(const Reader& reader, int channel) const noexcept;
    void advance(Reader& reader) noexcept;
    int alignedGrainStart(const Reader& reader, int otherGrain) const noexcept;
    float monoAt(int index) const noexcept;
    void startFade(int span) noexcept;
    float readCubic(int channel, double position) const noexcept;
    int fadeSamplesFor(const TimePattern& pattern, double bpm) const noexcept;

    std::vector<float> buffer;
    int capacity = 0;
    int writeIndex = 0;
    int channelsInUse = 1;

    Reader reader;
    Reader fadeReader;
    int fadeRemaining = 0;
    int fadeSpanSamples = 1;

    std::vector<float> grainWindow; // periodic Hann: the two half-offset grains sum to exactly 1
    int grainLength = 2;
    int alignSearch = 0;  // max grain-start nudge, samples each way
    int alignContext = 1; // samples of recent past compared when aligning
    bool keepPitch = false;
    bool keepPitchDirty = false;

    int lastWindow = 0;
    int lastSlice = -1;
    double anchorPos = 0.0;
    bool primed = false;
    bool patternDirty = false;
    bool holding = false; // transport was stopped; the next playing block must snap

    animalaudio::dsp::OnePoleSmoother mixSmoother;
    float mix = 1.0f;
    int patternIndex = 0;
    double sampleRate = 48000.0;
    int crossfadeSamples = 1;

    std::atomic<float> playheadPhase{0.0f};
    std::atomic<float> readMarker{0.0f};
    std::atomic<int> hostSynced{0};
    std::atomic<double> hostBpmSeen{0.0};
    Scope inputScope;
    Scope outputScope;
};
} // namespace turtletime::dsp
