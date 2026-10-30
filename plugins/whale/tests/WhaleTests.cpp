#include "WhaleHost.h"
#include "WhaleTestSupport.h"
#include "engine/StateCodec.h"

#include <animalaudio/testing/VoiceSynth.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace whale;
using Catch::Matchers::WithinAbs;
using whale::testing::Host;
using whale::testing::kBlock;
using whale::testing::kPhraseSeconds;
using whale::testing::kRate;
using whale::testing::kTakeStart;
using whale::testing::measuredMidi;
using whale::testing::noteAtSeconds;
using whale::testing::phrase;

namespace
{
double maxDifference(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0)
{
    double worst = 0.0;
    for (std::size_t i = from; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    return worst;
}

std::vector<float> recording(const TakeInfo& take)
{
    return take.audio->channels[0];
}
} // namespace

TEST_CASE("whale: input passes through until something is recorded")
{
    Host host;
    const auto voice = animalaudio::testing::sing(phrase(), 1.0, kRate);
    REQUIRE(maxDifference(host.play(voice, 0), voice) < 1.0e-9);
    REQUIRE(maxDifference(host.play(voice, 0, false), voice) < 1.0e-9);
    REQUIRE(host.processor.getSession().takes().empty());
}

TEST_CASE("whale: transfer records a take in place and plays it back over the input")
{
    Host host;
    const auto voice = animalaudio::testing::sing(phrase(), kPhraseSeconds, kRate);
    const auto heard = host.recordPhrase();
    REQUIRE(maxDifference(heard, voice) < 1.0e-9); // the performance passes through while it is recorded

    const auto take = host.onlyTake();
    REQUIRE(take.status == TakeStatus::Ready);
    REQUIRE_THAT(take.startSeconds, WithinAbs(1.0, 1.0e-9));
    REQUIRE(take.length() == static_cast<std::int64_t>(voice.size()));
    REQUIRE(take.notes.size() == phrase().size());
    REQUIRE(maxDifference(recording(take), voice) < 1.0e-6); // 24-bit grid

    // Over the take the recording replaces the input; elsewhere the input passes.
    REQUIRE(maxDifference(host.playback(), recording(take)) < 1.0e-6);
    const std::vector<float> other(4096, 0.25f);
    REQUIRE(maxDifference(host.play(other, kTakeStart - 4096), other) < 1.0e-9);
    REQUIRE(maxDifference(host.play(other, kTakeStart + take.length()), other) < 1.0e-9);
}

TEST_CASE("whale: an edit plays back; compare and undo bring back the recording")
{
    Host host;
    host.recordPhrase();
    const auto original = recording(host.onlyTake());

    host.editNotes(
        [](std::vector<dsp::Note>& notes)
        {
            const int g4 = noteAtSeconds(notes, 1.6);
            REQUIRE(g4 >= 0);
            notes[static_cast<std::size_t>(g4)].pitch = 2.0f;
        });
    host.settle();
    const auto edited = host.playback();
    REQUIRE_THAT(measuredMidi(edited, 1.48, 1.70, kRate), WithinAbs(69.2, 0.1));
    REQUIRE_THAT(measuredMidi(edited, 1.88, 2.20, kRate), WithinAbs(57.0, 0.1));
    REQUIRE(maxDifference(edited, original) > 0.05);

    host.harness.setParam(ids::compare, 1.0f);
    host.playback();
    REQUIRE(maxDifference(host.playback(), original) < 1.0e-5);

    host.harness.setParam(ids::compare, 0.0f);
    host.harness.setParam(ids::mix, 0.5f);
    host.playback();
    const auto half = host.playback();
    std::vector<float> expected(original.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        expected[i] = 0.5f * (original[i] + edited[i]);
    REQUIRE(maxDifference(half, expected) < 1.0e-4);

    host.harness.setParam(ids::mix, 1.0f);
    REQUIRE(host.processor.getSession().undo());
    host.settle();
    host.playback();
    REQUIRE(maxDifference(host.playback(), original) < 1.0e-6);
    // A re-render may cover a different stretch than the first one: equal to well under -80 dB (Renderer.h).
    REQUIRE(host.processor.getSession().redo());
    host.settle();
    REQUIRE(maxDifference(host.playback(), edited) < 1.0e-4);
}

TEST_CASE("whale: playback is the same at any block size")
{
    Host host;
    host.recordPhrase();
    host.editNotes([](std::vector<dsp::Note>& notes) { notes[1].pitch = -3.0f; });
    host.settle();
    const auto reference = host.playback();
    for (const int block : {1, 37, 2048})
    {
        INFO("block " << block);
        const auto out = host.play(std::vector<float>(static_cast<std::size_t>(kPhraseSeconds * kRate), 0.0f),
                                   kTakeStart, true, block);
        REQUIRE(maxDifference(out, reference) < 1.0e-6);
    }
}

TEST_CASE("whale: a jump or a stop ends the transfer; a new take replaces the one it overlaps")
{
    Host host;
    const auto voice = animalaudio::testing::sing(phrase(), kPhraseSeconds, kRate);
    host.processor.setTransferArmed(true);
    host.processor.getWorker().runOnce();
    const std::vector<float> first(voice.begin(), voice.begin() + 48000);
    host.play(first, 0);
    REQUIRE(host.processor.isCapturing());
    host.play(first, 480000); // the playhead jumped ten seconds
    REQUIRE_FALSE(host.processor.isCapturing());
    REQUIRE_FALSE(host.processor.isTransferArmed());
    host.settle();
    auto takes = host.processor.getSession().takes();
    REQUIRE(takes.size() == 1);
    REQUIRE_THAT(takes[0].startSeconds, WithinAbs(0.0, 1.0e-9));
    REQUIRE(takes[0].length() == 48000);

    host.processor.setTransferArmed(true);
    host.processor.getWorker().runOnce();
    host.play(voice, 24000);
    host.stop(0);
    host.settle();
    takes = host.processor.getSession().takes();
    REQUIRE(takes.size() == 1);
    REQUIRE_THAT(takes[0].startSeconds, WithinAbs(0.5, 1.0e-9));
}

TEST_CASE("whale: clicking a note with the transport stopped auditions it")
{
    Host host;
    host.recordPhrase();
    const auto take = host.onlyTake();
    const auto& note = take.notes[2];
    host.processor.audition(take.id, note.outStart, 0.2);

    const std::vector<float> silence(static_cast<std::size_t>(0.3 * kRate), 0.0f);
    const auto out = host.play(silence, 0, false);
    const auto original = recording(take);
    const auto from = static_cast<std::size_t>(note.outStart);
    const auto fade = static_cast<std::size_t>(0.005 * kRate);
    double worst = 0.0;
    for (std::size_t i = fade; i < static_cast<std::size_t>(0.19 * kRate); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(out[i] - original[from + i])));
    REQUIRE(worst < 1.0e-6);
    for (std::size_t i = static_cast<std::size_t>(0.2 * kRate); i < out.size(); ++i)
        REQUIRE(std::abs(out[i]) < 1.0e-9);
    std::uint32_t takeId = 0;
    REQUIRE(host.processor.auditionPosition(takeId) < 0.0);

    // Starting the transport stops an audition.
    host.processor.audition(take.id, note.outStart, 2.0);
    host.play(std::vector<float>(512, 0.0f), 0, false);
    REQUIRE(host.processor.auditionPosition(takeId) > 0.0);
    host.play(std::vector<float>(512, 0.0f), 0, true);
    REQUIRE(host.processor.auditionPosition(takeId) < 0.0);
}

TEST_CASE("whale: an offline render waits for the edit")
{
    Host host;
    host.recordPhrase();
    host.processor.setNonRealtime(true);
    host.editNotes(
        [](std::vector<dsp::Note>& notes)
        {
            for (auto& note : notes)
                note.pitch = 5.0f;
        });
    const auto out = host.playback();
    REQUIRE_THAT(measuredMidi(out, 0.20, 0.50, kRate), WithinAbs(65.0, 0.1));
    REQUIRE_THAT(measuredMidi(out, 1.88, 2.20, kRate), WithinAbs(62.0, 0.1));
}

TEST_CASE("whale: state keeps the takes bit for bit, the edits and the parameters")
{
    Host host;
    host.recordPhrase();
    host.editNotes(
        [](std::vector<dsp::Note>& notes)
        {
            notes[0].pitch = 1.0f;
            notes[0].snap = 0.5f;
            notes[2].formant = -2.0f;
            notes[2].gainDb = 3.0f;
            notes[3].muted = true;
            notes[1].glide = 0.2f;
            notes[1].curve = -0.4f;
            notes[1].shaped = true;
        });
    host.processor.getSession().setScale({2, dsp::Scale::Mode::Minor});
    host.harness.setParam(ids::mix, 0.8f);
    host.settle();
    const auto before = host.onlyTake();
    const auto heard = host.playback();

    juce::MemoryBlock state;
    host.processor.getStateInformation(state);
    REQUIRE(state.getSize() < before.audio->channels[0].size() * 3); // FLAC, not raw floats

    Host loaded;
    loaded.processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    loaded.settle();
    const auto after = loaded.onlyTake();
    REQUIRE(after.status == TakeStatus::Ready);
    REQUIRE_THAT(after.startSeconds, WithinAbs(before.startSeconds, 1.0e-12));
    REQUIRE(after.audio->channels == before.audio->channels);
    REQUIRE(after.notes.size() == before.notes.size());
    for (std::size_t i = 0; i < after.notes.size(); ++i)
    {
        const auto& a = after.notes[i];
        const auto& b = before.notes[i];
        REQUIRE(a.id == b.id);
        REQUIRE_THAT(a.outStart, WithinAbs(b.outStart, 1.0e-9));
        REQUIRE_THAT(a.outEnd, WithinAbs(b.outEnd, 1.0e-9));
        REQUIRE_THAT(a.pitch, WithinAbs(b.pitch, 1.0e-6));
        REQUIRE_THAT(a.snap, WithinAbs(b.snap, 1.0e-6));
        REQUIRE_THAT(a.formant, WithinAbs(b.formant, 1.0e-6));
        REQUIRE_THAT(a.gainDb, WithinAbs(b.gainDb, 1.0e-6));
        REQUIRE_THAT(a.glide, WithinAbs(b.glide, 1.0e-6));
        REQUIRE_THAT(a.curve, WithinAbs(b.curve, 1.0e-6));
        REQUIRE(a.shaped == b.shaped);
        REQUIRE(a.muted == b.muted);
        REQUIRE(a.linkedToNext == b.linkedToNext);
    }
    REQUIRE(loaded.processor.getSession().scale().key == 2);
    REQUIRE(loaded.processor.getSession().scale().mode == dsp::Scale::Mode::Minor);
    REQUIRE_THAT(loaded.processor.apvts.getRawParameterValue(ids::mix.getParamID())->load(), WithinAbs(0.8, 1.0e-3));
    loaded.playback();
    REQUIRE(maxDifference(loaded.playback(), heard) < 1.0e-4);

    // New notes made after a reload never reuse a restored id.
    const auto id = loaded.processor.getSession().newNoteId();
    for (const auto& note : after.notes)
        REQUIRE(note.id != id);
}

TEST_CASE("whale: empty and foreign state")
{
    Host host;
    juce::MemoryBlock state;
    host.processor.getStateInformation(state);
    Host loaded;
    loaded.processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(loaded.processor.getSession().takes().empty());

    // A parameter-only XML (as copyXmlToBinary writes) still sets the parameters.
    auto tree = host.processor.apvts.copyState();
    tree.getChildWithProperty("id", ids::mix.getParamID()).setProperty("value", 0.3f, nullptr);
    juce::MemoryBlock xml;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), xml);
    loaded.processor.setStateInformation(xml.getData(), static_cast<int>(xml.getSize()));
    REQUIRE_THAT(loaded.processor.apvts.getRawParameterValue(ids::mix.getParamID())->load(), WithinAbs(0.3, 1.0e-3));

    const char junk[] = "WHL1 but not really";
    loaded.processor.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    REQUIRE(loaded.processor.getSession().takes().empty());
}
