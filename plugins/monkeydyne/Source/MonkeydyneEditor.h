#pragma once

#include "MonkeydyneProcessor.h"
#include "ui/EditController.h"
#include "ui/Inspector.h"
#include "ui/NoteCanvas.h"
#include "ui/Overview.h"
#include "ui/MonkeydyneTheme.h"

#include <animalaudio/ui/BrandBadge.h>
#include <animalaudio/ui/PillButton.h>

namespace monkeydyne
{
class MonkeydyneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 1180;
    static constexpr int kHeight = 720;

    explicit MonkeydyneEditor(MonkeydyneProcessor& owner);
    ~MonkeydyneEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    ui::EditController& getController() noexcept { return controller; }
    ui::NoteCanvas& getCanvas() noexcept { return canvas; }
    ui::Inspector& getInspector() noexcept { return inspector; }
    juce::Button& getTransferButton() noexcept { return transferButton; }
    juce::Button& getToolButton(ui::Tool tool) noexcept { return tool == ui::Tool::Edit ? editTool : cutTool; }
    juce::Button& getGranularityButton(ui::Granularity granularity) noexcept;
    juce::Button& getUndoButton() noexcept { return undoButton; }
    juce::Button& getRedoButton() noexcept { return redoButton; }
    juce::Button& getClearButton() noexcept { return clearButton; }

    /** Advances the display as the timer would (tests call it without a message loop). */
    void refresh();

private:
    void timerCallback() override { refresh(); }
    void setupButton(juce::Button& button, const juce::String& tooltip);
    void showTool(ui::Tool tool);
    void showGranularity(ui::Granularity granularity);
    juce::String statusText() const;

    MonkeydyneProcessor& owner;
    animalaudio::ui::Theme theme;
    animalaudio::ui::BrandBadge brand;
    ui::EditController controller;
    ui::NoteCanvas canvas;
    ui::Overview overview;
    ui::Inspector inspector;

    animalaudio::ui::PillButton transferButton;
    animalaudio::ui::PillButton editTool;
    animalaudio::ui::PillButton cutTool;
    animalaudio::ui::PillButton noteGranularity;
    animalaudio::ui::PillButton wordGranularity;
    animalaudio::ui::PillButton phraseGranularity;
    animalaudio::ui::PillButton undoButton;
    animalaudio::ui::PillButton redoButton;
    animalaudio::ui::PillButton followButton;
    animalaudio::ui::PillButton fitButton;
    animalaudio::ui::PillButton zoomOutButton;
    animalaudio::ui::PillButton zoomInButton;
    animalaudio::ui::PillButton clearButton;

    juce::String shownStatus;
    float meter = 0.0f;
    int clearConfirmTicks = 0;
    bool wasCapturing = false;
    bool wasArmed = false;

    juce::TooltipWindow tooltipWindow{this, 500};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonkeydyneEditor)
};
} // namespace monkeydyne
