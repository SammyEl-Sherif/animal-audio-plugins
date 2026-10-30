#pragma once

#include "dsp/EditPlan.h"
#include "engine/Session.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <set>
#include <vector>

namespace monkeydyne::ui
{
struct NoteRef
{
    std::uint32_t take = 0;
    std::uint32_t note = 0;

    bool operator<(const NoteRef& other) const noexcept
    {
        return take != other.take ? take < other.take : note < other.note;
    }
    bool operator==(const NoteRef& other) const noexcept { return take == other.take && note == other.note; }
    bool operator!=(const NoteRef& other) const noexcept { return !(*this == other); }
};

enum class Granularity
{
    Note = 0,
    Word,
    Phrase
};

/**
 * The editor's view of the session: a copy of the takes (with an edit plan per analysed take), the selection, and
 * edits as undoable gestures. Message thread only. Listeners hear about every change (selection or notes).
 *
 * A gesture (a drag, a knob turn) checkpoints the session once, on its first actual change, so a click that
 * changes nothing leaves no empty undo step.
 */
class EditController : public juce::ChangeBroadcaster
{
public:
    explicit EditController(Session& session);

    /** Picks up changes made elsewhere (worker, undo, state load); true if anything changed. */
    bool refresh();

    const std::vector<TakeInfo>& takes() const noexcept { return mirror; }
    const dsp::Scale& scale() const noexcept { return currentScale; }
    const TakeInfo* take(std::uint32_t id) const noexcept;
    /** Edit plan of a ready take (nullptr while it is analysed). */
    const dsp::EditPlan* plan(std::uint32_t takeId) const noexcept;
    const dsp::Note* note(const NoteRef& ref) const noexcept;
    int indexOf(const NoteRef& ref) const noexcept;
    std::vector<NoteRef> allNotes() const;
    /** Loudest analysis frame of a take (dB), for scaling note blobs. */
    float peakDb(std::uint32_t takeId) const noexcept;

    // Selection ---------------------------------------------------------------------------------------------------
    const std::set<NoteRef>& selection() const noexcept { return selected; }
    bool isSelected(const NoteRef& ref) const noexcept { return selected.count(ref) > 0; }
    void setSelection(std::set<NoteRef> refs);
    void selectAll();
    void clearSelection();
    /** The note with its word or its phrase. */
    std::vector<NoteRef> expand(const NoteRef& ref, Granularity granularity) const;
    /** What the inspector and the shortcuts act on: the selection, or every note when nothing is selected. */
    std::vector<NoteRef> targets() const;

    // Edits -------------------------------------------------------------------------------------------------------
    void beginGesture();
    void endGesture();
    bool inGesture() const noexcept { return gestureOpen; }

    /** Replaces a take's notes (checkpointing first unless a gesture already did). */
    void setNotes(std::uint32_t takeId, std::vector<dsp::Note> notes);
    /** Calls `edit` on each target note and commits the takes it touched. */
    void editTargets(const std::function<void(dsp::Note&)>& edit);
    /** Calls `edit` with each take's notes and a mask of its targets; commits takes where it returns true. */
    void editTakes(const std::function<bool(const TakeInfo&, std::vector<dsp::Note>&, const std::vector<bool>&)>& edit,
                   bool selectionOnly);
    void setScale(const dsp::Scale& scale);
    bool undo();
    bool redo();
    bool canUndo() const { return session.canUndo(); }
    bool canRedo() const { return session.canRedo(); }
    void removeAllTakes();
    std::uint32_t newNoteId() { return session.newNoteId(); }

private:
    void reload();
    void checkpointOnce();

    Session& session;
    std::vector<TakeInfo> mirror;
    std::vector<std::unique_ptr<dsp::EditPlan>> plans;
    std::vector<float> peaks;
    dsp::Scale currentScale;
    std::uint64_t seenGeneration = 0;
    std::set<NoteRef> selected;
    bool gestureOpen = false;
    bool gestureChanged = false;
};
} // namespace monkeydyne::ui
