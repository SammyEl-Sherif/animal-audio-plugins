#pragma once

#include "dsp/Model.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>
#include <vector>

namespace monkeydyne
{
/** A recorded take's audio; immutable once made. Samples sit on a 24-bit grid so the saved copy is exact. */
struct TakeAudio
{
    static constexpr int kOverviewStep = 256;

    double rate = 48000.0;
    float storageGain = 1.0f; // samples / storageGain fit in [-1, 1) for the 24-bit FLAC copy
    std::vector<std::vector<float>> channels;
    std::vector<float> mono;
    std::vector<float> overview; // peak |mono| per kOverviewStep samples

    std::int64_t length() const noexcept { return mono.empty() ? 0 : static_cast<std::int64_t>(mono.size()); }

    /**
     * Builds mono, overview and the 24-bit grid from captured channels (identical channels become one). A
     * `storageGain` of 0 picks one from the peak.
     */
    static std::shared_ptr<TakeAudio> fromChannels(std::vector<std::vector<float>> channels, double rate,
                                                   float storageGain = 0.0f);
};

enum class TakeStatus
{
    Analysing,
    Ready
};

/** A take as the editor sees it: a copy, safe to keep. */
struct TakeInfo
{
    std::uint32_t id = 0;
    double startSeconds = 0.0;
    TakeStatus status = TakeStatus::Analysing;
    float progress = 0.0f;
    std::shared_ptr<const TakeAudio> audio;
    std::shared_ptr<const dsp::TakeAnalysis> analysis;
    std::vector<dsp::Note> notes;

    std::int64_t length() const noexcept { return audio != nullptr ? audio->length() : 0; }
    double rate() const noexcept { return audio != nullptr ? audio->rate : 48000.0; }
    double endSeconds() const noexcept { return startSeconds + static_cast<double>(length()) / rate(); }
};

/**
 * Everything Monkeydyne knows about the song: takes, their notes (with edits) and the scale, plus undo. The editor
 * edits on the message thread; the worker thread adds takes and analyses and reads snapshots to render. All
 * access goes through `lock` (never taken on the audio thread).
 */
class Session
{
public:
    // Message thread ----------------------------------------------------------------------------------------------
    std::vector<TakeInfo> takes() const;
    dsp::Scale scale() const;
    /** Changes whenever anything visible changes. */
    std::uint64_t generation() const noexcept { return changeCount.load(std::memory_order_acquire); }

    /** Saves the current notes and scale for undo; call once before an edit gesture. */
    void checkpoint();
    void setNotes(std::uint32_t takeId, std::vector<dsp::Note> notes);
    void setScale(const dsp::Scale& scale);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void removeAllTakes();
    std::uint32_t newNoteId();

    // Worker thread -----------------------------------------------------------------------------------------------
    struct Snapshot
    {
        std::uint64_t editGeneration = 0;
        dsp::Scale scale;
        std::vector<TakeInfo> takes;
    };
    Snapshot snapshot() const;
    std::uint64_t editGeneration() const noexcept { return edits.load(std::memory_order_acquire); }

    /** Adds a captured take; takes it overlaps on the timeline are replaced. */
    std::uint32_t addTake(double startSeconds, std::shared_ptr<const TakeAudio> audio);
    void setProgress(std::uint32_t takeId, float progress);
    /** Analysis done: notes come from it unless the take's notes were restored from a saved state. */
    void setAnalysis(std::uint32_t takeId, std::shared_ptr<const dsp::TakeAnalysis> analysis);

    // State -------------------------------------------------------------------------------------------------------
    juce::ValueTree toValueTree() const;
    /** Takes in the tree are matched with `audio` by index; they are re-analysed, their notes kept. */
    void restore(const juce::ValueTree& tree, std::vector<std::shared_ptr<const TakeAudio>> audio);
    std::vector<std::shared_ptr<const TakeAudio>> takeAudio() const;

private:
    struct Entry
    {
        TakeInfo info;
        bool notesRestored = false;
    };

    struct UndoState
    {
        std::vector<std::pair<std::uint32_t, std::vector<dsp::Note>>> notes;
        dsp::Scale scale;
    };

    UndoState captureUndoState() const;
    void applyUndoState(const UndoState& state);
    void changed(bool edit);
    Entry* find(std::uint32_t takeId);

    mutable juce::CriticalSection lock;
    std::vector<Entry> entries;
    dsp::Scale currentScale;
    std::vector<UndoState> undoStack;
    std::vector<UndoState> redoStack;
    std::uint32_t nextTakeId = 1;
    std::uint32_t nextNoteId = 1;
    std::atomic<std::uint64_t> changeCount{1};
    std::atomic<std::uint64_t> edits{1};
};
} // namespace monkeydyne
