#include "MonkeydyneEditor.h"

#include <animalaudio/core/BuildStamp.h>
#include <animalaudio/core/TextFormat.h>

#include <cmath>

namespace monkeydyne
{
namespace
{
using animalaudio::core::joinWithDots;
using animalaudio::ui::makeFont;

constexpr float kHeaderH = 68.0f;
constexpr int kToolbarY = 78;
constexpr int kToolbarH = 28;
constexpr int kMargin = 20;
constexpr int kOverviewH = 30;
constexpr int kInspectorH = 128;
constexpr int kFooterH = 26;
constexpr int kClearConfirmTicks = 90;

const juce::Rectangle<float> kTransferArea{350.0f, 19.0f, 132.0f, 30.0f};

juce::String clockText(double seconds)
{
    const auto minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String(minutes) + ":" + (rest < 9.95 ? "0" : "") + juce::String(rest, 1);
}

void recordGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, const juce::String& text)
{
    const float d = area.getHeight() * 0.62f;
    const auto font = makeFont(area.getHeight() * 0.78f, true, 0.12f);
    const float width = d + 7.0f + juce::GlyphArrangement::getStringWidth(font, text);
    const float x = area.getCentreX() - width * 0.5f;
    g.setColour(colour);
    g.fillEllipse(x, area.getCentreY() - d * 0.5f, d, d);
    g.setFont(font);
    g.drawText(text, juce::Rectangle<float>(x + d + 7.0f, area.getY() - 2.0f, width, area.getHeight() + 4.0f),
               juce::Justification::centredLeft, false);
}
} // namespace

MonkeydyneEditor::MonkeydyneEditor(MonkeydyneProcessor& plugin)
    : juce::AudioProcessorEditor(plugin)
    , owner(plugin)
    , theme(ui::makeTheme())
    , brand(theme.palette)
    , controller(plugin.getSession())
    , canvas(controller, theme)
    , overview(controller, canvas, theme)
    , inspector(controller, theme, plugin.apvts)
    , transferButton({}, theme.palette)
    , editTool("EDIT", theme.palette)
    , cutTool("CUT", theme.palette)
    , noteGranularity("NOTE", theme.palette)
    , wordGranularity("WORD", theme.palette)
    , phraseGranularity("PHRASE", theme.palette)
    , undoButton("UNDO", theme.palette)
    , redoButton("REDO", theme.palette)
    , followButton("FOLLOW", theme.palette)
    , fitButton("FIT", theme.palette)
    , zoomOutButton("-", theme.palette)
    , zoomInButton("+", theme.palette)
    , clearButton("CLEAR", theme.palette)
{
    setOpaque(true);
    brand.setTooltip(animalaudio::core::buildStampLong(theme.name));
    addAndMakeVisible(brand);
    addAndMakeVisible(canvas);
    addAndMakeVisible(overview);
    addAndMakeVisible(inspector);

    transferButton.glyph = [this](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) {
        recordGlyph(g, r, c, owner.isCapturing() ? "RECORDING" : owner.isTransferArmed() ? "ARMED" : "TRANSFER");
    };
    setupButton(transferButton,
                "Arm, then play the vocal in your host: Monkeydyne records it at its place in the song and "
                "finds its notes. Recording stops when the host stops or jumps.");
    transferButton.onClick = [this]
    {
        owner.setTransferArmed(!owner.isTransferArmed());
        refresh();
    };

    setupButton(editTool, "Edit tool (1): select, move, stretch and glide notes.");
    setupButton(cutTool, "Cut tool (2): click a note to split it; Alt-click a separation to link or unlink; "
                         "double-click to merge.");
    editTool.onClick = [this] { canvas.setTool(ui::Tool::Edit); };
    cutTool.onClick = [this] { canvas.setTool(ui::Tool::Cut); };
    canvas.onToolChanged = [this](ui::Tool tool) { showTool(tool); };

    setupButton(noteGranularity, "A click selects one note.");
    setupButton(wordGranularity, "A click selects the whole word (one sung stretch without a break).");
    setupButton(phraseGranularity, "A click selects the whole phrase (words between pauses).");
    noteGranularity.onClick = [this] { showGranularity(ui::Granularity::Note); };
    wordGranularity.onClick = [this] { showGranularity(ui::Granularity::Word); };
    phraseGranularity.onClick = [this] { showGranularity(ui::Granularity::Phrase); };

    setupButton(undoButton, "Undo (Cmd/Ctrl+Z)");
    setupButton(redoButton, "Redo (Shift+Cmd/Ctrl+Z)");
    undoButton.onClick = [this] { controller.undo(); };
    redoButton.onClick = [this] { controller.redo(); };

    setupButton(followButton, "Scroll with the host playhead.");
    followButton.onClick = [this]
    {
        canvas.setFollow(!canvas.isFollowing());
        followButton.setToggleState(canvas.isFollowing(), juce::dontSendNotification);
    };
    setupButton(fitButton, "Fit every take into view (F).");
    fitButton.onClick = [this] { canvas.fitAll(); };
    setupButton(zoomOutButton, "Zoom out (-). Cmd/Ctrl + wheel zooms time, Alt + wheel zooms pitch.");
    setupButton(zoomInButton, "Zoom in (+).");
    zoomOutButton.onClick = [this] { canvas.zoomTime(1.0 / 1.5, canvas.noteArea().getCentreX()); };
    zoomInButton.onClick = [this] { canvas.zoomTime(1.5, canvas.noteArea().getCentreX()); };
    setupButton(clearButton, "Remove every take and edit (click twice).");
    clearButton.onClick = [this]
    {
        if (clearConfirmTicks > 0)
        {
            controller.removeAllTakes();
            clearConfirmTicks = 0;
        }
        else
        {
            clearConfirmTicks = kClearConfirmTicks;
        }
        clearButton.setButtonText(clearConfirmTicks > 0 ? "SURE?" : "CLEAR");
        clearButton.setToggleState(clearConfirmTicks > 0, juce::dontSendNotification);
    };

    canvas.onAudition = [this](std::uint32_t take, double from, double seconds)
    { owner.audition(take, from, seconds); };
    canvas.onViewChanged = [this] { overview.repaint(); };

    showTool(ui::Tool::Edit);
    showGranularity(ui::Granularity::Note);
    followButton.setToggleState(true, juce::dontSendNotification);

    setResizable(true, true);
    setResizeLimits(980, 620, 2400, 1500);
    setSize(kWidth, kHeight);
    canvas.fitAll();
    refresh();
    startTimerHz(30);
}

MonkeydyneEditor::~MonkeydyneEditor()
{
    stopTimer();
}

void MonkeydyneEditor::setupButton(juce::Button& button, const juce::String& tooltip)
{
    button.setTooltip(tooltip);
    button.setWantsKeyboardFocus(false);
    addAndMakeVisible(button);
}

juce::Button& MonkeydyneEditor::getGranularityButton(ui::Granularity granularity) noexcept
{
    switch (granularity)
    {
    case ui::Granularity::Word:
        return wordGranularity;
    case ui::Granularity::Phrase:
        return phraseGranularity;
    case ui::Granularity::Note:
        break;
    }
    return noteGranularity;
}

void MonkeydyneEditor::showTool(ui::Tool tool)
{
    editTool.setToggleState(tool == ui::Tool::Edit, juce::dontSendNotification);
    cutTool.setToggleState(tool == ui::Tool::Cut, juce::dontSendNotification);
}

void MonkeydyneEditor::showGranularity(ui::Granularity granularity)
{
    canvas.setGranularity(granularity);
    noteGranularity.setToggleState(granularity == ui::Granularity::Note, juce::dontSendNotification);
    wordGranularity.setToggleState(granularity == ui::Granularity::Word, juce::dontSendNotification);
    phraseGranularity.setToggleState(granularity == ui::Granularity::Phrase, juce::dontSendNotification);
}

juce::String MonkeydyneEditor::statusText() const
{
    if (owner.isCapturing())
        return "RECORDING  " + clockText(owner.capturedSeconds());
    if (owner.isTransferArmed())
        return joinWithDots({"ARMED", "PRESS PLAY IN YOUR HOST"});
    const auto& takes = controller.takes();
    for (const auto& take : takes)
        if (take.status == TakeStatus::Analysing)
            return "LISTENING FOR NOTES  " + juce::String(juce::roundToInt(take.progress * 100.0f)) + " %";
    if (takes.empty())
        return "NO AUDIO YET";
    std::size_t notes = 0;
    for (const auto& take : takes)
        notes += take.notes.size();
    const juce::String count = juce::String(static_cast<int>(takes.size())) + (takes.size() == 1 ? " TAKE" : " TAKES");
    const juce::String noteCount = juce::String(static_cast<int>(notes)) + " NOTES";
    const bool rendering = owner.getWorker().activity() == Worker::Activity::Rendering;
    return joinWithDots({count, noteCount, rendering ? "RENDERING" : "READY"});
}

void MonkeydyneEditor::refresh()
{
    controller.refresh();
    std::uint32_t auditionTake = 0;
    const double auditionAt = owner.auditionPosition(auditionTake);
    const double host = owner.getHostSeconds();
    canvas.setTransport(host, owner.isHostPlaying(), auditionTake, auditionAt);
    canvas.setAmbientTime(host);
    overview.setPlayhead(host);
    overview.repaint();

    undoButton.setEnabled(controller.canUndo());
    redoButton.setEnabled(controller.canRedo());
    if (clearConfirmTicks > 0 && --clearConfirmTicks == 0)
    {
        clearButton.setButtonText("CLEAR");
        clearButton.setToggleState(false, juce::dontSendNotification);
    }

    const bool armed = owner.isTransferArmed();
    const bool capturing = owner.isCapturing();
    transferButton.setToggleState(armed || capturing, juce::dontSendNotification);
    if (armed != wasArmed || capturing != wasCapturing)
        transferButton.repaint();
    wasArmed = armed;
    wasCapturing = capturing;

    const float peak = owner.takeInputPeak();
    const float level = peak > 1.0e-5f ? std::clamp((20.0f * std::log10(peak) + 60.0f) / 60.0f, 0.0f, 1.0f) : 0.0f;
    const float shown = std::max(level, meter - 0.04f);
    const auto status = statusText();
    if (status != shownStatus || std::abs(shown - meter) > 0.002f || capturing)
    {
        shownStatus = status;
        meter = shown;
        repaint(
            juce::Rectangle<float>(kTransferArea.getX() - 8.0f, 0.0f, 480.0f, kHeaderH).getSmallestIntegerContainer());
    }
}

void MonkeydyneEditor::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    const auto bounds = getLocalBounds().toFloat();
    metal.paint(g, bounds, palette);

    // Header: a glass strip under an iridescent hairline.
    const auto header = bounds.withHeight(kHeaderH);
    g.setGradientFill(juce::ColourGradient(palette.text.withAlpha(0.065f), 0.0f, 0.0f, palette.text.withAlpha(0.015f),
                                           0.0f, kHeaderH, false));
    g.fillRect(header);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(header.withTop(kHeaderH - 1.0f));
    g.setColour(palette.text.withAlpha(0.06f));
    g.fillRect(header.withY(kHeaderH).withHeight(1.0f));
    juce::ColourGradient hairline(palette.accent, 0.0f, 0.0f, palette.detail, header.getRight(), 0.0f, false);
    hairline.addColour(0.5, palette.accentAlt);
    g.setGradientFill(hairline);
    g.fillRect(header.withHeight(2.0f));

    const float mascot = kHeaderH - 16.0f;
    theme.motif->drawMascot(g, {18.0f, 8.0f, mascot, mascot}, palette);
    const float titleX = 28.0f + mascot;
    ui::drawChromeText(g, theme.name, makeFont(26.0f, true, 0.14f), {titleX, 12.0f, 300.0f, 30.0f}, palette);
    g.setColour(palette.textDim);
    g.setFont(makeFont(10.5f, true, 0.3f));
    g.drawText(theme.tagline, juce::Rectangle<float>(titleX + 1.0f, 42.0f, 360.0f, 14.0f),
               juce::Justification::centredLeft, false);

    // Transfer: a gold ring breathes around the button while it records.
    if (owner.isCapturing())
    {
        const float pulse =
            0.5f + 0.5f * static_cast<float>(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.006));
        g.setColour(palette.accentAlt.withAlpha(0.35f + 0.5f * pulse));
        g.drawRoundedRectangle(kTransferArea.expanded(3.0f), kTransferArea.getHeight() * 0.5f + 3.0f, 2.0f);
    }
    const float x = kTransferArea.getRight() + 16.0f;
    g.setColour(owner.isCapturing() ? palette.accentAlt : palette.text);
    g.setFont(makeFont(12.0f, true, 0.16f));
    g.drawText(shownStatus, juce::Rectangle<float>(x, 20.0f, 330.0f, 15.0f), juce::Justification::centredLeft, false);
    const auto bar = juce::Rectangle<float>(x, 41.0f, 180.0f, 4.0f);
    ui::drawGlassWell(g, bar.expanded(1.5f), 3.5f, palette);
    if (meter > 0.0f)
    {
        g.setGradientFill(juce::ColourGradient(palette.accent, bar.getX(), 0.0f,
                                               meter > 0.92f ? palette.accentAlt : palette.accent.brighter(0.3f),
                                               bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * meter), 2.0f);
    }
    g.setColour(palette.textDim);
    g.setFont(makeFont(8.5f, true, 0.2f));
    g.drawText("INPUT", juce::Rectangle<float>(bar.getRight() + 8.0f, 36.0f, 60.0f, 14.0f),
               juce::Justification::centredLeft, false);

    g.setColour(palette.textDim);
    g.setFont(makeFont(9.0f, true, 0.22f));
    g.drawText("TOOL",
               juce::Rectangle<float>(static_cast<float>(kMargin), static_cast<float>(kToolbarY), 40.0f,
                                      static_cast<float>(kToolbarH)),
               juce::Justification::centredLeft, false);
    g.drawText("SELECT",
               juce::Rectangle<float>(static_cast<float>(noteGranularity.getX() - 52), static_cast<float>(kToolbarY),
                                      50.0f, static_cast<float>(kToolbarH)),
               juce::Justification::centredLeft, false);

    g.setColour(palette.textDim.withAlpha(0.6f));
    g.setFont(makeFont(10.5f, false));
    const float y = static_cast<float>(getHeight()) - 20.0f;
    g.drawText(animalaudio::core::buildStampShort(),
               juce::Rectangle<float>(static_cast<float>(kMargin), y, 400.0f, 14.0f), juce::Justification::centredLeft,
               false);
    g.drawText(joinWithDots(
                   {"DRAG: PITCH OR TIME", "ALT: CENTS", "EDGES: STRETCH", "DOUBLE-CLICK: SNAP", "RIGHT-CLICK: MORE"}),
               juce::Rectangle<float>(static_cast<float>(getWidth()) - 620.0f, y, 600.0f, 14.0f),
               juce::Justification::centredRight, false);
}

void MonkeydyneEditor::resized()
{
    const int width = getWidth();
    const int height = getHeight();
    brand.setBounds(width - kMargin - 150, 26, 150, 18);
    transferButton.setBounds(kTransferArea.getSmallestIntegerContainer());

    int x = kMargin + 40;
    const auto pill = [&](juce::Button& button, int w)
    {
        button.setBounds(x, kToolbarY, w, kToolbarH);
        x += w + 4;
    };
    pill(editTool, 58);
    pill(cutTool, 58);
    x += 62;
    pill(noteGranularity, 64);
    pill(wordGranularity, 64);
    pill(phraseGranularity, 72);
    x += 18;
    pill(undoButton, 62);
    pill(redoButton, 62);

    int right = width - kMargin;
    const auto rightPill = [&](juce::Button& button, int w)
    {
        right -= w;
        button.setBounds(right, kToolbarY, w, kToolbarH);
        right -= 4;
    };
    rightPill(clearButton, 72);
    right -= 14;
    rightPill(zoomInButton, kToolbarH);
    rightPill(zoomOutButton, kToolbarH);
    rightPill(fitButton, 48);
    rightPill(followButton, 78);

    const int inspectorY = height - kFooterH - kInspectorH;
    const int overviewY = inspectorY - 8 - kOverviewH;
    const int canvasY = kToolbarY + kToolbarH + 8;
    canvas.setBounds(kMargin, canvasY, width - 2 * kMargin, overviewY - 8 - canvasY);
    overview.setBounds(kMargin, overviewY, width - 2 * kMargin, kOverviewH);
    inspector.setBounds(kMargin, inspectorY, width - 2 * kMargin, kInspectorH);
}

bool MonkeydyneEditor::keyPressed(const juce::KeyPress& key)
{
    return canvas.keyPressed(key);
}
} // namespace monkeydyne
