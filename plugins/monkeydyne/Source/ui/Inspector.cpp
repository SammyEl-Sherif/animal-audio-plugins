#include "Inspector.h"

#include "MonkeydyneProcessor.h"

#include <animalaudio/core/TextFormat.h>
#include <animalaudio/ui/Drawing.h>
#include <animalaudio/ui/Panels.h>

#include <algorithm>
#include <cmath>

namespace monkeydyne::ui
{
namespace
{
using animalaudio::core::formatSemitones;
using animalaudio::ui::makeFont;

constexpr float kBaseWidths[] = {380.0f, 196.0f, 196.0f, 160.0f, 184.0f};
constexpr float kGap = 6.0f;
constexpr float kKnobSize = 66.0f;
constexpr float kKnobTop = 32.0f;
constexpr int kPanelCount = 5;
const char* const kKeyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kModeNames[] = {"CHR", "MAJ", "MIN"};

juce::String percent(float v)
{
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

juce::String decibels(float v)
{
    const float rounded = std::round(v * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String(rounded, 1) + " dB";
}

juce::String curveName(float curve)
{
    const int amount = juce::roundToInt(std::abs(curve) * 100.0f);
    if (amount == 0)
        return "EVEN";
    return (curve < 0.0f ? "EARLY " : "LATE ") + juce::String(amount);
}
} // namespace

Inspector::Inspector(EditController& c, const animalaudio::ui::Theme& t, juce::AudioProcessorValueTreeState& apvts)
    : controller(c)
    , theme(t)
    , mixKnob(theme, theme.palette.text, false)
    , compareButton("COMPARE", theme.palette)
    , keyDown({}, theme.palette)
    , keyUp({}, theme.palette)
{
    const auto& palette = theme.palette;
    knobs.reserve(knobCount);
    addKnob({"PITCH", -24.0, 24.0, 0.01, 0.0, true, true, [](const dsp::Note& n) { return n.pitch; },
             [](dsp::Note& n, float change) { n.pitch += change; },
             [](float v) { return formatSemitones(v).toUpperCase(); },
             "Moves the notes up or down; each keeps its own offset. In the editor, drag a note to move it in "
             "semitones (Alt: in cents)."},
            palette.accent);
    addKnob({"SNAP", 0.0, 1.0, 0.01, 0.0, false, false, [](const dsp::Note& n) { return n.snap; },
             [](dsp::Note& n, float v) { n.snap = v; }, percent,
             "How far each note's centre is pulled onto the nearest note of the scale. 100% sits exactly on it."},
            palette.accent);
    addKnob({"STRAIGHTEN", 0.0, 1.0, 0.01, 0.0, false, false, [](const dsp::Note& n) { return 1.0f - n.drift; },
             [](dsp::Note& n, float v) { n.drift = 1.0f - v; }, percent,
             "Flattens the slow wander inside each note (scoops, sags); vibrato stays. 100% holds the centre."},
            palette.accent.interpolatedWith(palette.text, 0.35f));
    addKnob({"VIBRATO", 0.0, 2.0, 0.01, 1.0, false, false, [](const dsp::Note& n) { return n.vibrato; },
             [](dsp::Note& n, float v) { n.vibrato = v; }, percent,
             "Fast pitch movement kept: 0% removes the vibrato, 100% as sung, 200% doubles it."},
            palette.accent.interpolatedWith(palette.detail, 0.4f));
    addKnob({"FORMANT", -12.0, 12.0, 0.01, 0.0, true, true, [](const dsp::Note& n) { return n.formant; },
             [](dsp::Note& n, float change) { n.formant += change; },
             [](float v) { return formatSemitones(v).toUpperCase(); },
             "Moves the vocal tract's resonances without changing the note: down sounds bigger, up smaller."},
            palette.detail);
    addKnob({"GAIN", -18.0, 18.0, 0.1, 0.0, true, true, [](const dsp::Note& n) { return n.gainDb; },
             [](dsp::Note& n, float change) { n.gainDb = std::clamp(n.gainDb + change, -60.0f, 18.0f); }, decibels,
             "Level of each note, in dB."},
            palette.text);
    addKnob({"GLIDE", 0.005, static_cast<double>(dsp::kMaxGlide), 0.001, static_cast<double>(dsp::kDefaultGlide), false,
             false, [](const dsp::Note& n) { return n.glide; },
             [](dsp::Note& n, float v)
             {
                 n.glide = v;
                 n.shaped = true;
             },
             [](float v) { return juce::String(juce::roundToInt(v * 1000.0f)) + " ms"; },
             "How long a note takes to slide into the next one it is linked to (dotted separation). Turning it "
             "replaces the slide as sung with this one; double-click the glide handle in the editor to go back."},
            palette.accentAlt);
    addKnob({"CURVE", -1.0, 1.0, 0.01, 0.0, false, true, [](const dsp::Note& n) { return n.curve; },
             [](dsp::Note& n, float v)
             {
                 n.curve = v;
                 n.shaped = true;
             },
             curveName,
             "Shape of the glide: EARLY leaves the note at once and eases in, LATE holds it and arrives quickly."},
            palette.accentAlt);

    auto* mixParam = apvts.getParameter(ids::mix.getParamID());
    mixKnob.setTooltip("Edited (100%) against the recording (0%), over the takes.");
    mixKnob.setDoubleClickReturnValue(true, mixParam->convertFrom0to1(mixParam->getDefaultValue()));
    mixAttachment = std::make_unique<juce::SliderParameterAttachment>(*mixParam, mixKnob);
    mixKnob.onValueChange = [this] { repaint(); };
    addAndMakeVisible(mixKnob);
    compareButton.setClickingTogglesState(true);
    compareButton.setTooltip("Hear the recording without any edit (bypasses Monkeydyne's changes, not the takes).");
    compareAttachment = std::make_unique<juce::ButtonParameterAttachment>(
        *apvts.getParameter(ids::compare.getParamID()), compareButton);
    addAndMakeVisible(compareButton);

    keyDown.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col)
    { animalaudio::ui::drawArrowGlyph(g, r, col, false); };
    keyUp.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col)
    { animalaudio::ui::drawArrowGlyph(g, r, col, true); };
    keyDown.setTooltip("Key down a semitone");
    keyUp.setTooltip("Key up a semitone");
    const auto stepKey = [this](int delta)
    {
        auto scale = controller.scale();
        scale.key = (scale.key + delta + 12) % 12;
        controller.setScale(scale);
    };
    keyDown.onClick = [stepKey] { stepKey(-1); };
    keyUp.onClick = [stepKey] { stepKey(1); };
    addAndMakeVisible(keyDown);
    addAndMakeVisible(keyUp);
    static const char* const modeTips[] = {"SNAP aims at every semitone.", "SNAP aims at the notes of the major scale.",
                                           "SNAP aims at the notes of the natural minor scale."};
    for (int i = 0; i < 3; ++i)
    {
        auto button = std::make_unique<animalaudio::ui::PillButton>(kModeNames[i], palette);
        button->setTooltip(modeTips[i]);
        button->onClick = [this, i]
        {
            auto scale = controller.scale();
            scale.mode = static_cast<dsp::Scale::Mode>(i);
            controller.setScale(scale);
        };
        addAndMakeVisible(*button);
        modeButtons.push_back(std::move(button));
    }

    controller.addChangeListener(this);
    refresh();
}

Inspector::~Inspector()
{
    controller.removeChangeListener(this);
}

void Inspector::addKnob(Spec spec, juce::Colour colour)
{
    Knob k;
    k.knob = std::make_unique<animalaudio::ui::AccentKnob>(theme, colour, spec.bipolar);
    k.knob->setRange(spec.minimum, spec.maximum, spec.interval);
    k.knob->setDoubleClickReturnValue(true, spec.defaultValue);
    k.knob->setValue(spec.defaultValue, juce::dontSendNotification);
    k.knob->setTooltip(spec.tooltip);
    k.spec = std::move(spec);
    k.lastValue = k.spec.defaultValue;
    const auto index = knobs.size();
    k.knob->onDragStart = [this, index]
    {
        auto& knob = knobs[index];
        active = &knob;
        origin = controller.takes();
        originTargets = controller.selection();
        originAll = originTargets.empty();
        startValue = knob.knob->getValue();
        controller.beginGesture();
    };
    k.knob->onDragEnd = [this]
    {
        active = nullptr;
        controller.endGesture();
    };
    k.knob->onValueChange = [this, index] { knobChanged(knobs[index]); };
    addAndMakeVisible(*k.knob);
    knobs.push_back(std::move(k));
}

void Inspector::knobChanged(Knob& k)
{
    if (updating)
        return;
    const bool dragging = active == &k;
    if (!dragging)
    {
        origin = controller.takes();
        originTargets = controller.selection();
        originAll = originTargets.empty();
        startValue = k.lastValue;
        controller.beginGesture();
    }
    const double value = k.knob->getValue();
    const auto change = static_cast<float>(value - startValue);
    for (const auto& take : origin)
    {
        if (take.analysis == nullptr)
            continue;
        auto notes = take.notes;
        bool any = false;
        for (auto& n : notes)
        {
            if (!originAll && originTargets.count({take.id, n.id}) == 0)
                continue;
            k.spec.set(n, k.spec.relative ? change : static_cast<float>(value));
            any = true;
        }
        if (any)
            controller.setNotes(take.id, std::move(notes));
    }
    k.lastValue = value;
    if (!dragging)
        controller.endGesture();
    repaint();
}

void Inspector::refresh()
{
    updating = true;
    const dsp::Note* first = nullptr;
    for (const auto& ref : controller.targets())
        if ((first = controller.note(ref)) != nullptr)
            break;
    for (auto& k : knobs)
    {
        k.knob->setEnabled(first != nullptr);
        if (&k == active)
            continue;
        const double v = first != nullptr ? static_cast<double>(k.spec.get(*first)) : k.spec.defaultValue;
        k.knob->setValue(v, juce::dontSendNotification);
        k.lastValue = k.knob->getValue();
    }
    const auto scale = controller.scale();
    for (std::size_t i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setToggleState(static_cast<int>(i) == static_cast<int>(scale.mode), juce::dontSendNotification);
    updating = false;
    repaint();
}

juce::Rectangle<float> Inspector::panelArea(int index) const
{
    float total = 0.0f;
    for (const float w : kBaseWidths)
        total += w;
    const float scale = (static_cast<float>(getWidth()) - kGap * (kPanelCount - 1)) / total;
    float x = 0.0f;
    for (int i = 0; i < index; ++i)
        x += kBaseWidths[i] * scale + kGap;
    return {x, 0.0f, kBaseWidths[index] * scale, static_cast<float>(getHeight())};
}

void Inspector::resized()
{
    const auto place = [this](int panel, int first, int count)
    {
        const auto area = panelArea(panel).reduced(8.0f, 0.0f);
        const float slot = area.getWidth() / static_cast<float>(count);
        for (int i = 0; i < count; ++i)
        {
            auto& k = knobs[static_cast<std::size_t>(first + i)];
            k.slot =
                juce::Rectangle<float>(area.getX() + slot * static_cast<float>(i), area.getY(), slot, area.getHeight());
            k.knob->setBounds(juce::Rectangle<float>(kKnobSize, kKnobSize)
                                  .withCentre({k.slot.getCentreX(), area.getY() + kKnobTop + kKnobSize * 0.5f})
                                  .getSmallestIntegerContainer());
        }
    };
    place(0, pitchKnob, 4);
    place(1, formantKnob, 2);
    place(2, glideKnob, 2);

    const auto scale = panelArea(3).getSmallestIntegerContainer();
    keyDown.setBounds(scale.getX() + 16, scale.getY() + 42, 24, 24);
    keyUp.setBounds(scale.getRight() - 16 - 24, scale.getY() + 42, 24, 24);
    const int pillWidth = (scale.getWidth() - 32 - 8) / 3;
    for (int i = 0; i < 3; ++i)
        modeButtons[static_cast<std::size_t>(i)]->setBounds(scale.getX() + 16 + i * (pillWidth + 4), scale.getY() + 96,
                                                            pillWidth, 24);

    const auto output = panelArea(4).getSmallestIntegerContainer();
    const int size = static_cast<int>(kKnobSize);
    mixKnob.setBounds(output.getX() + 12, output.getY() + static_cast<int>(kKnobTop), size, size);
    const int column = 12 + size + 10;
    compareButton.setBounds(output.getX() + column, output.getY() + 90, output.getWidth() - column - 12, 24);
}

void Inspector::paintPanel(juce::Graphics& g, int index, const juce::String& title, juce::Colour key,
                           const juce::String& note) const
{
    const auto area = panelArea(index);
    animalaudio::ui::drawPanel(g, area, title, key, theme.palette);
    if (note.isNotEmpty())
    {
        g.setColour(theme.palette.textDim.withAlpha(0.8f));
        g.setFont(makeFont(9.0f, true, 0.16f));
        g.drawText(note,
                   juce::Rectangle<float>(area.getX() + 18.0f, area.getY() + 14.0f, area.getWidth() - 36.0f, 14.0f),
                   juce::Justification::centredRight, false);
    }
}

void Inspector::paintKnobLabels(juce::Graphics& g, const Knob& k) const
{
    const auto& palette = theme.palette;
    const float y = static_cast<float>(k.knob->getBottom()) + 4.0f;
    const float alpha = k.knob->isEnabled() ? 1.0f : 0.4f;
    g.setColour(palette.textDim.withAlpha(alpha));
    g.setFont(makeFont(9.0f, true, 0.18f));
    g.drawText(k.spec.name, juce::Rectangle<float>(k.slot.getX(), y, k.slot.getWidth(), 11.0f),
               juce::Justification::centred, false);
    g.setColour(palette.text.withAlpha(alpha));
    g.setFont(makeFont(14.0f, true, 0.02f));
    g.drawText(k.spec.format(static_cast<float>(k.knob->getValue())),
               juce::Rectangle<float>(k.slot.getX(), y + 12.0f, k.slot.getWidth(), 17.0f), juce::Justification::centred,
               false);
}

void Inspector::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    const auto& selection = controller.selection();
    std::size_t total = 0;
    bool anyLinked = false;
    bool anyShaped = false;
    for (const auto& take : controller.takes())
    {
        total += take.notes.size();
        for (const auto& note : take.notes)
            if (note.linkedToNext && (selection.empty() || selection.count({take.id, note.id}) > 0))
            {
                anyLinked = true;
                anyShaped = anyShaped || note.shaped;
            }
    }
    const juce::String scope = total == 0          ? juce::String("NO NOTES YET")
                               : selection.empty() ? "ALL " + juce::String(static_cast<int>(total)) + " NOTES"
                                                   : juce::String(static_cast<int>(selection.size())) + " SELECTED";

    paintPanel(g, 0, "NOTES", palette.accent, scope);
    paintPanel(g, 1, "SOUND", palette.detail, {});
    paintPanel(g, 2, "GLIDE", palette.accentAlt,
               total == 0   ? juce::String()
               : !anyLinked ? "LINKED NOTES"
               : anyShaped  ? "SHAPED"
                            : "AS SUNG");
    paintPanel(g, 3, "SCALE", palette.accent.interpolatedWith(palette.text, 0.5f), {});
    paintPanel(g, 4, "OUTPUT", palette.text, {});
    for (const auto& k : knobs)
        paintKnobLabels(g, k);

    const auto scaleArea = panelArea(3);
    const auto scale = controller.scale();
    g.setColour(palette.text);
    g.setFont(makeFont(24.0f, true, 0.04f));
    g.drawText(
        kKeyNames[scale.key],
        juce::Rectangle<float>(scaleArea.getX() + 40.0f, scaleArea.getY() + 40.0f, scaleArea.getWidth() - 80.0f, 26.0f),
        juce::Justification::centred, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(8.5f, true, 0.2f));
    g.drawText(
        scale.mode == dsp::Scale::Mode::Chromatic ? "ANY SEMITONE"
        : scale.mode == dsp::Scale::Mode::Major   ? "MAJOR"
                                                  : "MINOR",
        juce::Rectangle<float>(scaleArea.getX() + 40.0f, scaleArea.getY() + 68.0f, scaleArea.getWidth() - 80.0f, 12.0f),
        juce::Justification::centred, false);

    const auto output = panelArea(4);
    const float column = 12.0f + kKnobSize + 10.0f;
    g.setColour(palette.text);
    g.setFont(makeFont(21.0f, true, 0.02f));
    g.drawText(juce::String(juce::roundToInt(mixKnob.getValue() * 100.0)) + " %",
               juce::Rectangle<float>(output.getX() + column, output.getY() + 40.0f, output.getWidth() - column - 12.0f,
                                      24.0f),
               juce::Justification::centredLeft, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(8.5f, true, 0.2f));
    g.drawText("EDIT MIX",
               juce::Rectangle<float>(output.getX() + column, output.getY() + 64.0f, output.getWidth() - column - 12.0f,
                                      12.0f),
               juce::Justification::centredLeft, false);
}
} // namespace monkeydyne::ui
