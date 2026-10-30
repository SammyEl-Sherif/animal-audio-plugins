#include "Session.h"

#include <algorithm>
#include <cmath>

namespace whale
{
namespace
{
constexpr std::size_t kMaxUndo = 200;
constexpr double kGrid = 8388608.0; // 2^23: one step of a 24-bit sample

const juce::Identifier kSession{"Session"};
const juce::Identifier kTake{"Take"};
const juce::Identifier kNote{"Note"};

juce::ValueTree noteToTree(const dsp::Note& n)
{
    juce::ValueTree t(kNote);
    t.setProperty("id", static_cast<int>(n.id), nullptr);
    t.setProperty("srcStart", n.srcStart, nullptr);
    t.setProperty("srcEnd", n.srcEnd, nullptr);
    t.setProperty("voicedStart", n.voicedStart, nullptr);
    t.setProperty("voicedEnd", n.voicedEnd, nullptr);
    t.setProperty("centre", n.centre, nullptr);
    t.setProperty("word", n.word, nullptr);
    t.setProperty("phrase", n.phrase, nullptr);
    t.setProperty("linked", n.linkedToNext, nullptr);
    t.setProperty("outStart", n.outStart, nullptr);
    t.setProperty("outEnd", n.outEnd, nullptr);
    t.setProperty("pitch", n.pitch, nullptr);
    t.setProperty("snap", n.snap, nullptr);
    t.setProperty("drift", n.drift, nullptr);
    t.setProperty("vibrato", n.vibrato, nullptr);
    t.setProperty("formant", n.formant, nullptr);
    t.setProperty("gain", n.gainDb, nullptr);
    t.setProperty("muted", n.muted, nullptr);
    t.setProperty("glide", n.glide, nullptr);
    t.setProperty("curve", n.curve, nullptr);
    t.setProperty("shaped", n.shaped, nullptr);
    return t;
}

dsp::Note noteFromTree(const juce::ValueTree& t)
{
    dsp::Note n;
    n.id = static_cast<std::uint32_t>(static_cast<int>(t.getProperty("id", 0)));
    n.srcStart = t.getProperty("srcStart", 0.0);
    n.srcEnd = t.getProperty("srcEnd", 0.0);
    n.voicedStart = t.getProperty("voicedStart", n.srcStart);
    n.voicedEnd = t.getProperty("voicedEnd", n.srcEnd);
    n.centre = t.getProperty("centre", 60.0f);
    n.word = t.getProperty("word", 0);
    n.phrase = t.getProperty("phrase", 0);
    n.linkedToNext = t.getProperty("linked", false);
    n.outStart = t.getProperty("outStart", n.srcStart);
    n.outEnd = t.getProperty("outEnd", n.srcEnd);
    n.pitch = t.getProperty("pitch", 0.0f);
    n.snap = t.getProperty("snap", 0.0f);
    n.drift = t.getProperty("drift", 1.0f);
    n.vibrato = t.getProperty("vibrato", 1.0f);
    n.formant = t.getProperty("formant", 0.0f);
    n.gainDb = t.getProperty("gain", 0.0f);
    n.muted = t.getProperty("muted", false);
    n.glide = t.getProperty("glide", dsp::kDefaultGlide);
    n.curve = t.getProperty("curve", 0.0f);
    n.shaped = t.getProperty("shaped", false);
    return n;
}
} // namespace

std::shared_ptr<TakeAudio> TakeAudio::fromChannels(std::vector<std::vector<float>> channels, double rate,
                                                   float storageGain)
{
    auto audio = std::make_shared<TakeAudio>();
    audio->rate = rate;
    if (channels.size() > 2)
        channels.resize(2);
    if (channels.size() == 2)
    {
        bool identical = true;
        for (std::size_t i = 0; i < channels[0].size() && identical; ++i)
            identical = std::abs(channels[0][i] - channels[1][i]) < 1.0e-7f;
        if (identical)
            channels.pop_back();
    }
    float peak = 0.0f;
    for (const auto& channel : channels)
        for (const float x : channel)
            peak = std::max(peak, std::abs(x));
    audio->storageGain = storageGain > 0.0f ? storageGain : (peak > 0.999f ? peak / 0.999f : 1.0f);
    const double gain = audio->storageGain;
    for (auto& channel : channels)
        for (auto& x : channel)
            x = static_cast<float>(std::round(static_cast<double>(x) / gain * kGrid) / kGrid * gain);

    const auto length = channels.empty() ? std::size_t{0} : channels[0].size();
    audio->mono.assign(length, 0.0f);
    for (const auto& channel : channels)
        for (std::size_t i = 0; i < length; ++i)
            audio->mono[i] += channel[i] / static_cast<float>(channels.size());
    for (std::size_t i = 0; i < length; i += kOverviewStep)
    {
        float p = 0.0f;
        for (std::size_t j = i; j < std::min(length, i + kOverviewStep); ++j)
            p = std::max(p, std::abs(audio->mono[j]));
        audio->overview.push_back(p);
    }
    audio->channels = std::move(channels);
    return audio;
}

std::vector<TakeInfo> Session::takes() const
{
    const juce::ScopedLock sl(lock);
    std::vector<TakeInfo> out;
    out.reserve(entries.size());
    for (const auto& e : entries)
        out.push_back(e.info);
    return out;
}

dsp::Scale Session::scale() const
{
    const juce::ScopedLock sl(lock);
    return currentScale;
}

void Session::changed(bool edit)
{
    changeCount.fetch_add(1, std::memory_order_acq_rel);
    if (edit)
        edits.fetch_add(1, std::memory_order_acq_rel);
}

Session::Entry* Session::find(std::uint32_t takeId)
{
    for (auto& e : entries)
        if (e.info.id == takeId)
            return &e;
    return nullptr;
}

Session::UndoState Session::captureUndoState() const
{
    UndoState state;
    state.scale = currentScale;
    for (const auto& e : entries)
        state.notes.emplace_back(e.info.id, e.info.notes);
    return state;
}

void Session::applyUndoState(const UndoState& state)
{
    for (const auto& [id, notes] : state.notes)
        if (auto* e = find(id))
            e->info.notes = notes;
    currentScale = state.scale;
    changed(true);
}

void Session::checkpoint()
{
    const juce::ScopedLock sl(lock);
    undoStack.push_back(captureUndoState());
    if (undoStack.size() > kMaxUndo)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void Session::setNotes(std::uint32_t takeId, std::vector<dsp::Note> notes)
{
    const juce::ScopedLock sl(lock);
    if (auto* e = find(takeId))
    {
        e->info.notes = std::move(notes);
        changed(true);
    }
}

void Session::setScale(const dsp::Scale& scale)
{
    const juce::ScopedLock sl(lock);
    currentScale = scale;
    changed(true);
}

bool Session::undo()
{
    const juce::ScopedLock sl(lock);
    if (undoStack.empty())
        return false;
    redoStack.push_back(captureUndoState());
    applyUndoState(undoStack.back());
    undoStack.pop_back();
    return true;
}

bool Session::redo()
{
    const juce::ScopedLock sl(lock);
    if (redoStack.empty())
        return false;
    undoStack.push_back(captureUndoState());
    applyUndoState(redoStack.back());
    redoStack.pop_back();
    return true;
}

bool Session::canUndo() const
{
    const juce::ScopedLock sl(lock);
    return !undoStack.empty();
}

bool Session::canRedo() const
{
    const juce::ScopedLock sl(lock);
    return !redoStack.empty();
}

void Session::removeAllTakes()
{
    const juce::ScopedLock sl(lock);
    entries.clear();
    undoStack.clear();
    redoStack.clear();
    changed(true);
}

std::uint32_t Session::newNoteId()
{
    const juce::ScopedLock sl(lock);
    return nextNoteId++;
}

Session::Snapshot Session::snapshot() const
{
    const juce::ScopedLock sl(lock);
    Snapshot s;
    s.editGeneration = edits.load(std::memory_order_acquire);
    s.scale = currentScale;
    for (const auto& e : entries)
        s.takes.push_back(e.info);
    return s;
}

std::uint32_t Session::addTake(double startSeconds, std::shared_ptr<const TakeAudio> audio)
{
    const juce::ScopedLock sl(lock);
    Entry entry;
    entry.info.id = nextTakeId++;
    entry.info.startSeconds = startSeconds;
    entry.info.audio = std::move(audio);
    const double end = entry.info.endSeconds();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const Entry& e)
                                 { return e.info.startSeconds < end && startSeconds < e.info.endSeconds(); }),
                  entries.end());
    const auto at = std::find_if(entries.begin(), entries.end(),
                                 [&](const Entry& e) { return e.info.startSeconds > startSeconds; });
    const auto id = entry.info.id;
    entries.insert(at, std::move(entry));
    changed(true);
    return id;
}

void Session::setProgress(std::uint32_t takeId, float progress)
{
    const juce::ScopedLock sl(lock);
    if (auto* e = find(takeId))
    {
        e->info.progress = progress;
        changed(false);
    }
}

void Session::setAnalysis(std::uint32_t takeId, std::shared_ptr<const dsp::TakeAnalysis> analysis)
{
    const juce::ScopedLock sl(lock);
    auto* e = find(takeId);
    if (e == nullptr)
        return;
    if (!e->notesRestored)
    {
        e->info.notes = dsp::notesFromAnalysis(*analysis, nextNoteId);
        nextNoteId += static_cast<std::uint32_t>(e->info.notes.size());
    }
    e->info.analysis = std::move(analysis);
    e->info.status = TakeStatus::Ready;
    e->info.progress = 1.0f;
    changed(true);
}

juce::ValueTree Session::toValueTree() const
{
    const juce::ScopedLock sl(lock);
    juce::ValueTree tree(kSession);
    tree.setProperty("key", currentScale.key, nullptr);
    tree.setProperty("mode", static_cast<int>(currentScale.mode), nullptr);
    for (const auto& e : entries)
    {
        juce::ValueTree take(kTake);
        take.setProperty("id", static_cast<int>(e.info.id), nullptr);
        take.setProperty("start", e.info.startSeconds, nullptr);
        for (const auto& note : e.info.notes)
            take.appendChild(noteToTree(note), nullptr);
        tree.appendChild(take, nullptr);
    }
    return tree;
}

std::vector<std::shared_ptr<const TakeAudio>> Session::takeAudio() const
{
    const juce::ScopedLock sl(lock);
    std::vector<std::shared_ptr<const TakeAudio>> out;
    for (const auto& e : entries)
        out.push_back(e.info.audio);
    return out;
}

void Session::restore(const juce::ValueTree& tree, std::vector<std::shared_ptr<const TakeAudio>> audio)
{
    const juce::ScopedLock sl(lock);
    entries.clear();
    undoStack.clear();
    redoStack.clear();
    currentScale = {};
    if (tree.hasType(kSession))
    {
        currentScale.key = std::clamp(static_cast<int>(tree.getProperty("key", 0)), 0, 11);
        currentScale.mode =
            static_cast<dsp::Scale::Mode>(std::clamp(static_cast<int>(tree.getProperty("mode", 0)), 0, 2));
        std::size_t index = 0;
        for (const auto& child : tree)
        {
            if (!child.hasType(kTake))
                continue;
            if (index >= audio.size() || audio[index] == nullptr)
                break;
            Entry entry;
            entry.info.id = static_cast<std::uint32_t>(static_cast<int>(child.getProperty("id", 0)));
            entry.info.startSeconds = child.getProperty("start", 0.0);
            entry.info.audio = audio[index++];
            entry.notesRestored = true;
            for (const auto& n : child)
                if (n.hasType(kNote))
                {
                    entry.info.notes.push_back(noteFromTree(n));
                    nextNoteId = std::max(nextNoteId, entry.info.notes.back().id + 1);
                }
            std::sort(entry.info.notes.begin(), entry.info.notes.end(),
                      [](const dsp::Note& a, const dsp::Note& b) { return a.srcStart < b.srcStart; });
            nextTakeId = std::max(nextTakeId, entry.info.id + 1);
            entries.push_back(std::move(entry));
        }
    }
    changed(true);
}
} // namespace whale
