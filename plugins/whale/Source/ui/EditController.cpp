#include "EditController.h"

#include <algorithm>

namespace whale::ui
{
EditController::EditController(Session& s)
    : session(s)
{
    reload();
}

bool EditController::refresh()
{
    if (session.generation() == seenGeneration)
        return false;
    reload();
    sendChangeMessage();
    return true;
}

void EditController::reload()
{
    seenGeneration = session.generation();
    mirror = session.takes();
    currentScale = session.scale();
    plans.clear();
    peaks.clear();
    for (const auto& t : mirror)
    {
        plans.push_back(t.analysis != nullptr ? std::make_unique<dsp::EditPlan>(*t.analysis, t.notes, currentScale)
                                              : nullptr);
        float peak = -60.0f;
        if (t.analysis != nullptr)
            for (const auto& frame : t.analysis->frames)
                peak = std::max(peak, frame.levelDb);
        peaks.push_back(peak);
    }

    std::set<NoteRef> kept;
    for (const auto& ref : selected)
        if (note(ref) != nullptr)
            kept.insert(ref);
    selected = std::move(kept);
}

const TakeInfo* EditController::take(std::uint32_t id) const noexcept
{
    for (const auto& t : mirror)
        if (t.id == id)
            return &t;
    return nullptr;
}

const dsp::EditPlan* EditController::plan(std::uint32_t takeId) const noexcept
{
    for (std::size_t i = 0; i < mirror.size(); ++i)
        if (mirror[i].id == takeId)
            return plans[i].get();
    return nullptr;
}

float EditController::peakDb(std::uint32_t takeId) const noexcept
{
    for (std::size_t i = 0; i < mirror.size(); ++i)
        if (mirror[i].id == takeId)
            return peaks[i];
    return 0.0f;
}

int EditController::indexOf(const NoteRef& ref) const noexcept
{
    if (const auto* t = take(ref.take))
        for (std::size_t i = 0; i < t->notes.size(); ++i)
            if (t->notes[i].id == ref.note)
                return static_cast<int>(i);
    return -1;
}

const dsp::Note* EditController::note(const NoteRef& ref) const noexcept
{
    const int index = indexOf(ref);
    return index >= 0 ? &take(ref.take)->notes[static_cast<std::size_t>(index)] : nullptr;
}

std::vector<NoteRef> EditController::allNotes() const
{
    std::vector<NoteRef> refs;
    for (const auto& t : mirror)
        for (const auto& n : t.notes)
            refs.push_back({t.id, n.id});
    return refs;
}

void EditController::setSelection(std::set<NoteRef> refs)
{
    if (refs == selected)
        return;
    selected = std::move(refs);
    sendChangeMessage();
}

void EditController::selectAll()
{
    const auto all = allNotes();
    setSelection(std::set<NoteRef>(all.begin(), all.end()));
}

void EditController::clearSelection()
{
    setSelection({});
}

std::vector<NoteRef> EditController::expand(const NoteRef& ref, Granularity granularity) const
{
    const auto* n = note(ref);
    if (n == nullptr || granularity == Granularity::Note)
        return {ref};
    std::vector<NoteRef> refs;
    for (const auto& other : take(ref.take)->notes)
        if (granularity == Granularity::Word ? other.word == n->word : other.phrase == n->phrase)
            refs.push_back({ref.take, other.id});
    return refs;
}

std::vector<NoteRef> EditController::targets() const
{
    if (!selected.empty())
        return {selected.begin(), selected.end()};
    return allNotes();
}

void EditController::beginGesture()
{
    gestureOpen = true;
    gestureChanged = false;
}

void EditController::endGesture()
{
    gestureOpen = false;
    gestureChanged = false;
}

void EditController::checkpointOnce()
{
    if (gestureOpen && gestureChanged)
        return;
    session.checkpoint();
    gestureChanged = true;
}

void EditController::setNotes(std::uint32_t takeId, std::vector<dsp::Note> notes)
{
    checkpointOnce();
    session.setNotes(takeId, std::move(notes));
    refresh();
}

void EditController::editTargets(const std::function<void(dsp::Note&)>& edit)
{
    editTakes(
        [&](const TakeInfo&, std::vector<dsp::Note>& notes, const std::vector<bool>& mask)
        {
            bool any = false;
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (mask[i])
                {
                    edit(notes[i]);
                    any = true;
                }
            return any;
        },
        false);
}

void EditController::editTakes(
    const std::function<bool(const TakeInfo&, std::vector<dsp::Note>&, const std::vector<bool>&)>& edit,
    bool selectionOnly)
{
    const bool all = selected.empty() && !selectionOnly;
    bool changed = false;
    for (const auto& t : std::vector<TakeInfo>(mirror))
    {
        if (t.analysis == nullptr)
            continue;
        auto notes = t.notes;
        std::vector<bool> mask(notes.size());
        bool any = false;
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            mask[i] = all || isSelected({t.id, notes[i].id});
            any = any || mask[i];
        }
        if (!any || !edit(t, notes, mask))
            continue;
        checkpointOnce();
        session.setNotes(t.id, std::move(notes));
        changed = true;
    }
    if (changed)
        refresh();
}

void EditController::setScale(const dsp::Scale& scale)
{
    checkpointOnce();
    session.setScale(scale);
    refresh();
}

bool EditController::undo()
{
    const bool done = session.undo();
    refresh();
    return done;
}

bool EditController::redo()
{
    const bool done = session.redo();
    refresh();
    return done;
}

void EditController::removeAllTakes()
{
    session.removeAllTakes();
    selected.clear();
    refresh();
}
} // namespace whale::ui
