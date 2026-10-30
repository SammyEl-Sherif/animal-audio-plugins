#pragma once

#include "Playback.h"
#include "Renderer.h"

#include <functional>

namespace monkeydyne::dsp
{
/**
 * A take's rendered segments, kept up to date with its edits. Segments run between the analysis render cuts
 * (pauses), at their edited output positions; a segment whose surroundings are unedited is left out (the original
 * plays). Each segment remembers a signature of everything that can change its audio: its range and the notes
 * within 0.8 s of it. update() re-renders only segments whose signature changed.
 */
class SegmentCache
{
public:
    static constexpr double kFadeSeconds = 0.01;

    /**
     * Brings the cache up to date with `plan`. `published` is called after each newly rendered segment;
     * `stop` (may be empty) is polled between segments and aborts the update (returns false), keeping what is done.
     */
    bool update(const SourceView& source, const TakeAnalysis& analysis, const EditPlan& plan,
                const std::function<void()>& published = {}, const std::function<bool()>& stop = {},
                const std::atomic<bool>* cancel = nullptr);

    const std::vector<std::shared_ptr<const RenderedSegment>>& segments() const noexcept { return current; }
    int renderedLastUpdate() const noexcept { return renderedCount; }
    void clear()
    {
        current.clear();
        signatures.clear();
    }

private:
    struct Planned
    {
        std::int64_t begin = 0, end = 0, rise = 0, fall = 0;
        bool rises = false, falls = false, neutral = true;
        std::uint64_t signature = 0;
    };

    std::vector<Planned> planSegments(const TakeAnalysis& analysis, const EditPlan& plan) const;

    std::vector<std::shared_ptr<const RenderedSegment>> current;
    std::vector<std::uint64_t> signatures; // parallel to `current`
    Renderer renderer;
    FadeTable fades;
    int renderedCount = 0;
};
} // namespace monkeydyne::dsp
