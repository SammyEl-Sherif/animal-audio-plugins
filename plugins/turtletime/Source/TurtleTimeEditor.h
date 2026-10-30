#pragma once

#include "TurtleTimeProcessor.h"
#include "ui/TurtleTimeTheme.h"
#include "ui/TurtleTimeWidgets.h"

#include <animalaudio/ui/BrandBadge.h>

#include <array>
#include <memory>

namespace turtletime
{
class TurtleTimeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 940;
    static constexpr int kHeight = 528;

    explicit TurtleTimeEditor(TurtleTimeProcessor& owner);
    ~TurtleTimeEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshSelection();
    int selectedPattern() const;
    bool keepPitchSelected() const;

    void paintHeader(juce::Graphics& g) const;
    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintControls(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintMatrix(juce::Graphics& g, juce::Rectangle<float> area) const;

    TurtleTimeProcessor& owner;
    animalaudio::ui::Theme theme;
    animalaudio::ui::BrandBadge brand;
    ui::TurtleKnob mixKnob;
    std::array<std::unique_ptr<ui::PatternCell>, dsp::kNumPatterns> patternCells;
    std::array<std::unique_ptr<ui::PatternCell>, 2> pitchCells; // DROP, KEEP

    std::unique_ptr<juce::SliderParameterAttachment> mixAttachment;
    int shownPattern = -1;
    int shownKeepPitch = -1;
    juce::TooltipWindow tooltipWindow{this, 400};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TurtleTimeEditor)
};
} // namespace turtletime
