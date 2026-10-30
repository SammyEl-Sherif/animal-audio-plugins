#pragma once

#include "ParrotProcessor.h"
#include "ui/ParrotTheme.h"
#include "ui/ParrotWidgets.h"

#include <animalaudio/ui/BrandBadge.h>
#include <animalaudio/ui/ChoicePillRow.h>
#include <animalaudio/ui/PillButton.h>

#include <memory>
#include <vector>

namespace parrot
{
class ParrotEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 940;
    static constexpr int kHeight = 560;

    explicit ParrotEditor(ParrotProcessor& owner);
    ~ParrotEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::Slider& getPitchKnob() noexcept { return pitchKnob; }
    juce::Slider& getFormantKnob() noexcept { return formantKnob; }
    juce::Button& getFineButton() noexcept { return fineButton; }
    juce::Button& getLinkButton() noexcept { return linkButton; }
    juce::Button& getNoteDownButton() noexcept { return noteDown; }
    juce::Button& getNoteUpButton() noexcept { return noteUp; }
    animalaudio::ui::ChoicePillRow& getModePills() noexcept { return *modePills; }

    /** Advances the display as the timer would (tests call it without a message loop). */
    void refresh();

private:
    void timerCallback() override { refresh(); }

    float value(const juce::ParameterID& id) const;
    void stepNote(int delta);
    void attach(juce::Slider& slider, const juce::ParameterID& id, const juce::String& tooltip);
    void attach(juce::Button& button, const juce::ParameterID& id, const juce::String& tooltip);

    void paintHeader(juce::Graphics& g) const;
    void paintPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, juce::Colour key) const;
    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintKnobPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, juce::Colour key,
                        const juce::String& readout, const juce::String& detail) const;
    void paintModePanel(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintDrivePanel(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintMixPanel(juce::Graphics& g, juce::Rectangle<float> area) const;

    ParrotProcessor& owner;
    animalaudio::ui::Theme theme;
    animalaudio::ui::BrandBadge brand;
    ui::ParrotKnob pitchKnob;
    ui::ParrotKnob formantKnob;
    ui::ParrotKnob glideKnob;
    ui::ParrotKnob driveKnob;
    ui::ParrotKnob mixKnob;
    animalaudio::ui::PillButton fineButton;
    animalaudio::ui::PillButton linkButton;
    animalaudio::ui::PillButton noteDown;
    animalaudio::ui::PillButton noteUp;
    std::unique_ptr<animalaudio::ui::ChoicePillRow> modePills;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    float viewCentre = 57.0f; // MIDI note at the middle of the pitch roll, eased toward the voice
    float shownInput = -1.0f;
    float shownOutput = -1.0f;
    int silentFrames = 1000;
    float shownHeat = 0.0f;
    juce::int64 shownState = 0;

    juce::TooltipWindow tooltipWindow{this, 400};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParrotEditor)
};
} // namespace parrot
