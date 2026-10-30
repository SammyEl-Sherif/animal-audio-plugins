#include "MonkeydyneEditor.h"
#include "MonkeydyneHost.h"

#include <animalaudio/testing/TestKit.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

// Every interaction here is a synthetic event sent to the component; nothing moves the real mouse pointer.

using namespace monkeydyne;
using Catch::Matchers::WithinAbs;
using monkeydyne::testing::Host;
using monkeydyne::testing::kRate;
using monkeydyne::testing::noteAtSeconds;

namespace
{
struct EditorRig
{
    juce::ScopedJuceInitialiser_GUI gui;
    Host host;
    std::unique_ptr<MonkeydyneEditor> editor;

    explicit EditorRig(bool record = true)
    {
        if (record)
            host.recordPhrase();
        editor.reset(static_cast<MonkeydyneEditor*>(host.processor.createEditor()));
        sync();
        canvas().fitAll();
    }

    ~EditorRig() { editor.reset(); }

    ui::EditController& controller() { return editor->getController(); }
    ui::NoteCanvas& canvas() { return editor->getCanvas(); }

    void sync()
    {
        editor->refresh();
        controller().dispatchPendingMessages();
    }

    ui::NoteRef note(int index)
    {
        const auto& take = controller().takes().front();
        return {take.id, take.notes[static_cast<std::size_t>(index)].id};
    }

    int indexNear(double seconds) { return noteAtSeconds(controller().takes().front().notes, seconds); }

    dsp::Note value(const ui::NoteRef& ref)
    {
        const auto* n = controller().note(ref);
        REQUIRE(n != nullptr);
        return *n;
    }

    std::size_t noteCount() { return controller().takes().front().notes.size(); }

    juce::MouseEvent event(juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods, int clicks = 1)
    {
        auto& c = canvas();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks,
                                at != down);
    }

    void drag(juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        const auto held = mods.withFlags(juce::ModifierKeys::leftButtonModifier);
        canvas().mouseDown(event(from, from, held));
        for (int i = 1; i <= 8; ++i)
            canvas().mouseDrag(event(from + (to - from) * (static_cast<float>(i) / 8.0f), from, held));
        canvas().mouseUp(event(to, from, mods));
        sync();
    }

    void click(juce::Point<float> at, juce::ModifierKeys mods = {}, bool doubleClick = false)
    {
        const auto held = mods.withFlags(juce::ModifierKeys::leftButtonModifier);
        canvas().mouseDown(event(at, at, held));
        canvas().mouseUp(event(at, at, mods));
        if (doubleClick)
        {
            canvas().mouseDown(event(at, at, held, 2));
            canvas().mouseUp(event(at, at, mods, 2));
            canvas().mouseDoubleClick(event(at, at, mods, 2));
        }
        sync();
    }

    float pixelsPerSecond()
    {
        return static_cast<float>(canvas().noteArea().getWidth() / canvas().getVisibleSeconds());
    }
};

/** True if `area` has a saturated pixel of `colour`'s hue (blobs are gradients over the dark background). */
bool hasHue(const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour)
{
    for (int y = area.getY(); y < area.getBottom(); y += 2)
        for (int x = area.getX(); x < area.getRight(); x += 2)
        {
            const auto c = image.getPixelAt(x, y);
            const float d = std::abs(c.getHue() - colour.getHue());
            if (std::min(d, 1.0f - d) < 0.04f && c.getSaturation() > 0.35f && c.getBrightness() > 0.35f)
                return true;
        }
    return false;
}
} // namespace

TEST_CASE("monkeydyne: editor renders headlessly")
{
    EditorRig rig;
    auto& c = rig.controller();
    const auto take = c.takes().front();
    REQUIRE(take.notes.size() == monkeydyne::testing::phrase().size());

    // A few edits, so the picture shows the tools at work: "go" moved up a whole tone onto A4, the first legato
    // slide made long and late, the A3 -> D4 -> F4 line snapped and straightened.
    auto notes = take.notes;
    const auto go = static_cast<std::size_t>(rig.indexNear(1.6));
    notes[go].pitch = 2.0f;
    notes[go].snap = 1.0f;
    notes[0].glide = 0.16f;
    notes[0].curve = 0.45f;
    notes[0].shaped = true;
    for (std::size_t i = go + 1; i < notes.size(); ++i)
    {
        notes[i].snap = 1.0f;
        notes[i].drift = 0.3f;
    }
    c.setNotes(take.id, notes);
    c.setSelection({{take.id, notes[go].id}});
    rig.host.settle();
    rig.sync();
    rig.canvas().fitAll();
    rig.canvas().setAmbientTime(4.2);

    const auto image = rig.editor->createComponentSnapshot(rig.editor->getLocalBounds(), true, 2.0f);
    REQUIRE(image.getWidth() == MonkeydyneEditor::kWidth * 2);
    REQUIRE(image.getHeight() == MonkeydyneEditor::kHeight * 2);
    animalaudio::testing::writePngIfRequested(image, "monkeydyne-ui.png");

    const auto palette = ui::makePalette();
    const auto blob = rig.canvas().noteBounds({take.id, notes[go].id}).toNearestInt() + rig.canvas().getPosition();
    REQUIRE_FALSE(blob.isEmpty());
    REQUIRE(hasHue(image, (blob * 2).reduced(4), palette.accentAlt)); // the selected note is gold
    const auto other = rig.canvas().noteBounds(rig.note(4)).toNearestInt() + rig.canvas().getPosition();
    REQUIRE(hasHue(image, (other * 2).reduced(4), palette.accent)); // the others orchid
}

TEST_CASE("monkeydyne: an empty editor says how to start")
{
    EditorRig rig(false);
    REQUIRE(rig.controller().takes().empty());
    const auto image = rig.editor->createComponentSnapshot(rig.editor->getLocalBounds(), true, 1.0f);
    REQUIRE(image.getWidth() == MonkeydyneEditor::kWidth);
    animalaudio::testing::writePngIfRequested(image, "monkeydyne-ui-empty.png");
}

TEST_CASE("monkeydyne: dragging a note up lands it on a higher note; Alt moves it in cents")
{
    EditorRig rig;
    const auto ref = rig.note(rig.indexNear(1.6));
    const auto before = rig.value(ref);
    const auto from = rig.canvas().noteBounds(ref).getCentre();
    const float row = rig.canvas().getRowHeight();

    rig.drag(from, from.translated(0.0f, -2.0f * row - 1.0f));
    auto after = rig.value(ref);
    REQUIRE_THAT(after.pitch, WithinAbs(before.pitch + 2.0f, 1.0e-6));
    REQUIRE_THAT(after.snap, WithinAbs(1.0, 1.0e-6));
    REQUIRE_THAT(rig.controller().plan(ref.take)->targetCentre(after),
                 WithinAbs(std::round(before.centre) + 2.0f, 1.0e-4));
    REQUIRE_THAT(after.outStart, WithinAbs(before.outStart, 1.0e-9)); // a vertical drag never moves the time
    REQUIRE(rig.controller().isSelected(ref));
    REQUIRE(rig.canvas().noteBounds(ref).getCentreY() < from.y - 1.5f * row); // the view stays; the note moves

    REQUIRE(rig.controller().undo());
    REQUIRE_THAT(rig.canvas().noteBounds(ref).getCentreY(), WithinAbs(from.y, 0.5));
    after = rig.value(ref);
    REQUIRE_THAT(after.pitch, WithinAbs(before.pitch, 1.0e-6));
    REQUIRE_FALSE(rig.controller().canUndo());

    rig.drag(from, from.translated(0.0f, -1.5f * row), juce::ModifierKeys::altModifier);
    after = rig.value(ref);
    REQUIRE_THAT(after.pitch, WithinAbs(1.5, 1.0e-4));
    REQUIRE_THAT(after.snap, WithinAbs(0.0, 1.0e-6));

    // A click without a drag changes nothing and leaves no undo step behind.
    REQUIRE(rig.controller().undo());
    rig.click(rig.canvas().noteBounds(ref).getCentre());
    REQUIRE_FALSE(rig.controller().canUndo());
}

TEST_CASE("monkeydyne: dragging sideways moves a note in time; dragging its edge stretches it")
{
    EditorRig rig;
    const auto ref = rig.note(rig.indexNear(1.6));
    const auto before = rig.value(ref);
    const auto bounds = rig.canvas().noteBounds(ref);
    const float pps = rig.pixelsPerSecond();

    rig.drag(bounds.getCentre(), bounds.getCentre().translated(-0.1f * pps, 0.0f));
    auto after = rig.value(ref);
    REQUIRE_THAT(after.outStart, WithinAbs(before.outStart - 0.1 * kRate, 0.003 * kRate));
    REQUIRE_THAT(after.outLength(), WithinAbs(before.outLength(), 1.0));
    REQUIRE_THAT(after.pitch, WithinAbs(before.pitch, 1.0e-6));
    REQUIRE_THAT(after.srcStart, WithinAbs(before.srcStart, 1.0e-9));

    REQUIRE(rig.controller().undo());
    const auto edge = juce::Point<float>(bounds.getX() + 1.0f, bounds.getCentreY());
    rig.drag(edge, edge.translated(-0.08f * pps, 0.0f));
    after = rig.value(ref);
    REQUIRE(after.isStretched());
    REQUIRE_THAT(after.outEnd, WithinAbs(before.outEnd, 1.0));
    REQUIRE_THAT(after.outStart, WithinAbs(before.outStart - 0.08 * kRate, 0.003 * kRate));
}

TEST_CASE("monkeydyne: a click selects the note, its word or its phrase; a band selects what it touches")
{
    EditorRig rig;
    auto& c = rig.controller();
    const auto first = rig.note(0);

    rig.click(rig.canvas().noteBounds(first).getCentre());
    REQUIRE(c.selection() == std::set<ui::NoteRef>{first});

    rig.editor->getGranularityButton(ui::Granularity::Word).onClick();
    rig.click(rig.canvas().noteBounds(first).getCentre());
    REQUIRE(c.selection() == std::set<ui::NoteRef>{rig.note(0), rig.note(1)});

    rig.editor->getGranularityButton(ui::Granularity::Phrase).onClick();
    rig.click(rig.canvas().noteBounds(rig.note(3)).getCentre());
    REQUIRE(c.selection().size() == rig.noteCount() - 2);

    // Shift-click takes a phrase back out, Escape clears.
    rig.click(rig.canvas().noteBounds(rig.note(3)).getCentre(), juce::ModifierKeys::shiftModifier);
    REQUIRE(c.selection().empty());

    rig.editor->getGranularityButton(ui::Granularity::Note).onClick();
    const auto a = rig.canvas().noteBounds(rig.note(3));
    const auto f = rig.canvas().noteBounds(rig.note(5));
    const auto area = rig.canvas().noteArea();
    rig.drag({a.getX() + 2.0f, area.getY() + 4.0f}, {f.getRight() - 2.0f, area.getBottom() - 4.0f});
    REQUIRE(c.selection() == std::set<ui::NoteRef>{rig.note(3), rig.note(4), rig.note(5)});
    REQUIRE(rig.canvas().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    REQUIRE(c.selection().empty());
}

TEST_CASE("monkeydyne: the glide handle sets the glide time and its curve")
{
    EditorRig rig;
    const auto ref = rig.note(0);
    REQUIRE(rig.value(ref).linkedToNext);
    rig.click(rig.canvas().noteBounds(ref).getCentre());
    const auto handle = rig.canvas().glideHandle(ref);
    const float pps = rig.pixelsPerSecond();

    REQUIRE_FALSE(rig.value(ref).shaped);

    rig.drag(handle, handle.translated(0.05f * pps, -40.0f));
    const auto after = rig.value(ref);
    REQUIRE(after.shaped);
    REQUIRE_THAT(after.glide, WithinAbs(dsp::kDefaultGlide + 0.1, 0.004));
    REQUIRE_THAT(after.curve, WithinAbs(0.5, 0.01));
    REQUIRE_THAT(after.pitch, WithinAbs(0.0, 1.0e-6)); // the note itself stays put

    // The drawn pitch follows the designed glide: at the boundary a late curve is still near the first note.
    const auto* plan = rig.controller().plan(ref.take);
    REQUIRE(plan != nullptr);
    const auto& next = rig.value(rig.note(1));
    REQUIRE(plan->outputMidiAt(next.outStart) < after.centre + 0.3f * (next.centre - after.centre));

    rig.click(rig.canvas().glideHandle(ref), {}, true);
    const auto sung = rig.value(ref);
    REQUIRE_FALSE(sung.shaped);
    REQUIRE_THAT(sung.glide, WithinAbs(dsp::kDefaultGlide, 1.0e-6));
    REQUIRE_THAT(sung.curve, WithinAbs(0.0, 1.0e-6));
    REQUIRE(rig.controller().canUndo());
}

TEST_CASE("monkeydyne: the cut tool splits a note, Alt-click unlinks, a double-click merges")
{
    EditorRig rig;
    animalaudio::testing::click(rig.editor->getToolButton(ui::Tool::Cut));
    REQUIRE(rig.canvas().getTool() == ui::Tool::Cut);
    REQUIRE(rig.editor->getToolButton(ui::Tool::Cut).getToggleState());

    const auto count = rig.noteCount();
    const auto ref = rig.note(3);
    const auto bounds = rig.canvas().noteBounds(ref);
    rig.click(bounds.getCentre());
    REQUIRE(rig.noteCount() == count + 1);
    const auto left = rig.value(ref);
    REQUIRE(left.linkedToNext);
    REQUIRE(rig.controller().selection().size() == 2);

    const auto separation = juce::Point<float>(rig.canvas().noteBounds(ref).getRight(), bounds.getCentreY());
    rig.click(separation, juce::ModifierKeys::altModifier);
    REQUIRE_FALSE(rig.value(ref).linkedToNext);

    rig.click(separation, {}, true);
    REQUIRE(rig.noteCount() == count);
    REQUIRE_THAT(rig.value(ref).outEnd, WithinAbs(rig.controller().takes().front().notes[4].outStart, 1.0));

    REQUIRE(rig.canvas().keyPressed(juce::KeyPress('1', {}, '1')));
    REQUIRE(rig.canvas().getTool() == ui::Tool::Edit);
    REQUIRE(rig.editor->getToolButton(ui::Tool::Edit).getToggleState());
}

TEST_CASE("monkeydyne: inspector knobs act on the selection, or on every note")
{
    EditorRig rig;
    auto& c = rig.controller();
    auto& inspector = rig.editor->getInspector();

    inspector.getKnob(ui::Inspector::snapKnob).setValue(1.0, juce::sendNotificationSync);
    for (const auto& note : c.takes().front().notes)
        REQUIRE_THAT(note.snap, WithinAbs(1.0, 1.0e-6));
    REQUIRE(c.undo());
    REQUIRE_FALSE(c.canUndo()); // one knob change, one undo step, whatever it touched
    rig.sync();

    c.setSelection({rig.note(2)});
    rig.sync();
    inspector.getKnob(ui::Inspector::pitchKnob).setValue(3.0, juce::sendNotificationSync);
    REQUIRE_THAT(rig.value(rig.note(2)).pitch, WithinAbs(3.0, 1.0e-6));
    REQUIRE_THAT(rig.value(rig.note(3)).pitch, WithinAbs(0.0, 1.0e-6));

    // Relative: each selected note keeps its own offset.
    c.setSelection({rig.note(2), rig.note(3)});
    rig.sync();
    REQUIRE_THAT(inspector.getKnob(ui::Inspector::pitchKnob).getValue(), WithinAbs(3.0, 1.0e-6));
    inspector.getKnob(ui::Inspector::pitchKnob).setValue(4.0, juce::sendNotificationSync);
    REQUIRE_THAT(rig.value(rig.note(2)).pitch, WithinAbs(4.0, 1.0e-6));
    REQUIRE_THAT(rig.value(rig.note(3)).pitch, WithinAbs(1.0, 1.0e-6));

    inspector.getKnob(ui::Inspector::straightenKnob).setValue(0.75, juce::sendNotificationSync);
    REQUIRE_THAT(rig.value(rig.note(3)).drift, WithinAbs(0.25, 1.0e-6));
    inspector.getKnob(ui::Inspector::formantKnob).setValue(-2.0, juce::sendNotificationSync);
    REQUIRE_THAT(rig.value(rig.note(2)).formant, WithinAbs(-2.0, 1.0e-6));

    animalaudio::testing::click(inspector.getModeButton(static_cast<int>(dsp::Scale::Mode::Minor)));
    REQUIRE(c.scale().mode == dsp::Scale::Mode::Minor);
    animalaudio::testing::click(inspector.getKeyUpButton());
    REQUIRE(c.scale().key == 1);
}

TEST_CASE("monkeydyne: keys nudge, snap, mute and undo")
{
    EditorRig rig;
    const auto ref = rig.note(2);
    rig.click(rig.canvas().noteBounds(ref).getCentre());
    auto& canvas = rig.canvas();

    REQUIRE(canvas.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    REQUIRE_THAT(rig.value(ref).pitch, WithinAbs(1.0, 1.0e-6));
    REQUIRE(canvas.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::altModifier, 0)));
    REQUIRE_THAT(rig.value(ref).pitch, WithinAbs(0.9, 1.0e-5));
    REQUIRE(canvas.keyPressed(juce::KeyPress('s', {}, 's')));
    REQUIRE_THAT(rig.value(ref).snap, WithinAbs(1.0, 1.0e-6));
    REQUIRE(canvas.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    REQUIRE(rig.value(ref).muted);
    REQUIRE(canvas.keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    REQUIRE_FALSE(rig.value(ref).muted);
    REQUIRE(canvas.keyPressed(
        juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 'z')));
    REQUIRE(rig.value(ref).muted);
    REQUIRE_FALSE(canvas.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))); // the host's transport key
}

TEST_CASE("monkeydyne: transfer arms from the button; clear asks first")
{
    EditorRig rig;
    animalaudio::testing::click(rig.editor->getTransferButton());
    REQUIRE(rig.host.processor.isTransferArmed());
    animalaudio::testing::click(rig.editor->getTransferButton());
    REQUIRE_FALSE(rig.host.processor.isTransferArmed());

    animalaudio::testing::click(rig.editor->getClearButton());
    rig.sync();
    REQUIRE(rig.controller().takes().size() == 1);
    REQUIRE(rig.editor->getClearButton().getButtonText() == "SURE?");
    animalaudio::testing::click(rig.editor->getClearButton());
    rig.sync();
    REQUIRE(rig.controller().takes().empty());
}

TEST_CASE("monkeydyne: the editor lays out at any size")
{
    EditorRig rig;
    for (const auto& size : {juce::Point<int>(980, 620), juce::Point<int>(1180, 720), juce::Point<int>(1900, 1100)})
    {
        rig.editor->setSize(size.x, size.y);
        const auto bounds = rig.editor->getLocalBounds();
        INFO(size.x << "x" << size.y);
        REQUIRE(bounds.contains(rig.canvas().getBounds()));
        REQUIRE(bounds.contains(rig.editor->getInspector().getBounds()));
        REQUIRE(rig.canvas().getHeight() > 250);
        REQUIRE(rig.editor->getInspector().getBottom() <= size.y - 20);
        REQUIRE(rig.editor->getClearButton().getX() > rig.editor->getRedoButton().getRight());
    }
}
