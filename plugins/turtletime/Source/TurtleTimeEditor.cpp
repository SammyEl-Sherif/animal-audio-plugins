#include "TurtleTimeEditor.h"

#include "ui/TurtleTimeMotif.h"

#include <animalaudio/core/BuildStamp.h>

#include <algorithm>
#include <cmath>

namespace turtletime
{
namespace
{
using ui::trackedFont;

constexpr float kHeaderH = 64.0f;
const juce::Rectangle<float> kDisplay{20.0f, 80.0f, 680.0f, 236.0f};
const juce::Rectangle<float> kControls{716.0f, 80.0f, 204.0f, 236.0f};
const juce::Rectangle<float> kMatrix{20.0f, 332.0f, 900.0f, 164.0f};
constexpr float kMatrixInset = 14.0f;
constexpr float kRowLabelW = 116.0f;
constexpr float kRowH = 28.0f;
constexpr float kCellGap = 6.0f;

// MIX panel, top to bottom (offsets from kControls' top).
constexpr float kKnobTop = 28.0f;
constexpr float kKnobSize = 108.0f;
constexpr float kMixValueTop = 134.0f;
constexpr float kPitchDividerTop = 166.0f;
constexpr float kPitchRowTop = 175.0f;
constexpr float kPitchRowH = 26.0f;
constexpr float kStatusDividerTop = 210.0f;
constexpr float kPitchLabelW = 58.0f;

juce::Rectangle<float> pitchRow()
{
    return {kControls.getX() + 14.0f, kControls.getY() + kPitchRowTop, kControls.getWidth() - 28.0f, kPitchRowH};
}

juce::Rectangle<int> pitchCellBounds(int index)
{
    const auto cells = pitchRow().withTrimmedLeft(kPitchLabelW);
    const float cellW = (cells.getWidth() - 4.0f) * 0.5f;
    return juce::Rectangle<float>(cells.getX() + static_cast<float>(index) * (cellW + 4.0f), cells.getY(), cellW,
                                  cells.getHeight())
        .getSmallestIntegerContainer();
}

juce::String text(std::string_view view)
{
    return juce::String(view.data(), view.size());
}

const juce::String& dot()
{
    static const juce::String separator(juce::CharPointer_UTF8("  \xc2\xb7  "));
    return separator;
}

juce::Rectangle<float> rowArea(int row)
{
    const auto inner = kMatrix.reduced(kMatrixInset);
    const float gap = (inner.getHeight() - kRowH * dsp::kNumGroups) / static_cast<float>(dsp::kNumGroups - 1);
    return {inner.getX(), inner.getY() + static_cast<float>(row) * (kRowH + gap), inner.getWidth(), kRowH};
}

juce::Rectangle<int> cellBounds(int index)
{
    const int row = index / dsp::kPatternsPerGroup;
    const int column = index % dsp::kPatternsPerGroup;
    const auto cells = rowArea(row).withTrimmedLeft(kRowLabelW);
    const float cellW =
        (cells.getWidth() - kCellGap * (dsp::kPatternsPerGroup - 1)) / static_cast<float>(dsp::kPatternsPerGroup);
    return juce::Rectangle<float>(cells.getX() + static_cast<float>(column) * (cellW + kCellGap), cells.getY(), cellW,
                                  kRowH)
        .getSmallestIntegerContainer();
}

void fillPanel(juce::Graphics& g, juce::Rectangle<float> area, const animalaudio::ui::Palette& palette)
{
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(area.translated(0.0f, 1.5f), 6.0f);
    g.setGradientFill(juce::ColourGradient(palette.panel.brighter(0.04f), area.getX(), area.getY(), palette.panel,
                                           area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(palette.outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(palette.text.withAlpha(0.04f));
    g.drawHorizontalLine(static_cast<int>(area.getY() + 1.0f), area.getX() + 6.0f, area.getRight() - 6.0f);
}

juce::Path envelope(const dsp::TurtleTimeEngine::Scope& scope, juce::Rectangle<float> area)
{
    constexpr int bins = dsp::TurtleTimeEngine::kScopeBins;
    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.46f;
    auto level = [&](int i)
    {
        const float smoothed = 0.25f * scope.getBin(std::max(0, i - 1)) + 0.5f * scope.getBin(i) +
                               0.25f * scope.getBin(std::min(bins - 1, i + 1));
        return std::min(1.0f, std::pow(std::max(0.0f, smoothed), 0.65f)) * half;
    };
    auto xAt = [&](int i)
    { return area.getX() + area.getWidth() * (static_cast<float>(i) + 0.5f) / static_cast<float>(bins); };

    juce::Path path;
    path.startNewSubPath(area.getX(), mid);
    for (int i = 0; i < bins; ++i)
        path.lineTo(xAt(i), mid - level(i));
    path.lineTo(area.getRight(), mid);
    for (int i = bins - 1; i >= 0; --i)
        path.lineTo(xAt(i), mid + level(i));
    path.closeSubPath();
    return path;
}
} // namespace

TurtleTimeEditor::TurtleTimeEditor(TurtleTimeProcessor& plugin)
    : juce::AudioProcessorEditor(plugin)
    , owner(plugin)
    , theme(ui::makeTheme())
    , brand(theme.palette)
    , mixKnob(theme)
{
    setOpaque(true);
    brand.setTooltip(animalaudio::core::buildStampLong(theme.name));

    mixKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixKnob.setTooltip("Dry / wet. 100% is the full time effect.");
    auto* mixParam = owner.apvts.getParameter(ids::mix.getParamID());
    mixKnob.setDoubleClickReturnValue(true, mixParam->convertFrom0to1(mixParam->getDefaultValue()));
    mixAttachment = std::make_unique<juce::SliderParameterAttachment>(*mixParam, mixKnob);

    for (int i = 0; i < dsp::kNumPatterns; ++i)
    {
        const auto& pattern = dsp::kPatterns[static_cast<std::size_t>(i)];
        auto cell = std::make_unique<ui::PatternCell>(
            text(dsp::kLengthNames[static_cast<std::size_t>(i % dsp::kPatternsPerGroup)]), theme.palette);
        cell->setTooltip(text(pattern.name) + ": " + text(pattern.blurb));
        cell->onClick = [&, i]()
        {
            if (auto* param = owner.apvts.getParameter(ids::pattern.getParamID()))
                param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(i)));
            refreshSelection();
        };
        addAndMakeVisible(*cell);
        patternCells[static_cast<std::size_t>(i)] = std::move(cell);
    }

    for (int keep = 0; keep < 2; ++keep)
    {
        auto cell = std::make_unique<ui::PatternCell>(keep == 1 ? "KEEP" : "DROP", theme.palette);
        cell->setComponentID(keep == 1 ? "keepPitch" : "dropPitch");
        cell->setTooltip(keep == 1 ? "Keep pitch: HALF and SLOW stretch time but keep the original pitch."
                                   : "Drop pitch: HALF and SLOW play slower and lower, like tape (one or two "
                                     "octaves down).");
        cell->onClick = [this, keep]()
        {
            if (auto* param = owner.apvts.getParameter(ids::keepPitch.getParamID()))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(static_cast<float>(keep));
                param->endChangeGesture();
            }
            refreshSelection();
        };
        addAndMakeVisible(*cell);
        pitchCells[static_cast<std::size_t>(keep)] = std::move(cell);
    }

    addAndMakeVisible(brand);
    addAndMakeVisible(mixKnob);

    refreshSelection();
    setSize(kWidth, kHeight);
    startTimerHz(30);
}

TurtleTimeEditor::~TurtleTimeEditor()
{
    stopTimer();
}

int TurtleTimeEditor::selectedPattern() const
{
    return dynamic_cast<juce::AudioParameterChoice*>(owner.apvts.getParameter(ids::pattern.getParamID()))->getIndex();
}

bool TurtleTimeEditor::keepPitchSelected() const
{
    return dynamic_cast<juce::AudioParameterBool*>(owner.apvts.getParameter(ids::keepPitch.getParamID()))->get();
}

void TurtleTimeEditor::refreshSelection()
{
    const int selected = selectedPattern();
    const int keep = keepPitchSelected() ? 1 : 0;
    if (selected == shownPattern && keep == shownKeepPitch)
        return;
    shownPattern = selected;
    shownKeepPitch = keep;
    for (int i = 0; i < dsp::kNumPatterns; ++i)
        patternCells[static_cast<std::size_t>(i)]->setToggleState(i == selected, juce::dontSendNotification);

    // Still clickable for 1x patterns, so the choice can be made before switching to HALF or SLOW.
    const bool applies = dsp::slowsDown(dsp::patternAt(selected).rate);
    for (int i = 0; i < 2; ++i)
    {
        pitchCells[static_cast<std::size_t>(i)]->setToggleState(i == keep, juce::dontSendNotification);
        pitchCells[static_cast<std::size_t>(i)]->setAlpha(applies ? 1.0f : 0.45f);
    }
    repaint();
}

void TurtleTimeEditor::timerCallback()
{
    refreshSelection();
    repaint(kDisplay.expanded(2.0f).getSmallestIntegerContainer());
    repaint(kControls.expanded(2.0f).getSmallestIntegerContainer());
}

void TurtleTimeEditor::paintHeader(juce::Graphics& g) const
{
    const auto palette = theme.palette;
    const auto header = getLocalBounds().toFloat().removeFromTop(kHeaderH);
    g.setGradientFill(juce::ColourGradient(palette.panel, 0.0f, 0.0f, palette.background, 0.0f, kHeaderH, false));
    g.fillRect(header);
    g.setColour(palette.accent.withAlpha(0.9f));
    g.fillRect(header.withHeight(2.0f));
    g.setColour(palette.outline);
    g.fillRect(header.withTop(kHeaderH - 1.0f));

    if (theme.motif)
        theme.motif->drawMascot(g, {18.0f, 12.0f, 40.0f, 40.0f}, palette);

    g.setColour(palette.text);
    g.setFont(trackedFont(22.0f, true, 0.1f));
    g.drawText(theme.name, juce::Rectangle<float>(68.0f, 12.0f, 300.0f, 26.0f), juce::Justification::centredLeft,
               false);
    g.setColour(palette.textDim);
    g.setFont(trackedFont(10.5f, true, 0.3f));
    g.drawText(theme.tagline, juce::Rectangle<float>(69.0f, 38.0f, 300.0f, 14.0f), juce::Justification::centredLeft,
               false);
}

void TurtleTimeEditor::paintDisplay(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto palette = theme.palette;
    const auto& engine = owner.getEngine();
    const int selected = selectedPattern();
    const auto& pattern = dsp::patternAt(selected);
    const int row = selected / dsp::kPatternsPerGroup;
    const int column = selected % dsp::kPatternsPerGroup;
    const bool playing = owner.isHostSynced();

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(area.translated(0.0f, 1.5f), 6.0f);
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(area, 6.0f);
        g.reduceClipRegion(clip);
        g.setColour(palette.ambient);
        g.fillRect(area);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.5f), 0.0f, area.getY(),
                                               juce::Colours::transparentBlack, 0.0f, area.getY() + 16.0f, false));
        g.fillRect(area.withHeight(16.0f));
    }
    g.setColour(palette.outline.darker(0.2f));
    g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);

    // Readout
    auto readout = text(dsp::kGroupNames[static_cast<std::size_t>(row)]) + dot() +
                   text(dsp::kGroupDetails[static_cast<std::size_t>(row)]);
    if (const auto pitch = dsp::pitchLabel(pattern, keepPitchSelected()); !pitch.empty())
        readout << dot() << text(pitch);
    g.setColour(palette.accent);
    g.setFont(trackedFont(11.0f, true, 0.16f));
    g.drawText(readout, juce::Rectangle<float>(area.getX() + 18.0f, area.getY() + 14.0f, 420.0f, 14.0f),
               juce::Justification::centredLeft, false);
    g.setColour(palette.text);
    g.setFont(trackedFont(26.0f, true, 0.04f));
    g.drawText(text(dsp::kLengthNames[static_cast<std::size_t>(column)]),
               juce::Rectangle<float>(area.getX() + 17.0f, area.getY() + 30.0f, 360.0f, 30.0f),
               juce::Justification::centredLeft, false);

    // Legend
    {
        auto legend = juce::Rectangle<float>(area.getRight() - 196.0f, area.getY() + 14.0f, 180.0f, 14.0f);
        g.setFont(trackedFont(10.0f, true, 0.14f));
        auto readKey = legend.removeFromRight(56.0f);
        g.setColour(palette.textDim);
        g.drawText("READ", readKey.withTrimmedLeft(16.0f), juce::Justification::centredLeft, false);
        g.setColour(palette.accentAlt);
        g.fillPath(ui::makeHexagon(readKey.getPosition().translated(6.0f, 7.0f), 5.0f));
        legend.removeFromRight(14.0f);
        auto nowKey = legend.removeFromRight(52.0f);
        g.setColour(palette.textDim);
        g.drawText("NOW", nowKey.withTrimmedLeft(14.0f), juce::Justification::centredLeft, false);
        g.setColour(palette.text);
        g.fillRect(nowKey.getX() + 4.0f, nowKey.getY() + 1.0f, 2.0f, 12.0f);
        legend.removeFromRight(14.0f);
        g.setColour(palette.textDim);
        g.drawText("OUT", legend.removeFromRight(40.0f).withTrimmedLeft(14.0f), juce::Justification::centredLeft,
                   false);
        g.setColour(palette.accent);
        g.fillRect(legend.getRight() + 2.0f, legend.getY() + 4.0f, 8.0f, 6.0f);
    }

    const auto wave = area.reduced(18.0f, 0.0f).withTrimmedTop(84.0f).withTrimmedBottom(38.0f);

    // Grid
    const double beats = pattern.windowBeats;
    const int lines = beats >= 1.0 ? std::max(1, static_cast<int>(std::lround(beats))) : 4;
    g.setFont(trackedFont(9.5f, true, 0.1f));
    for (int i = 0; i <= lines; ++i)
    {
        const float x = wave.getX() + wave.getWidth() * static_cast<float>(i) / static_cast<float>(lines);
        const bool strong = i == 0 || i == lines || (beats >= 1.0 && i % 4 == 0);
        g.setColour(strong ? palette.outline.brighter(0.25f) : palette.outline.withAlpha(0.55f));
        g.fillRect(x, wave.getY(), 1.0f, wave.getHeight());
        const bool label = i < lines && (lines <= 8 || i % 4 == 0);
        if (label)
        {
            const juce::String name =
                beats >= 1.0 ? (lines > 8 ? juce::String(i / 4 + 1) : juce::String(i + 1)) : juce::String(i + 1);
            g.setColour(palette.detail);
            g.drawText(name, juce::Rectangle<float>(x + 4.0f, wave.getY() - 15.0f, 40.0f, 12.0f),
                       juce::Justification::centredLeft, false);
        }
    }
    g.setColour(palette.outline.withAlpha(0.6f));
    g.fillRect(wave.getX(), wave.getCentreY(), wave.getWidth(), 1.0f);

    // Waveforms
    g.setColour(palette.textDim.withAlpha(0.16f));
    g.fillPath(envelope(engine.getInputScope(), wave));
    const auto out = envelope(engine.getOutputScope(), wave);
    g.setGradientFill(juce::ColourGradient(palette.accent.withAlpha(0.42f), 0.0f, wave.getCentreY(),
                                           palette.accent.withAlpha(0.12f), 0.0f, wave.getY(), true));
    g.fillPath(out);
    g.setColour(palette.accent.withAlpha(0.85f));
    g.strokePath(out, juce::PathStrokeType(1.1f));

    // Heads
    const float playX = wave.getX() + wave.getWidth() * owner.getPlayheadPhase();
    const float readX = wave.getX() + wave.getWidth() * owner.getReadMarker();
    g.setColour(palette.accentAlt.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(std::min(playX, readX), wave.getY(), std::abs(playX - readX), wave.getHeight()));
    g.setColour(palette.accentAlt.withAlpha(0.85f));
    g.fillRect(readX - 0.5f, wave.getY(), 1.0f, wave.getHeight());
    const auto hex = ui::makeHexagon({readX, wave.getBottom()}, 6.0f);
    g.setColour(palette.accentAlt);
    g.fillPath(hex);
    g.setColour(palette.ambient);
    g.strokePath(hex, juce::PathStrokeType(1.5f));

    g.setColour(palette.text.withAlpha(0.10f));
    g.fillRect(playX - 3.0f, wave.getY(), 6.0f, wave.getHeight());
    g.setColour(palette.text);
    g.fillRect(playX - 0.75f, wave.getY(), 1.5f, wave.getHeight());
    juce::Path cap;
    cap.addTriangle(playX - 5.0f, wave.getY(), playX + 5.0f, wave.getY(), playX, wave.getY() + 6.0f);
    g.fillPath(cap);

    // Footer line
    const auto footer =
        juce::Rectangle<float>(area.getX() + 18.0f, area.getBottom() - 26.0f, area.getWidth() - 36.0f, 16.0f);
    g.setColour(palette.textDim);
    g.setFont(animalaudio::ui::makeFont(12.0f, false));
    g.drawText(text(pattern.blurb), footer, juce::Justification::centredLeft, true);
    if (!playing)
    {
        g.setColour(palette.accentAlt);
        g.setFont(trackedFont(10.0f, true, 0.14f));
        g.drawText("STOPPED" + dot() + "DRY", footer, juce::Justification::centredRight, false);
    }
}

void TurtleTimeEditor::paintControls(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto palette = theme.palette;
    fillPanel(g, area, palette);

    g.setColour(palette.textDim);
    g.setFont(trackedFont(11.0f, true, 0.24f));
    g.drawText("MIX", area.withHeight(16.0f).translated(0.0f, 14.0f), juce::Justification::centred, false);

    g.setColour(palette.text);
    g.setFont(trackedFont(20.0f, true, 0.04f));
    g.drawText(juce::String(juce::roundToInt(mixKnob.getValue() * 100.0)) + "%",
               juce::Rectangle<float>(area.getX(), area.getY() + kMixValueTop, area.getWidth(), 24.0f),
               juce::Justification::centred, false);

    g.setColour(palette.outline);
    g.fillRect(area.getX() + 14.0f, area.getY() + kPitchDividerTop, area.getWidth() - 28.0f, 1.0f);
    g.setColour(dsp::slowsDown(dsp::patternAt(selectedPattern()).rate) ? palette.textDim
                                                                       : palette.textDim.withAlpha(0.5f));
    g.setFont(trackedFont(10.5f, true, 0.16f));
    g.drawText("PITCH", pitchRow().withWidth(kPitchLabelW).withTrimmedLeft(2.0f), juce::Justification::centredLeft,
               false);

    const float dividerY = area.getY() + kStatusDividerTop;
    g.setColour(palette.outline);
    g.fillRect(area.getX() + 14.0f, dividerY, area.getWidth() - 28.0f, 1.0f);

    const bool playing = owner.isHostSynced();
    const auto status = juce::Rectangle<float>(area.getX() + 16.0f, dividerY + 6.0f, area.getWidth() - 32.0f, 16.0f);
    g.setColour(playing ? palette.accent : palette.detail);
    g.fillEllipse(status.getX(), status.getCentreY() - 3.5f, 7.0f, 7.0f);
    if (playing)
    {
        g.setColour(palette.accent.withAlpha(0.25f));
        g.fillEllipse(status.getX() - 3.0f, status.getCentreY() - 6.5f, 13.0f, 13.0f);
    }
    g.setColour(playing ? palette.text : palette.textDim);
    g.setFont(trackedFont(10.5f, true, 0.16f));
    g.drawText(playing ? "PLAYING" : "STOPPED", status.withTrimmedLeft(14.0f), juce::Justification::centredLeft, false);

    const double bpm = owner.getEngine().getHostBpm();
    g.setColour(palette.textDim);
    g.drawText(bpm > 0.0 ? juce::String(bpm, 1) + " BPM" : juce::String("--- BPM"), status,
               juce::Justification::centredRight, false);
}

void TurtleTimeEditor::paintMatrix(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto palette = theme.palette;
    fillPanel(g, area, palette);

    const int active = selectedPattern() / dsp::kPatternsPerGroup;
    for (int row = 0; row < dsp::kNumGroups; ++row)
    {
        const auto label = rowArea(row).withWidth(kRowLabelW - 10.0f);
        const bool on = row == active;
        if (on)
        {
            g.setColour(palette.accent);
            g.fillRect(label.getX() - 14.0f + 1.0f, label.getY() + 4.0f, 2.0f, label.getHeight() - 8.0f);
        }
        g.setColour(on ? palette.accent : palette.text);
        g.setFont(trackedFont(12.0f, true, 0.14f));
        g.drawText(text(dsp::kGroupNames[static_cast<std::size_t>(row)]), label.withHeight(15.0f),
                   juce::Justification::centredLeft, false);
        g.setColour(palette.textDim);
        g.setFont(trackedFont(9.5f, true, 0.12f));
        g.drawText(text(dsp::kGroupDetails[static_cast<std::size_t>(row)]), label.withTrimmedTop(15.0f),
                   juce::Justification::centredLeft, false);
    }
}

void TurtleTimeEditor::paint(juce::Graphics& g)
{
    const auto palette = theme.palette;
    g.setGradientFill(juce::ColourGradient(palette.background.brighter(0.06f), 0.0f, kHeaderH, palette.background, 0.0f,
                                           static_cast<float>(getHeight()), false));
    g.fillAll();

    paintHeader(g);
    paintDisplay(g, kDisplay);
    paintControls(g, kControls);
    paintMatrix(g, kMatrix);

    g.setColour(palette.detail);
    g.setFont(animalaudio::ui::makeFont(10.5f, false));
    g.drawText(animalaudio::core::buildStampShort(),
               juce::Rectangle<float>(20.0f, static_cast<float>(getHeight()) - 24.0f, 400.0f, 14.0f),
               juce::Justification::centredLeft, false);
    g.drawText("TEMPO FROM HOST",
               juce::Rectangle<float>(static_cast<float>(getWidth()) - 320.0f, static_cast<float>(getHeight()) - 24.0f,
                                      300.0f, 14.0f),
               juce::Justification::centredRight, false);
}

void TurtleTimeEditor::resized()
{
    brand.setBounds(getWidth() - 20 - 150, 23, 150, 18);
    mixKnob.setBounds(juce::Rectangle<float>(kControls.getCentreX() - kKnobSize * 0.5f, kControls.getY() + kKnobTop,
                                             kKnobSize, kKnobSize)
                          .getSmallestIntegerContainer());
    for (int i = 0; i < dsp::kNumPatterns; ++i)
        patternCells[static_cast<std::size_t>(i)]->setBounds(cellBounds(i));
    for (int i = 0; i < 2; ++i)
        pitchCells[static_cast<std::size_t>(i)]->setBounds(pitchCellBounds(i));
}
} // namespace turtletime
