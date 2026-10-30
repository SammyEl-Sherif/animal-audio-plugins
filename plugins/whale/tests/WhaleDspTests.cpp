#include "dsp/Analysis.h"
#include "dsp/EditPlan.h"
#include "dsp/Edits.h"
#include "dsp/Capture.h"
#include "dsp/Playback.h"
#include "dsp/Renderer.h"
#include "dsp/SegmentCache.h"
#include "WhaleTestSupport.h"

#include <animalaudio/dsp/Notes.h>
#include <animalaudio/dsp/ProbabilisticYin.h>
#include <animalaudio/testing/VoiceSynth.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using namespace whale::dsp;
using animalaudio::testing::SungNote;
using Catch::Matchers::WithinAbs;

namespace
{
using whale::testing::kPhraseSeconds;
using whale::testing::measuredMidi;
using whale::testing::phrase;
using whale::testing::same;

struct Take
{
    double rate = 48000.0;
    std::vector<float> audio;
    TakeAnalysis analysis;

    SourceView view() const
    {
        SourceView v;
        v.channels[0] = audio.data();
        v.numChannels = 1;
        v.mono = audio.data();
        v.length = static_cast<std::int64_t>(audio.size());
        return v;
    }
};

Take analysedPhrase(double rate)
{
    Take take;
    take.rate = rate;
    take.audio = animalaudio::testing::sing(phrase(), kPhraseSeconds, rate);
    REQUIRE(analyseTake(take.audio.data(), static_cast<std::int64_t>(take.audio.size()), rate, take.analysis));
    return take;
}

const Take& phraseAt48k()
{
    static const Take take = analysedPhrase(48000.0);
    return take;
}

std::vector<float> render(const Take& take, const std::vector<Note>& notes, std::int64_t begin, std::int64_t end,
                          const Scale& scale = {})
{
    const EditPlan plan(take.analysis, notes, scale);
    Renderer renderer;
    std::vector<std::vector<float>> out;
    REQUIRE(renderer.render(take.view(), take.analysis, plan, begin, end, out));
    return out[0];
}

std::vector<double> pitchCurve(const std::vector<float>& x, double rate)
{
    const auto track = animalaudio::dsp::ProbabilisticYin::analyse(x.data(), static_cast<std::int64_t>(x.size()), rate);
    std::vector<double> curve;
    for (const auto& f : track.frames)
        curve.push_back(f.hz > 0.0f ? animalaudio::dsp::hzToMidi(f.hz) : -1.0);
    return curve;
}

double rmsDb(const std::vector<float>& x, double fromSeconds, double toSeconds, double rate)
{
    const auto a = static_cast<std::size_t>(fromSeconds * rate);
    const auto b = std::min(x.size(), static_cast<std::size_t>(toSeconds * rate));
    return 20.0 * std::log10(animalaudio::testing::rms(x, a, b) + 1.0e-12);
}

int noteNear(const std::vector<Note>& notes, double seconds, double rate)
{
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].srcStart <= seconds * rate && seconds * rate < notes[i].srcEnd)
            return static_cast<int>(i);
    return -1;
}
} // namespace

TEST_CASE("whale: analysis finds the notes, words and phrases of a sung phrase")
{
    const double rate = GENERATE(44100.0, 48000.0);
    const auto take = analysedPhrase(rate);
    const auto& found = take.analysis.notes;
    const auto& sung = phrase();
    INFO("rate " << rate << ", notes found " << found.size());
    REQUIRE(found.size() == sung.size());

    const bool linked[] = {true, false, false, true, true, false};
    const int words[] = {0, 0, 1, 2, 2, 2};
    const int phrases[] = {0, 0, 1, 1, 1, 1};
    for (std::size_t i = 0; i < sung.size(); ++i)
    {
        const auto& note = found[i];
        const double expected = sung[i].midi + sung[i].detuneCents / 100.0;
        INFO("note " << i << " centre " << note.centre << " expected " << expected << " voiced "
                     << note.voicedStart / rate << ".." << note.voicedEnd / rate);
        CHECK(std::abs(note.centre - expected) < 0.12);
        CHECK(note.linkedToNext == linked[i]);
        CHECK(note.word == words[i]);
        CHECK(note.phrase == phrases[i]);
        // The voiced core covers the sung note (boundaries of legato notes sit inside the slide).
        const double sungStart = sung[i].start + sung[i].glideFromPrevious * 0.5;
        const double sungEnd = sung[i].start + sung[i].length;
        CHECK(std::abs(note.voicedStart / rate - sungStart) < 0.04);
        if (!(i + 1 < sung.size() && sung[i + 1].glideFromPrevious > 0.0))
            CHECK(std::abs(note.voicedEnd / rate - sungEnd) < 0.04);
        CHECK(note.start <= note.voicedStart);
        CHECK(note.end >= note.voicedEnd);
    }
    // Consonants join the note they lead into.
    CHECK(found[0].start / rate < 0.10 - 0.035);
    CHECK(found[2].start / rate < 1.40 - 0.05);

    // Render cuts sit between notes, never inside one.
    REQUIRE(take.analysis.cuts.size() == 4);
    CHECK(same(take.analysis.cuts.front(), 0.0));
    CHECK(same(take.analysis.cuts.back(), static_cast<double>(take.audio.size())));
    for (const double cut : take.analysis.cuts)
        for (const auto& note : found)
            CHECK((cut <= note.start || cut >= note.end));
}

TEST_CASE("whale: pitch marks are one period apart on steady notes")
{
    const auto& take = phraseAt48k();
    const double period = take.rate / animalaudio::testing::midiToHz(67.2);
    int checked = 0;
    for (std::size_t i = 0; i + 1 < take.analysis.marks.size(); ++i)
    {
        const auto& m = take.analysis.marks[i];
        if (m.position < 1.45 * take.rate || m.position > 1.70 * take.rate)
            continue;
        ++checked;
        CHECK(std::abs(take.analysis.marks[i + 1].position - m.position - period) < 0.01 * period);
    }
    CHECK(checked > 50);
}

TEST_CASE("whale: silence and a single wide-vibrato note")
{
    std::vector<float> silence(48000, 0.0f);
    TakeAnalysis analysis;
    REQUIRE(analyseTake(silence.data(), static_cast<std::int64_t>(silence.size()), 48000.0, analysis));
    CHECK(analysis.notes.empty());
    CHECK(analysis.cuts.size() == 2);

    const std::vector<SungNote> held{{62.0, 0.1, 1.6, 90.0, 5.0, 0.0, 0.0, 0.0, 1.0f}};
    const auto audio = animalaudio::testing::sing(held, 1.9, 48000.0);
    REQUIRE(analyseTake(audio.data(), static_cast<std::int64_t>(audio.size()), 48000.0, analysis));
    REQUIRE(analysis.notes.size() == 1);
    CHECK(std::abs(analysis.notes[0].centre - 62.0f) < 0.15f);
}

TEST_CASE("whale: scale snapping")
{
    Scale chromatic;
    CHECK(same(chromatic.nearest(60.4f), 60.0));
    CHECK(same(chromatic.nearest(60.6f), 61.0));
    Scale aMinor{9, Scale::Mode::Minor};
    CHECK(same(aMinor.nearest(61.2f), 62.0)); // C# is not in A minor: D is nearer than C
    CHECK(same(aMinor.nearest(60.9f), 60.0));
    Scale dMajor{2, Scale::Mode::Major};
    CHECK(dMajor.contains(66)); // F#
    CHECK_FALSE(dMajor.contains(65));
}

TEST_CASE("whale: an unedited plan is the identity")
{
    const auto& take = phraseAt48k();
    const auto notes = notesFromAnalysis(take.analysis, 1);
    const EditPlan plan(take.analysis, notes, {});
    for (double t = 0.0; t < static_cast<double>(take.audio.size()); t += 997.0)
    {
        CHECK(same(plan.sourceOf(t), t));
        CHECK(same(plan.outputOf(t), t));
        CHECK(same(plan.shiftAt(t), 0.0));
        CHECK(same(plan.gainAt(t), 1.0));
    }
    CHECK(plan.isNeutral(0.0, static_cast<double>(take.audio.size())));
}

TEST_CASE("whale: pitch plan moves, snaps, straightens and glides")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int a = noteNear(notes, 0.3, rate);
    const int b = noteNear(notes, 0.8, rate);
    REQUIRE(a >= 0);
    REQUIRE(b == a + 1);
    REQUIRE(notes[static_cast<std::size_t>(a)].linkedToNext);

    SECTION("moving one linked note slides into it over the glide")
    {
        notes[static_cast<std::size_t>(b)].pitch = 2.0f;
        const EditPlan plan(take.analysis, notes, {});
        const double edge = notes[static_cast<std::size_t>(b)].outStart;
        const double half = 0.5 * kDefaultGlide * rate;
        CHECK_THAT(plan.shiftAt(edge + half + 10.0), WithinAbs(2.0, 1.0e-4));
        CHECK_THAT(plan.shiftAt(edge - half - 10.0), WithinAbs(0.0, 1.0e-4));
        CHECK_THAT(plan.shiftAt(edge), WithinAbs(1.0, 1.0e-3));
        float previous = -1.0f;
        for (double t = edge - half; t <= edge + half; t += 16.0)
        {
            const float s = plan.shiftAt(t);
            CHECK(s >= previous - 1.0e-5f);
            previous = s;
        }
        CHECK_FALSE(plan.isNeutral(edge, edge + 1000.0));
        CHECK(plan.isNeutral(2.6 * rate, 3.2 * rate));

        // A late curve holds the first note longer; an early one leaves it sooner.
        notes[static_cast<std::size_t>(a)].curve = 0.8f;
        CHECK(EditPlan(take.analysis, notes, {}).shiftAt(edge) < 0.5f);
        notes[static_cast<std::size_t>(a)].curve = -0.8f;
        CHECK(EditPlan(take.analysis, notes, {}).shiftAt(edge) > 1.5f);
        // A longer glide starts earlier.
        notes[static_cast<std::size_t>(a)].curve = 0.0f;
        notes[static_cast<std::size_t>(a)].glide = 0.2f;
        CHECK(EditPlan(take.analysis, notes, {}).shiftAt(edge - 0.08 * rate) > 0.05f);
    }

    SECTION("snap pulls the centre onto the scale by the amount asked")
    {
        auto& note = notes[static_cast<std::size_t>(b)];
        note.centre = 64.3f;
        note.snap = 1.0f;
        CHECK_THAT(EditPlan(take.analysis, notes, {}).targetCentre(note), WithinAbs(64.0, 1.0e-5));
        note.snap = 0.5f;
        CHECK_THAT(EditPlan(take.analysis, notes, {}).targetCentre(note), WithinAbs(64.15, 1.0e-5));
        note.pitch = 0.5f; // 64.8 -> 65
        note.snap = 1.0f;
        CHECK_THAT(EditPlan(take.analysis, notes, {}).targetCentre(note), WithinAbs(65.0, 1.0e-5));
    }

    SECTION("straighten and vibrato reshape the curve around the centre")
    {
        auto& note = notes[static_cast<std::size_t>(b)];
        note.drift = 0.0f;
        note.vibrato = 0.0f;
        const EditPlan flat(take.analysis, notes, {});
        for (double t = 0.68 * rate; t < 0.90 * rate; t += 200.0)
            CHECK_THAT(flat.outputMidiAt(t), WithinAbs(note.centre, 0.01));

        note.drift = 1.0f;
        note.vibrato = 2.0f;
        const EditPlan wide(take.analysis, notes, {});
        double sungSpread = 0.0, wideSpread = 0.0;
        for (double t = 0.68 * rate; t < 0.90 * rate; t += 100.0)
        {
            sungSpread =
                std::max(sungSpread, static_cast<double>(std::abs(take.analysis.midiAt(t) - take.analysis.driftAt(t))));
            wideSpread =
                std::max(wideSpread, static_cast<double>(std::abs(wide.outputMidiAt(t) - take.analysis.driftAt(t))));
        }
        CHECK(sungSpread > 0.25);
        CHECK_THAT(wideSpread, WithinAbs(2.0 * sungSpread, 0.02));
    }

    SECTION("a muted note fades out and back in")
    {
        notes[static_cast<std::size_t>(a + 2)].muted = true; // "go"
        const auto& muted = notes[static_cast<std::size_t>(a + 2)];
        const EditPlan plan(take.analysis, notes, {});
        CHECK(same(plan.gainAt(0.5 * (muted.outStart + muted.outEnd)), 0.0));
        CHECK(same(plan.gainAt(muted.outStart - 100.0), 1.0));
        CHECK(plan.gainAt(muted.outStart + 0.0015 * rate) > 0.3f);
    }
}

TEST_CASE("whale: time map follows stretched and moved notes")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int g = noteNear(notes, 1.5, rate); // "go", unlinked on both sides
    REQUIRE(g >= 0);
    std::vector<bool> selected(notes.size(), false);
    selected[static_cast<std::size_t>(g)] = true;

    auto& note = notes[static_cast<std::size_t>(g)];
    const double length = static_cast<double>(take.audio.size());
    std::vector<bool> lastWord(notes.size(), false);
    for (std::size_t i = static_cast<std::size_t>(g) + 1; i < notes.size(); ++i)
        lastWord[i] = true;
    REQUIRE(same(edit::moveTime(notes, lastWord, 0.5 * rate, length, rate), 0.5 * rate));
    const double wanted = note.outStart + 1.5 * note.srcLength();
    REQUIRE(same(edit::moveEdge(notes, g, false, wanted, length, rate), wanted));
    const EditPlan plan(take.analysis, notes, {});
    CHECK_THAT(plan.sourceOf(note.outStart), WithinAbs(note.srcStart, 1.0e-6));
    CHECK_THAT(plan.sourceOf(note.outEnd), WithinAbs(note.srcEnd, 1.0e-6));
    // The consonant and the first 30 ms of the vowel keep their speed.
    const double attack = note.voicedStart - note.srcStart + 0.03 * rate;
    CHECK_THAT(plan.sourceOf(note.outStart + 0.5 * attack), WithinAbs(note.srcStart + 0.5 * attack, 1.0e-6));
    double previous = -1.0;
    for (double t = 0.0; t < length; t += 101.0)
    {
        const double s = plan.sourceOf(t);
        CHECK(s >= previous);
        CHECK_THAT(plan.outputOf(s), WithinAbs(t, 1.0e-3));
        previous = s;
    }
}

TEST_CASE("whale: timing edits keep notes attached, ordered and in range")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    const double length = static_cast<double>(take.audio.size());
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int first = noteNear(notes, 2.0, rate); // A3 -> D4 -> F4, all linked
    REQUIRE(first >= 0);
    const int mid = first + 1;
    std::vector<bool> selected(notes.size(), false);
    selected[static_cast<std::size_t>(mid)] = true;

    SECTION("moving a linked note stretches one neighbour and squeezes the other")
    {
        const double before = notes[static_cast<std::size_t>(first)].outLength();
        const double after = notes[static_cast<std::size_t>(mid + 1)].outLength();
        CHECK(same(edit::moveTime(notes, selected, 0.05 * rate, length, rate), 0.05 * rate));
        CHECK_THAT(notes[static_cast<std::size_t>(first)].outLength(), WithinAbs(before + 0.05 * rate, 1.0e-6));
        CHECK_THAT(notes[static_cast<std::size_t>(mid + 1)].outLength(), WithinAbs(after - 0.05 * rate, 1.0e-6));
        CHECK(same(notes[static_cast<std::size_t>(first)].outEnd, notes[static_cast<std::size_t>(mid)].outStart));
        CHECK(same(notes[static_cast<std::size_t>(mid)].outEnd, notes[static_cast<std::size_t>(mid + 1)].outStart));
    }

    SECTION("a move is clamped where a neighbour would get too short")
    {
        const double applied = edit::moveTime(notes, selected, 10.0 * rate, length, rate);
        const auto& last = notes[static_cast<std::size_t>(mid + 1)];
        CHECK(applied < 10.0 * rate);
        CHECK(last.outLength() >=
              std::max(edit::kShortestNoteSeconds * rate, edit::kMinStretch * last.srcLength()) - 1.0e-6);
    }

    SECTION("an unlinked neighbour is a wall")
    {
        std::vector<bool> word(notes.size(), false);
        for (int i = first; i <= mid + 1; ++i)
            word[static_cast<std::size_t>(i)] = true;
        const auto& previous = notes[static_cast<std::size_t>(first - 1)];
        edit::moveTime(notes, word, -5.0 * rate, length, rate);
        CHECK_THAT(notes[static_cast<std::size_t>(first)].outStart, WithinAbs(previous.outEnd, 1.0e-6));
    }

    SECTION("dragging an edge between linked notes moves both")
    {
        const double target = notes[static_cast<std::size_t>(mid)].outEnd + 0.04 * rate;
        CHECK(same(edit::moveEdge(notes, mid, false, target, length, rate), target));
        CHECK(same(notes[static_cast<std::size_t>(mid + 1)].outStart, target));
    }

    SECTION("split, merge, link and reset")
    {
        const auto count = notes.size();
        const double cut =
            0.5 * (notes[static_cast<std::size_t>(mid)].outStart + notes[static_cast<std::size_t>(mid)].outEnd);
        const auto original = notes[static_cast<std::size_t>(mid)];
        REQUIRE(edit::split(notes, mid, cut, take.analysis, 99));
        REQUIRE(notes.size() == count + 1);
        CHECK(notes[static_cast<std::size_t>(mid)].linkedToNext);
        CHECK(notes[static_cast<std::size_t>(mid + 1)].id == 99);
        CHECK(same(notes[static_cast<std::size_t>(mid)].outEnd, cut));
        CHECK(same(notes[static_cast<std::size_t>(mid + 1)].outStart, cut));
        CHECK(notes[static_cast<std::size_t>(mid + 1)].linkedToNext == original.linkedToNext);
        CHECK_FALSE(notes[static_cast<std::size_t>(mid)].shaped);
        CHECK(EditPlan(take.analysis, notes, {}).isNeutral(0.0, length)); // a cut alone changes nothing
        CHECK_FALSE(edit::split(notes, mid, notes[static_cast<std::size_t>(mid)].outStart + 10.0, take.analysis, 100));

        REQUIRE(edit::setLinked(notes, mid, false));
        CHECK(edit::canLink(notes, mid));
        REQUIRE(edit::merge(notes, mid, take.analysis));
        CHECK(notes.size() == count);
        CHECK(same(notes[static_cast<std::size_t>(mid)].srcStart, original.srcStart));
        CHECK(same(notes[static_cast<std::size_t>(mid)].srcEnd, original.srcEnd));
        CHECK(std::abs(notes[static_cast<std::size_t>(mid)].centre - original.centre) < 0.05f);

        // Gaps cannot be linked.
        CHECK_FALSE(edit::setLinked(notes, first - 1, true));

        edit::moveTime(notes, selected, 0.05 * rate, length, rate);
        edit::resetTiming(notes, selected);
        for (const auto& note : notes)
        {
            CHECK(same(note.outStart, note.srcStart));
            CHECK(same(note.outEnd, note.srcEnd));
        }
    }
}

TEST_CASE("whale: an unedited render is the recording")
{
    const auto& take = phraseAt48k();
    const auto notes = notesFromAnalysis(take.analysis, 1);
    const auto out = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));
    float worst = 0.0f;
    for (std::size_t i = 0; i < out.size(); ++i)
        worst = std::max(worst, std::abs(out[i] - take.audio[i]));
    CHECK(worst < 1.0e-4f);
}

TEST_CASE("whale: moving a note changes its pitch and nothing else")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    const float shift = GENERATE(-7.0f, -3.0f, 2.0f, 5.0f, 12.0f);
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int n = noteNear(notes, 2.0, rate); // A3, 220 Hz: harmonics close enough to see the formants
    notes[static_cast<std::size_t>(n)].pitch = shift;
    const auto out = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));

    const double sung = measuredMidi(take.audio, 1.90, 2.18, rate);
    const double moved = measuredMidi(out, 1.90, 2.18, rate);
    INFO("shift " << shift << " sung " << sung << " moved " << moved);
    CHECK(std::abs(moved - sung - shift) < 0.03);
    // Level follows the recording.
    CHECK(std::abs(rmsDb(out, 1.90, 2.18, rate) - rmsDb(take.audio, 1.90, 2.18, rate)) < 1.0);
    // Formants stay where they were (measurable while the harmonics still sample the envelope densely).
    if (std::abs(shift) <= 7.0f)
    {
        const auto begin = static_cast<std::size_t>(1.92 * rate);
        const auto count = static_cast<std::size_t>(0.24 * rate);
        const double formants = animalaudio::testing::formantShift(
            animalaudio::testing::envelopeDb(take.audio, begin, count, animalaudio::testing::midiToHz(sung), rate),
            animalaudio::testing::envelopeDb(out, begin, count, animalaudio::testing::midiToHz(moved), rate));
        INFO("formant shift " << formants);
        CHECK(std::abs(formants) <= 0.5);
    }
    // Other words are untouched.
    float worst = 0.0f;
    for (std::size_t i = 0; i < static_cast<std::size_t>(1.75 * rate); ++i)
        worst = std::max(worst, std::abs(out[i] - take.audio[i]));
    CHECK(worst < 1.0e-4f);
}

TEST_CASE("whale: formant moves independently of pitch")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    const float formant = GENERATE(-4.0f, 3.0f);
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int n = noteNear(notes, 2.0, rate);
    notes[static_cast<std::size_t>(n)].formant = formant;
    const auto out = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));
    const double sung = measuredMidi(take.audio, 1.90, 2.18, rate);
    CHECK(std::abs(measuredMidi(out, 1.90, 2.18, rate) - sung) < 0.03);
    const auto f0 = animalaudio::testing::midiToHz(sung);
    const auto begin = static_cast<std::size_t>(1.92 * rate);
    const auto count = static_cast<std::size_t>(0.24 * rate);
    const double moved =
        animalaudio::testing::formantShift(animalaudio::testing::envelopeDb(take.audio, begin, count, f0, rate),
                                           animalaudio::testing::envelopeDb(out, begin, count, f0, rate));
    INFO("formant " << formant << " measured " << moved);
    CHECK(std::abs(moved - formant) <= 0.75);
}

TEST_CASE("whale: stretching a note keeps its pitch")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    const double factor = GENERATE(0.6, 1.8);
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int g = noteNear(notes, 1.5, rate);
    auto& note = notes[static_cast<std::size_t>(g)];
    const double length = static_cast<double>(take.audio.size());
    std::vector<bool> lastWord(notes.size(), false);
    for (std::size_t i = static_cast<std::size_t>(g) + 1; i < notes.size(); ++i)
        lastWord[i] = true;
    REQUIRE(same(edit::moveTime(notes, lastWord, 0.5 * rate, length, rate), 0.5 * rate));
    const double wanted = note.outStart + factor * note.srcLength();
    REQUIRE(same(edit::moveEdge(notes, g, false, wanted, length, rate), wanted));
    const auto out = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));

    const double sung = measuredMidi(take.audio, 1.45, 1.70, rate);
    const double voicedOut = (note.voicedStart + 0.05 * rate) / rate;
    const double voicedOutEnd = (note.outEnd - 0.05 * rate) / rate;
    CHECK(std::abs(measuredMidi(out, voicedOut, voicedOutEnd, rate) - sung) < 0.05);
    CHECK(std::abs(rmsDb(out, voicedOut, voicedOutEnd, rate) - rmsDb(take.audio, 1.45, 1.70, rate)) < 1.0);

    // The voiced part lasts as long as asked: measure where pYIN hears voicing.
    const auto curve = pitchCurve(out, rate);
    const auto hop = animalaudio::dsp::ProbabilisticYin::analyse(out.data(), 4096, rate).hop;
    int voicedFrames = 0;
    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const double t = static_cast<double>(i) * hop;
        voicedFrames += t >= note.outStart && t < note.outEnd && curve[i] > 0.0 ? 1 : 0;
    }
    const double voicedSeconds = voicedFrames * hop / rate;
    const double expected = (note.voicedEnd - note.voicedStart) / rate * factor;
    INFO("voiced " << voicedSeconds << " expected about " << expected);
    CHECK(std::abs(voicedSeconds - expected) < 0.06);
}

TEST_CASE("whale: linked notes glide; the glide time sets how long")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int a = noteNear(notes, 2.0, rate); // A3 -> D4, linked
    const int b = a + 1;
    REQUIRE(notes[static_cast<std::size_t>(a)].linkedToNext);
    // Flatten both so the only movement is the transition, then move D4 up a fourth.
    for (const int i : {a, b})
    {
        notes[static_cast<std::size_t>(i)].drift = 0.0f;
        notes[static_cast<std::size_t>(i)].vibrato = 0.0f;
    }
    notes[static_cast<std::size_t>(b)].pitch = 5.0f;

    const auto transition = [&](float glideSeconds)
    {
        notes[static_cast<std::size_t>(a)].glide = glideSeconds;
        const auto out = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));
        const auto curve = pitchCurve(out, rate);
        const auto hop = animalaudio::dsp::ProbabilisticYin::analyse(out.data(), 4096, rate).hop;
        const double from = notes[static_cast<std::size_t>(a)].centre;
        const double to = notes[static_cast<std::size_t>(b)].centre + 5.0;
        double t10 = -1.0, t90 = -1.0;
        const double edge = notes[static_cast<std::size_t>(b)].outStart;
        for (std::size_t i = 0; i < curve.size(); ++i)
        {
            const double t = static_cast<double>(i) * hop;
            if (t < edge - 0.4 * rate || t > edge + 0.4 * rate || curve[i] < 0.0)
                continue;
            const double progress = (curve[i] - from) / (to - from);
            if (t10 < 0.0 && progress > 0.1)
                t10 = t;
            if (t90 < 0.0 && progress > 0.9)
                t90 = t;
        }
        // Both ends are reached exactly.
        CHECK(std::abs(measuredMidi(out, (edge - 0.17 * rate) / rate, (edge - 0.12 * rate) / rate, rate) - from) <
              0.05);
        CHECK(std::abs(measuredMidi(out, (edge + 0.15 * rate) / rate, (edge + 0.25 * rate) / rate, rate) - to) < 0.05);
        return (t90 - t10) / rate;
    };
    const double quick = transition(0.02f);
    const double slow = transition(0.25f);
    INFO("10-90% transition: quick " << quick << " s, slow " << slow << " s");
    CHECK(quick < 0.06);
    CHECK(slow > 0.12);
    CHECK(slow < 0.25);
}

TEST_CASE("whale: a shaped glide replaces the slide as sung")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    const double length = static_cast<double>(take.audio.size());
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int a = noteNear(notes, 0.3, rate); // C4 -> E4, sung with a 60 ms legato slide
    const int b = a + 1;
    auto& link = notes[static_cast<std::size_t>(a)];
    REQUIRE(link.linkedToNext);
    const double edge = notes[static_cast<std::size_t>(b)].outStart;
    const float from = link.centre;
    const float to = notes[static_cast<std::size_t>(b)].centre;
    REQUIRE(to - from > 3.5f);

    SECTION("left as sung, glide settings alone change nothing")
    {
        link.glide = 0.3f;
        link.curve = 0.5f;
        CHECK(link.isUnedited());
        CHECK(EditPlan(take.analysis, notes, {}).isNeutral(0.0, length));
    }

    SECTION("a long shaped glide is exactly the designed ramp")
    {
        link.shaped = true;
        link.glide = 0.3f;
        const EditPlan plan(take.analysis, notes, {});
        CHECK_FALSE(link.isUnedited());
        CHECK_FALSE(plan.isNeutral(edge - 1000.0, edge + 1000.0));
        const double half = 0.15 * rate;
        int checked = 0;
        for (double t = edge - half; t <= edge + half; t += 48.0)
        {
            const float out = plan.outputMidiAt(t);
            if (out < 0.0f)
                continue;
            const auto u = static_cast<float>((t - (edge - half)) / (2.0 * half));
            CHECK_THAT(out, WithinAbs(from + (to - from) * glideShape(u, 0.0f), 1.0e-3));
            ++checked;
        }
        CHECK(checked > 250);
        // 100 ms before the boundary the singer has not started moving; the designed glide has.
        CHECK(std::abs(take.analysis.midiAt(edge - 0.1 * rate) - from) < 0.1f);
        CHECK(plan.outputMidiAt(edge - 0.1 * rate) > from + 0.2f);
    }

    SECTION("a short shaped glide removes the sung slide around it")
    {
        link.shaped = true;
        link.glide = 0.01f;
        const EditPlan plan(take.analysis, notes, {});
        bool slideWasThere = false;
        for (double t = edge - 0.08 * rate; t < edge + 0.08 * rate; t += 48.0)
        {
            const float out = plan.outputMidiAt(t);
            if (std::abs(t - edge) < 0.006 * rate || out < 0.0f)
                continue;
            const float held = t < edge ? from : to;
            CHECK_THAT(out, WithinAbs(held, 1.0e-3));
            slideWasThere = slideWasThere || std::abs(take.analysis.midiAt(t) - held) > 0.5f;
        }
        CHECK(slideWasThere);
    }

    SECTION("the curve makes it leave early or late")
    {
        link.shaped = true;
        link.glide = 0.2f;
        link.curve = 0.8f;
        CHECK(EditPlan(take.analysis, notes, {}).outputMidiAt(edge) < from + 0.25f * (to - from));
        link.curve = -0.8f;
        CHECK(EditPlan(take.analysis, notes, {}).outputMidiAt(edge) > from + 0.75f * (to - from));
    }

    SECTION("the notes' own movement comes back smoothly around it")
    {
        link.shaped = true;
        notes[static_cast<std::size_t>(b)].pitch = 3.0f;
        const EditPlan plan(take.analysis, notes, {});
        float previous = -1.0f;
        for (double t = edge - 0.3 * rate; t < edge + 0.3 * rate; t += 12.0)
        {
            const float out = plan.outputMidiAt(t);
            if (out >= 0.0f && previous >= 0.0f)
                CHECK(std::abs(out - previous) < 0.08f);
            previous = out;
        }
        const double later = edge + 0.2 * rate; // E4's vibrato, moved up 3 semitones
        CHECK_THAT(plan.outputMidiAt(later), WithinAbs(take.analysis.midiAt(later) + 3.0f, 1.0e-3));
    }

    SECTION("a reset or an unlink puts the slide back")
    {
        link.shaped = true;
        link.glide = 0.2f;
        edit::resetSound(link);
        CHECK_FALSE(link.shaped);
        CHECK(EditPlan(take.analysis, notes, {}).isNeutral(0.0, length));
        link.shaped = true;
        REQUIRE(edit::setLinked(notes, a, false));
        CHECK(notes[static_cast<std::size_t>(a)].isUnedited());
    }
}

TEST_CASE("whale: a shaped glide is what the render sings")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    auto notes = notesFromAnalysis(take.analysis, 1);
    const int a = noteNear(notes, 0.3, rate);
    const int b = a + 1;
    const double edge = notes[static_cast<std::size_t>(b)].outStart;
    const double from = notes[static_cast<std::size_t>(a)].centre;
    const double to = notes[static_cast<std::size_t>(b)].centre;

    const auto riseSeconds = [&](const std::vector<float>& out)
    {
        const auto curve = pitchCurve(out, rate);
        const auto hop = animalaudio::dsp::ProbabilisticYin::analyse(out.data(), 4096, rate).hop;
        double t10 = -1.0, t90 = -1.0;
        for (std::size_t i = 0; i < curve.size(); ++i)
        {
            const double t = static_cast<double>(i) * hop;
            if (t < edge - 0.3 * rate || t > edge + 0.3 * rate || curve[i] < 0.0)
                continue;
            const double progress = (curve[i] - from) / (to - from);
            if (t10 < 0.0 && progress > 0.1)
                t10 = t;
            if (t90 < 0.0 && progress > 0.9)
                t90 = t;
        }
        return (t90 - t10) / rate;
    };
    const double sung = riseSeconds(take.audio);
    notes[static_cast<std::size_t>(a)].shaped = true;
    notes[static_cast<std::size_t>(a)].glide = 0.3f;
    const auto out = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));
    const double shaped = riseSeconds(out);
    INFO("10-90% rise: sung " << sung << " s, shaped " << shaped << " s (smoothstep over 0.3 s: 0.18 s)");
    CHECK(sung < 0.06);
    CHECK(shaped > 0.14);
    CHECK(shaped < 0.22);
    CHECK(std::abs(measuredMidi(out, (edge - 0.3 * rate) / rate, (edge - 0.2 * rate) / rate, rate) - from) < 0.05);
}

TEST_CASE("whale: rendering part of a take matches the same part of a full render")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    auto notes = notesFromAnalysis(take.analysis, 1);
    for (auto& note : notes)
    {
        note.pitch = 1.5f;
        note.snap = 0.5f;
    }
    edit::moveEdge(notes, noteNear(notes, 1.5, rate), false,
                   notes[static_cast<std::size_t>(noteNear(notes, 1.5, rate))].outEnd + 0.02 * rate,
                   static_cast<double>(take.audio.size()), rate);
    const auto full = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));
    const auto& cuts = take.analysis.cuts;
    const EditPlan plan(take.analysis, notes, {});
    for (std::size_t c = 0; c + 1 < cuts.size(); ++c)
    {
        const auto begin = static_cast<std::int64_t>(std::floor(plan.outputOf(cuts[c])));
        const auto end = static_cast<std::int64_t>(std::ceil(plan.outputOf(cuts[c + 1])));
        const auto part = render(take, notes, begin, end);
        float worst = 0.0f;
        for (std::int64_t i = begin; i < end; ++i)
            worst = std::max(worst,
                             std::abs(part[static_cast<std::size_t>(i - begin)] - full[static_cast<std::size_t>(i)]));
        INFO("segment " << c);
        CHECK(worst < 1.0e-4f);
    }
}

namespace
{
std::vector<float> assemble(const Take& take, const SegmentCache& cache, float mix = 1.0f, int block = 512)
{
    TakePlayback playback;
    playback.rate = take.rate;
    playback.length = static_cast<std::int64_t>(take.audio.size());
    playback.original = std::make_shared<const ChannelData>(ChannelData{take.audio});
    playback.segments = cache.segments();
    FadeTable fades;
    std::vector<float> left(take.audio.size()), right(take.audio.size());
    for (std::int64_t at = 0; at < playback.length; at += block)
    {
        const int n = static_cast<int>(std::min<std::int64_t>(block, playback.length - at));
        float* out[2] = {left.data() + at, right.data() + at};
        readTake(playback, at, n, out, 2, mix, fades);
    }
    for (std::size_t i = 0; i < left.size(); ++i)
        REQUIRE(same(left[i], right[i])); // a mono take plays on both channels
    return left;
}

float worstDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}
} // namespace

TEST_CASE("whale: playback stitches re-rendered segments into the full edit")
{
    const auto& take = phraseAt48k();
    const double rate = take.rate;
    auto notes = notesFromAnalysis(take.analysis, 1);
    SegmentCache cache;

    // Unedited: nothing to render, playback is the recording.
    REQUIRE(cache.update(take.view(), take.analysis, EditPlan(take.analysis, notes, {})));
    CHECK(cache.segments().empty());
    CHECK(worstDifference(assemble(take, cache), take.audio) < 1.0e-7f);

    // Edit the middle word only: one segment renders, and playback equals a full render.
    const int g = noteNear(notes, 1.5, rate);
    notes[static_cast<std::size_t>(g)].pitch = 3.0f;
    REQUIRE(cache.update(take.view(), take.analysis, EditPlan(take.analysis, notes, {})));
    CHECK(cache.renderedLastUpdate() >= 1);
    CHECK(cache.renderedLastUpdate() <= 3);
    CHECK(worstDifference(assemble(take, cache), render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()))) <
          1.0e-4f);

    // Edit everything, including timing: still exact, at any block size.
    for (auto& note : notes)
    {
        note.snap = 1.0f;
        note.vibrato = 0.5f;
    }
    std::vector<bool> lastWord(notes.size(), false);
    for (std::size_t i = static_cast<std::size_t>(g) + 1; i < notes.size(); ++i)
        lastWord[i] = true;
    edit::moveTime(notes, lastWord, 0.3 * rate, static_cast<double>(take.audio.size()), rate);
    REQUIRE(cache.update(take.view(), take.analysis, EditPlan(take.analysis, notes, {})));
    const auto full = render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()));
    CHECK(worstDifference(assemble(take, cache, 1.0f, 1), full) < 1.0e-4f);
    CHECK(worstDifference(assemble(take, cache, 1.0f, 333), full) < 1.0e-4f);
    // Mix 0 is the recording.
    CHECK(worstDifference(assemble(take, cache, 0.0f), take.audio) < 1.0e-7f);

    // Touching one note again re-renders only near it.
    notes[static_cast<std::size_t>(g)].gainDb = -3.0f;
    REQUIRE(cache.update(take.view(), take.analysis, EditPlan(take.analysis, notes, {})));
    CHECK(cache.renderedLastUpdate() <= 3);
    CHECK(worstDifference(assemble(take, cache), render(take, notes, 0, static_cast<std::int64_t>(take.audio.size()))) <
          1.0e-4f);
}

TEST_CASE("whale: capture records across chunks and stops when it runs out")
{
    CaptureBuffer capture;
    CHECK_FALSE(capture.begin(1.0, 48000.0, 2)); // nothing allocated yet
    capture.keepAhead(1);
    REQUIRE(capture.begin(1.5, 48000.0, 2));
    std::vector<float> l(1000), r(1000);
    std::int64_t written = 0;
    for (int block = 0; block < 200; ++block)
    {
        for (int i = 0; i < 1000; ++i)
        {
            l[static_cast<std::size_t>(i)] = static_cast<float>(written + i);
            r[static_cast<std::size_t>(i)] = -static_cast<float>(written + i);
        }
        const float* in[2] = {l.data(), r.data()};
        capture.write(in, 2, 1000);
        written += 1000;
        capture.keepAhead(1);
    }
    capture.finish();
    CaptureBuffer::Take take;
    REQUIRE(capture.collect(take, 2));
    CHECK(capture.state() == CaptureBuffer::State::Idle);
    CHECK_FALSE(take.overflowed);
    CHECK(same(take.startSeconds, 1.5));
    REQUIRE(take.channels.size() == 2);
    REQUIRE(take.channels[0].size() == 200000);
    bool exact = true;
    for (std::size_t i = 0; i < take.channels[0].size(); ++i)
        exact = exact && same(take.channels[0][i], static_cast<double>(i)) &&
                same(take.channels[1][i], -static_cast<double>(i));
    CHECK(exact);

    // Without the worker keeping up, the capture ends at the last chunk instead of writing anywhere else.
    REQUIRE(capture.begin(0.0, 48000.0, 1));
    for (int block = 0; block < 200; ++block)
    {
        const float* in[1] = {l.data()};
        capture.write(in, 1, 1000);
    }
    CHECK(capture.state() == CaptureBuffer::State::Finished);
    REQUIRE(capture.collect(take, 1));
    CHECK(take.overflowed);
    CHECK(take.channels[0].size() == static_cast<std::size_t>(2 * CaptureBuffer::kChunkFrames));
}
