#include "ParrotEditor.h"

#include <animalaudio/core/BuildStamp.h>
#include <animalaudio/core/TextFormat.h>
#include <animalaudio/ui/Drawing.h>
#include <animalaudio/ui/Panels.h>

#include <algorithm>
#include <cmath>

namespace parrot
{
namespace
{
using animalaudio::core::formatCents;
using animalaudio::core::joinWithDots;
using animalaudio::ui::drawArrowGlyph;
using animalaudio::ui::makeFont;

constexpr float kHeaderH = 68.0f;
const juce::Rectangle<float> kPitchPanel{20.0f, 84.0f, 236.0f, 276.0f};
const juce::Rectangle<float> kDisplay{272.0f, 84.0f, 396.0f, 276.0f};
const juce::Rectangle<float> kFormantPanel{684.0f, 84.0f, 236.0f, 276.0f};
const juce::Rectangle<float> kModePanel{20.0f, 376.0f, 400.0f, 152.0f};
const juce::Rectangle<float> kDrivePanel{436.0f, 376.0f, 236.0f, 152.0f};
const juce::Rectangle<float> kMixPanel{688.0f, 376.0f, 232.0f, 152.0f};
constexpr float kRollSpan = 24.0f; // semitones visible in the pitch roll
constexpr int kRollPoints = 400;   // trace points across the roll

juce::String noteText(int midiNote)
{
    return juce::String(dsp::noteName(midiNote));
}

juce::String intervalName(float semitones)
{
    static const char* const names[] = {"UNISON",      "MINOR 2ND", "MAJOR 2ND",   "MINOR 3RD", "MAJOR 3RD",
                                        "PERFECT 4TH", "TRITONE",   "PERFECT 5TH", "MINOR 6TH", "MAJOR 6TH",
                                        "MINOR 7TH",   "MAJOR 7TH", "OCTAVE"};
    const int whole = std::clamp(juce::roundToInt(semitones), -12, 12);
    juce::String name = names[std::abs(whole)];
    if (whole != 0)
        name << (whole > 0 ? " UP" : " DOWN");
    const int cents = juce::roundToInt((semitones - static_cast<float>(whole)) * 100.0f);
    if (cents != 0)
        name << "  " << formatCents(cents);
    return name;
}

juce::String modeBlurb(dsp::Mode mode)
{
    switch (mode)
    {
    case dsp::Mode::Snap:
        return "Pulls each note onto the semitone, then shifts it.";
    case dsp::Mode::Robot:
        return "One steady note: NOTE, or the MIDI key held.";
    case dsp::Mode::Shift:
        break;
    }
    return "Moves the voice by PITCH. Formants stay put.";
}
} // namespace

ParrotEditor::ParrotEditor(ParrotProcessor& plugin)
    : juce::AudioProcessorEditor(plugin)
    , owner(plugin)
    , theme(ui::makeTheme())
    , brand(theme.palette)
    , pitchKnob(theme, theme.palette.accent, true)
    , formantKnob(theme, theme.palette.detail, true)
    , glideKnob(theme, theme.palette.textDim, false)
    , driveKnob(theme, theme.palette.accentAlt, false)
    , mixKnob(theme, theme.palette.text, false)
    , fineButton("FINE", theme.palette)
    , linkButton("LINK", theme.palette)
    , noteDown({}, theme.palette)
    , noteUp({}, theme.palette)
{
    setOpaque(true);
    brand.setTooltip(animalaudio::core::buildStampLong(theme.name));

    attach(pitchKnob, ids::pitch, "Transposes the voice up to an octave either way. Double-click resets.");
    attach(formantKnob, ids::formant,
           "Moves the formants (the vocal tract's resonances) without changing the note: down sounds bigger, "
           "up sounds smaller.");
    attach(glideKnob, ids::glide, "How quickly SNAP and ROBOT slide to a new note. 0 is the hard robotic jump.");
    attach(driveKnob, ids::drive, "Two-stage 12AX7 tube saturation after the shift. Level stays matched.");
    attach(mixKnob, ids::mix, "Dry / wet. The dry signal is delayed to line up with the shifted one.");
    attach(fineButton, ids::fine, "Off: PITCH and FORMANT move in whole semitones. On: in cents.");
    attach(linkButton, ids::link, "Formants follow PITCH, like a tape or sampler: chipmunk up, giant down.");

    auto& modeChoice = *dynamic_cast<juce::AudioParameterChoice*>(owner.apvts.getParameter(ids::mode.getParamID()));
    modePills = std::make_unique<animalaudio::ui::ChoicePillRow>(
        modeChoice, theme.palette, "SHIFT transposes, SNAP pulls to semitones, ROBOT holds one note (NOTE or MIDI).");
    for (int i = 0; i < modePills->getNumPills(); ++i)
        modePills->getPill(i).setButtonText(modeChoice.choices[i].toUpperCase());
    addAndMakeVisible(*modePills);

    noteDown.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    { drawArrowGlyph(g, r, c, false); };
    noteUp.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { drawArrowGlyph(g, r, c, true); };
    noteDown.setTooltip("Robot note down a semitone");
    noteUp.setTooltip("Robot note up a semitone");
    noteDown.onClick = [this] { stepNote(-1); };
    noteUp.onClick = [this] { stepNote(1); };
    addAndMakeVisible(noteDown);
    addAndMakeVisible(noteUp);
    addAndMakeVisible(brand);

    setSize(kWidth, kHeight);
    refresh();
    startTimerHz(30);
}

ParrotEditor::~ParrotEditor()
{
    stopTimer();
}

void ParrotEditor::attach(juce::Slider& slider, const juce::ParameterID& id, const juce::String& tooltip)
{
    auto* param = owner.apvts.getParameter(id.getParamID());
    slider.setTooltip(tooltip);
    slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(*param, slider));
    addAndMakeVisible(slider);
}

void ParrotEditor::attach(juce::Button& button, const juce::ParameterID& id, const juce::String& tooltip)
{
    button.setClickingTogglesState(true);
    button.setTooltip(tooltip);
    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(*owner.apvts.getParameter(id.getParamID()), button));
    addAndMakeVisible(button);
}

float ParrotEditor::value(const juce::ParameterID& id) const
{
    const auto* param = owner.apvts.getParameter(id.getParamID());
    return param->convertFrom0to1(param->getValue());
}

void ParrotEditor::stepNote(int delta)
{
    auto* param = owner.apvts.getParameter(ids::note.getParamID());
    const int next = std::clamp(juce::roundToInt(value(ids::note)) + delta, 0, dsp::kNumNotes - 1);
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(next)));
    param->endChangeGesture();
    repaint(kModePanel.getSmallestIntegerContainer());
}

void ParrotEditor::refresh()
{
    const auto& engine = owner.getEngine();
    const int count = std::min(engine.getTraceCount(), dsp::ParrotEngine::kTraceSize);
    const float in = count > 0 ? engine.traceInput(0) : -1.0f;
    const float out = count > 0 ? engine.traceOutput(0) : -1.0f;
    if (in >= 0.0f && out >= 0.0f)
    {
        shownInput = in;
        shownOutput = out;
        silentFrames = 0;
    }
    else
    {
        ++silentFrames;
    }

    float low = 1.0e9f;
    float high = -1.0e9f;
    for (int age = 0; age < std::min(count, kRollPoints); age += 3)
        for (const float v : {engine.traceInput(age), engine.traceOutput(age)})
            if (v >= 0.0f)
            {
                low = std::min(low, v);
                high = std::max(high, v);
            }
    if (low <= high)
    {
        float target = 0.5f * (low + high);
        if (high - low > kRollSpan - 3.0f && shownOutput >= 0.0f)
            target = shownOutput;
        if (std::abs(target - viewCentre) > kRollSpan * 0.5f)
            viewCentre = target;
        else
            viewCentre += (target - viewCentre) * 0.08f;
        viewCentre = std::clamp(viewCentre, 24.0f + kRollSpan * 0.5f, 108.0f - kRollSpan * 0.5f);
    }
    shownHeat += (engine.getHeat() - shownHeat) * 0.35f;

    juce::int64 state = engine.getHeldNote();
    for (const auto* id : {&ids::pitch, &ids::formant, &ids::fine, &ids::link, &ids::mode, &ids::note, &ids::glide,
                           &ids::drive, &ids::mix})
        state = state * 1000003 + juce::roundToInt(value(*id) * 1000.0f);
    if (state != shownState)
    {
        shownState = state;
        const auto mode = owner.getMode();
        glideKnob.setAlpha(mode == dsp::Mode::Shift ? 0.45f : 1.0f);
        noteDown.setAlpha(mode == dsp::Mode::Robot ? 1.0f : 0.45f);
        noteUp.setAlpha(mode == dsp::Mode::Robot ? 1.0f : 0.45f);
        repaint();
        return;
    }
    repaint(kDisplay.expanded(3.0f).getSmallestIntegerContainer());
    repaint(kDrivePanel.getSmallestIntegerContainer());
}

void ParrotEditor::paintHeader(juce::Graphics& g) const
{
    animalaudio::ui::drawTitleBar(g, getLocalBounds().toFloat().removeFromTop(kHeaderH), theme);
}

void ParrotEditor::paintPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                              juce::Colour key) const
{
    animalaudio::ui::drawPanel(g, area, title, key, theme.palette);
}

void ParrotEditor::paintKnobPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                                  juce::Colour key, const juce::String& readout, const juce::String& detail) const
{
    const auto& palette = theme.palette;
    paintPanel(g, area, title, key);
    g.setColour(palette.text);
    g.setFont(makeFont(24.0f, true, 0.02f));
    g.drawText(readout, juce::Rectangle<float>(area.getX(), area.getY() + 186.0f, area.getWidth(), 28.0f),
               juce::Justification::centred, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.16f));
    g.drawText(detail, juce::Rectangle<float>(area.getX() + 8.0f, area.getY() + 214.0f, area.getWidth() - 16.0f, 14.0f),
               juce::Justification::centred, false);
}

void ParrotEditor::paintDisplay(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    const auto& engine = owner.getEngine();
    animalaudio::ui::drawScreen(g, area, theme);

    // Readout: detected note -> sung note, and the formant shift.
    const bool live = silentFrames < 12;
    const float top = area.getY() + 14.0f;
    auto noteBlock = [&](float x, const juce::String& label, float midi, juce::Colour colour)
    {
        g.setColour(palette.textDim);
        g.setFont(makeFont(9.5f, true, 0.22f));
        g.drawText(label, juce::Rectangle<float>(x, top, 120.0f, 12.0f), juce::Justification::centredLeft, false);
        const auto big = makeFont(28.0f, true, 0.02f);
        const auto line = juce::Rectangle<float>(x - 1.0f, top + 13.0f, 130.0f, 32.0f);
        g.setFont(big);
        if (midi < 0.0f)
        {
            g.setColour(palette.textDim.withAlpha(0.45f));
            g.drawText("--", line, juce::Justification::centredLeft, false);
            return;
        }
        const int note = juce::roundToInt(midi);
        const auto name = noteText(note);
        g.setColour(live ? colour : colour.withAlpha(0.4f));
        g.drawText(name, line, juce::Justification::centredLeft, false);
        g.setColour(palette.textDim.withAlpha(live ? 1.0f : 0.4f));
        g.setFont(makeFont(11.0f, true, 0.04f));
        g.drawText(formatCents(juce::roundToInt((midi - static_cast<float>(note)) * 100.0f)),
                   line.withTrimmedLeft(juce::GlyphArrangement::getStringWidth(big, name) + 6.0f).withTrimmedTop(6.0f),
                   juce::Justification::centredLeft, false);
    };
    const float left = area.getX() + 18.0f;
    noteBlock(left, "INPUT", shownInput, palette.text);
    drawArrowGlyph(g, juce::Rectangle<float>(left + 122.0f, top + 22.0f, 14.0f, 14.0f), palette.textDim.withAlpha(0.6f),
                   true);
    noteBlock(left + 152.0f, "OUTPUT", shownOutput, palette.accent);

    const bool fine = value(ids::fine) > 0.5f;
    const bool link = value(ids::link) > 0.5f;
    const float pitch = dsp::appliedPitch(value(ids::pitch), fine);
    const float formant = dsp::appliedFormant(value(ids::pitch), value(ids::formant), fine, link);
    const auto chip = juce::Rectangle<float>(area.getRight() - 118.0f, top, 100.0f, 45.0f);
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.22f));
    g.drawText(link ? "FORMANT  LINKED" : "FORMANT", chip.withHeight(12.0f), juce::Justification::centredRight, false);
    g.setColour(palette.detail);
    g.setFont(makeFont(18.0f, true, 0.02f));
    g.drawText(formatSemitones(formant), chip.withTrimmedTop(16.0f).withHeight(24.0f),
               juce::Justification::centredRight, false);

    // Pitch roll: semitone rows (sharps shaded, C labelled), input in grey, output in scarlet.
    const auto roll = juce::Rectangle<float>(area.getX() + 18.0f, area.getY() + 70.0f, area.getWidth() - 36.0f,
                                             area.getHeight() - 104.0f);
    const float rowH = roll.getHeight() / kRollSpan;
    const auto yOf = [&](float midi) { return roll.getCentreY() - (midi - viewCentre) * rowH; };
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(roll.getSmallestIntegerContainer());
        g.setColour(palette.background.withAlpha(0.55f));
        g.fillRect(roll);
        const int lowest = static_cast<int>(std::floor(viewCentre - kRollSpan * 0.5f)) - 1;
        const int highest = static_cast<int>(std::ceil(viewCentre + kRollSpan * 0.5f)) + 1;
        animalaudio::ui::drawSemitoneRows(g, roll, yOf, lowest, highest, rowH, palette, true);

        if (owner.getMode() == dsp::Mode::Robot)
        {
            const int held = engine.getHeldNote();
            const float target =
                static_cast<float>(held >= 0 ? held : dsp::kLowestNote + juce::roundToInt(value(ids::note))) + pitch;
            const float y = yOf(target);
            const float dashes[] = {5.0f, 4.0f};
            g.setColour(palette.accentAlt.withAlpha(0.7f));
            g.drawDashedLine(juce::Line<float>(roll.getX(), y, roll.getRight(), y), dashes, 2, 1.2f);
        }

        const int points = std::min({engine.getTraceCount(), dsp::ParrotEngine::kTraceSize, kRollPoints});
        auto trace = [&](bool output)
        {
            juce::Path path;
            bool pen = false;
            float last = 0.0f;
            for (int age = 0; age < points; ++age)
            {
                const float v = output ? engine.traceOutput(age) : engine.traceInput(age);
                if (v < 0.0f || (pen && std::abs(v - last) > 3.0f))
                    pen = false;
                if (v < 0.0f)
                    continue;
                const juce::Point<float> p(roll.getRight() - static_cast<float>(age) * roll.getWidth() /
                                                                 static_cast<float>(kRollPoints - 1),
                                           yOf(v));
                if (pen)
                    path.lineTo(p);
                else
                    path.startNewSubPath(p);
                pen = true;
                last = v;
            }
            return path;
        };
        const auto stroke = [](float width)
        { return juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
        g.setColour(palette.textDim.withAlpha(0.55f));
        g.strokePath(trace(false), stroke(1.4f));
        const auto out = trace(true);
        g.setColour(palette.accent.withAlpha(0.2f));
        g.strokePath(out, stroke(6.0f));
        g.setColour(palette.accent);
        g.strokePath(out, stroke(2.0f));

        if (points > 0 && engine.traceOutput(0) >= 0.0f)
        {
            const auto head = juce::Point<float>(roll.getRight() - 1.0f, yOf(engine.traceOutput(0)));
            g.setColour(palette.ambient);
            g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(head));
            g.setColour(palette.accentAlt);
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(head));
        }
    }
    g.setColour(palette.outline.withAlpha(0.6f));
    g.drawRect(roll, 1.0f);

    // Status line
    const auto mode = owner.getMode();
    juce::String status;
    if (mode == dsp::Mode::Shift)
        status = joinWithDots({"SHIFT", formatSemitones(pitch).toUpperCase()});
    else if (mode == dsp::Mode::Snap)
        status = joinWithDots({"SNAP", "CHROMATIC", formatSemitones(pitch).toUpperCase()});
    else if (engine.getHeldNote() >= 0)
        status =
            joinWithDots({"ROBOT", "MIDI " + noteText(engine.getHeldNote()), formatSemitones(pitch).toUpperCase()});
    else
        status = joinWithDots({"ROBOT", noteText(dsp::kLowestNote + juce::roundToInt(value(ids::note))),
                               formatSemitones(pitch).toUpperCase()});
    const auto footer =
        juce::Rectangle<float>(area.getX() + 18.0f, area.getBottom() - 26.0f, area.getWidth() - 36.0f, 14.0f);
    g.setFont(makeFont(10.0f, true, 0.16f));
    g.setColour(palette.accentAlt);
    g.drawText(status, footer, juce::Justification::centredLeft, false);
    g.setColour(palette.textDim);
    const float hz = engine.getOutputHz();
    g.drawText(live && hz > 0.0f ? juce::String(hz, 1) + " HZ" : juce::String("LISTENING"), footer,
               juce::Justification::centredRight, false);
}

void ParrotEditor::paintModePanel(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    const auto mode = owner.getMode();
    const bool robot = mode == dsp::Mode::Robot;
    paintPanel(g, area, "MODE", palette.text);

    g.setColour(palette.textDim.withAlpha(robot ? 1.0f : 0.5f));
    g.setFont(makeFont(9.5f, true, 0.22f));
    g.drawText("ROBOT NOTE", juce::Rectangle<float>(area.getX() + 18.0f, area.getY() + 76.0f, 200.0f, 12.0f),
               juce::Justification::centredLeft, false);

    const auto box = juce::Rectangle<float>(area.getX() + 52.0f, area.getY() + 92.0f, 64.0f, 28.0f);
    g.setColour(palette.background);
    g.fillRoundedRectangle(box, 6.0f);
    g.setColour(palette.outline);
    g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);
    const int held = owner.getEngine().getHeldNote();
    const bool midi = robot && held >= 0;
    g.setColour(midi ? palette.accentAlt : robot ? palette.text : palette.textDim);
    g.setFont(makeFont(15.0f, true, 0.04f));
    g.drawText(noteText(midi ? held : dsp::kLowestNote + juce::roundToInt(value(ids::note))), box,
               juce::Justification::centred, false);
    g.setColour(midi ? palette.accentAlt : palette.textDim.withAlpha(robot ? 0.9f : 0.45f));
    g.setFont(makeFont(9.5f, true, 0.14f));
    g.drawText(midi ? "MIDI KEY HELD" : "OR HOLD A MIDI KEY",
               juce::Rectangle<float>(box.getRight() + 44.0f, box.getY(), 140.0f, box.getHeight()),
               juce::Justification::centredLeft, false);

    g.setColour(palette.textDim);
    g.setFont(makeFont(11.0f, false));
    g.drawFittedText(modeBlurb(mode),
                     juce::Rectangle<int>(static_cast<int>(area.getX()) + 18, static_cast<int>(area.getY()) + 124,
                                          static_cast<int>(area.getWidth()) - 136, 22),
                     juce::Justification::centredLeft, 1, 0.9f);

    const auto glide = glideKnob.getBounds().toFloat();
    g.setColour(palette.textDim.withAlpha(mode == dsp::Mode::Shift ? 0.5f : 1.0f));
    g.setFont(makeFont(9.5f, true, 0.22f));
    g.drawText("GLIDE", glide.withY(glide.getBottom() + 2.0f).withHeight(12.0f), juce::Justification::centred, false);
    g.setColour(palette.text.withAlpha(mode == dsp::Mode::Shift ? 0.5f : 1.0f));
    g.setFont(makeFont(12.0f, true, 0.04f));
    g.drawText(juce::String(juce::roundToInt(value(ids::glide))) + " ms",
               glide.withY(glide.getBottom() + 15.0f).withHeight(14.0f), juce::Justification::centred, false);
}

void ParrotEditor::paintDrivePanel(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    paintPanel(g, area, "DRIVE", palette.accentAlt);

    const auto knob = driveKnob.getBounds().toFloat();
    g.setColour(palette.text);
    g.setFont(makeFont(13.0f, true, 0.04f));
    g.drawText(juce::String(juce::roundToInt(value(ids::drive) * 100.0f)) + " %",
               knob.withY(knob.getBottom() - 2.0f).withHeight(16.0f), juce::Justification::centred, false);

    // A 12AX7 whose heater glows with the drive and flares with the signal.
    const auto slot = juce::Rectangle<float>(area.getRight() - 92.0f, area.getY() + 12.0f, 74.0f, 128.0f);
    const auto glass = juce::Rectangle<float>(slot.getCentreX() - 19.0f, slot.getY() + 8.0f, 38.0f, 86.0f);
    const float glow = std::clamp(std::max(shownHeat, 0.18f * value(ids::drive)), 0.0f, 1.0f);
    const auto hot = palette.accentAlt.interpolatedWith(palette.accent, shownHeat * 0.7f);
    const auto core = juce::Point<float>(glass.getCentreX(), glass.getY() + glass.getHeight() * 0.55f);
    if (glow > 0.01f)
    {
        g.setGradientFill(juce::ColourGradient(hot.withAlpha(0.45f * glow), core.x, core.y, hot.withAlpha(0.0f),
                                               core.x + 46.0f, core.y, true));
        g.fillEllipse(juce::Rectangle<float>(92.0f, 92.0f).withCentre(core));
    }

    juce::Path envelope;
    envelope.addRoundedRectangle(glass.getX(), glass.getY(), glass.getWidth(), glass.getHeight(), 19.0f, 19.0f, true,
                                 true, false, false);
    g.setColour(palette.text.withAlpha(0.05f));
    g.fillPath(envelope);

    const auto plate = juce::Rectangle<float>(glass.getCentreX() - 11.0f, glass.getY() + 28.0f, 22.0f, 40.0f);
    g.setColour(palette.panelRaised.darker(0.3f));
    g.fillRect(plate);
    g.setColour(palette.outline.brighter(0.2f));
    g.drawRect(plate, 1.0f);
    for (int i = 0; i < 5; ++i)
        g.fillRect(plate.getX() + 2.0f, plate.getY() + 5.0f + 7.5f * static_cast<float>(i), plate.getWidth() - 4.0f,
                   0.8f);
    const auto heater = juce::Rectangle<float>(4.0f, 30.0f).withCentre(plate.getCentre());
    g.setColour(hot.withAlpha(0.2f + 0.8f * glow));
    g.fillRoundedRectangle(heater, 2.0f);
    g.setColour(palette.text.withAlpha(0.5f * glow));
    g.fillRoundedRectangle(heater.reduced(1.2f, 4.0f), 1.0f);

    g.setColour(palette.text.withAlpha(0.12f));
    g.fillEllipse(juce::Rectangle<float>(16.0f, 6.0f).withCentre({glass.getCentreX(), glass.getY() + 10.0f}));
    g.setColour(palette.text.withAlpha(0.24f));
    g.strokePath(envelope, juce::PathStrokeType(1.0f));
    g.setColour(palette.text.withAlpha(0.08f));
    g.fillRect(glass.getX() + 6.0f, glass.getY() + 16.0f, 2.0f, glass.getHeight() - 24.0f);

    const auto base = juce::Rectangle<float>(glass.getX() - 3.0f, glass.getBottom(), glass.getWidth() + 6.0f, 11.0f);
    g.setColour(palette.panelRaised.brighter(0.15f));
    g.fillRoundedRectangle(base, 2.5f);
    g.setColour(palette.outline.brighter(0.3f));
    for (int i = 0; i < 4; ++i)
        g.fillRect(base.getX() + 7.0f + 9.0f * static_cast<float>(i), base.getBottom(), 1.5f, 4.0f);

    g.setColour(palette.textDim);
    g.setFont(makeFont(9.0f, true, 0.16f));
    g.drawText(juce::String(juce::CharPointer_UTF8("12AX7 \xc3\x97 2")),
               slot.withY(base.getBottom() + 6.0f).withHeight(12.0f), juce::Justification::centred, false);
}

void ParrotEditor::paintMixPanel(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    paintPanel(g, area, "MIX", palette.text);

    const float x = area.getX() + 128.0f;
    const float w = area.getRight() - 18.0f - x;
    g.setColour(palette.text);
    g.setFont(makeFont(22.0f, true, 0.02f));
    g.drawText(juce::String(juce::roundToInt(value(ids::mix) * 100.0f)) + " %",
               juce::Rectangle<float>(x, area.getY() + 40.0f, w, 26.0f), juce::Justification::centredLeft, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.22f));
    g.drawText("DRY / WET", juce::Rectangle<float>(x, area.getY() + 66.0f, w, 12.0f), juce::Justification::centredLeft,
               false);

    g.setColour(palette.outline);
    g.fillRect(x, area.getY() + 90.0f, w, 1.0f);
    g.setColour(palette.textDim);
    g.drawText("LATENCY", juce::Rectangle<float>(x, area.getY() + 100.0f, w, 12.0f), juce::Justification::centredLeft,
               false);
    const double rate = owner.getSampleRate() > 0.0 ? owner.getSampleRate() : 44100.0;
    g.setColour(palette.text);
    g.setFont(makeFont(13.0f, true, 0.04f));
    g.drawText(juce::String(juce::roundToInt(1000.0 * owner.getLatencySamples() / rate)) + " ms",
               juce::Rectangle<float>(x, area.getY() + 113.0f, w, 16.0f), juce::Justification::centredLeft, false);
}

void ParrotEditor::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    g.setGradientFill(juce::ColourGradient(palette.background.brighter(0.05f), 0.0f, kHeaderH, palette.background, 0.0f,
                                           static_cast<float>(getHeight()), false));
    g.fillAll();
    paintHeader(g);

    const bool fine = value(ids::fine) > 0.5f;
    const bool link = value(ids::link) > 0.5f;
    const float pitch = dsp::appliedPitch(value(ids::pitch), fine);
    const float formant = fine ? value(ids::formant) : std::round(value(ids::formant));
    paintKnobPanel(g, kPitchPanel, "PITCH", palette.accent, formatSemitones(pitch), intervalName(pitch));
    const juce::String character = formant < 0.0f ? "BIGGER, DARKER" : formant > 0.0f ? "SMALLER, BRIGHTER" : "NATURAL";
    paintKnobPanel(
        g, kFormantPanel, "FORMANT", palette.detail, formatSemitones(formant),
        link
            ? "+ PITCH = " +
                  formatSemitones(dsp::appliedFormant(value(ids::pitch), value(ids::formant), fine, link)).toUpperCase()
            : juce::String(character));
    paintDisplay(g, kDisplay);
    paintModePanel(g, kModePanel);
    paintDrivePanel(g, kDrivePanel);
    paintMixPanel(g, kMixPanel);

    g.setColour(palette.textDim.withAlpha(0.6f));
    g.setFont(makeFont(10.5f, false));
    const float y = static_cast<float>(getHeight()) - 25.0f;
    g.drawText(animalaudio::core::buildStampShort(), juce::Rectangle<float>(20.0f, y, 400.0f, 14.0f),
               juce::Justification::centredLeft, false);
    g.drawText("MIDI NOTES PLAY ROBOT MODE",
               juce::Rectangle<float>(static_cast<float>(getWidth()) - 320.0f, y, 300.0f, 14.0f),
               juce::Justification::centredRight, false);
}

void ParrotEditor::resized()
{
    brand.setBounds(getWidth() - 20 - 150, 26, 150, 18);

    const auto knobIn = [](juce::Rectangle<float> panel, float size)
    {
        return juce::Rectangle<float>(panel.getCentreX() - size * 0.5f, panel.getY() + 32.0f, size, size)
            .getSmallestIntegerContainer();
    };
    pitchKnob.setBounds(knobIn(kPitchPanel, 152.0f));
    formantKnob.setBounds(knobIn(kFormantPanel, 152.0f));
    const auto pill = [](juce::Rectangle<float> panel)
    {
        return juce::Rectangle<float>(panel.getCentreX() - 46.0f, panel.getY() + 238.0f, 92.0f, 24.0f)
            .getSmallestIntegerContainer();
    };
    fineButton.setBounds(pill(kPitchPanel));
    linkButton.setBounds(pill(kFormantPanel));

    const auto mode = kModePanel.getSmallestIntegerContainer();
    modePills->setBounds(mode.getX() + 18, mode.getY() + 36, animalaudio::ui::ChoicePillRow::widthFor(3), 28);
    noteDown.setBounds(mode.getX() + 18, mode.getY() + 92, 28, 28);
    noteUp.setBounds(mode.getX() + 122, mode.getY() + 92, 28, 28);
    glideKnob.setBounds(mode.getRight() - 18 - 78, mode.getY() + 22, 78, 78);

    const auto drive = kDrivePanel.getSmallestIntegerContainer();
    driveKnob.setBounds(drive.getX() + 18, drive.getY() + 32, 100, 100);
    const auto mix = kMixPanel.getSmallestIntegerContainer();
    mixKnob.setBounds(mix.getX() + 16, mix.getY() + 32, 100, 100);
}
} // namespace parrot
