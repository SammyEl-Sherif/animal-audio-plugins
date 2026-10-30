#pragma once

#include "EditController.h"

#include <animalaudio/ui/AccentKnob.h>
#include <animalaudio/ui/PillButton.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace monkeydyne::ui
{
/**
 * The panels under the note editor. NOTES: pitch, snap, straighten, vibrato; SOUND: formant, gain; GLIDE: time and
 * curve of the transition into the next linked note; SCALE: key and mode SNAP aims at; OUTPUT: mix and compare.
 * Note knobs act on the selection, or on every note when nothing is selected; relative ones (pitch, formant, gain)
 * keep each note's own offset, the others set one value for all.
 */
class Inspector : public juce::Component, private juce::ChangeListener
{
public:
    enum KnobId
    {
        pitchKnob = 0,
        snapKnob,
        straightenKnob,
        vibratoKnob,
        formantKnob,
        gainKnob,
        glideKnob,
        curveKnob,
        knobCount
    };

    Inspector(EditController& controller, const animalaudio::ui::Theme& theme,
              juce::AudioProcessorValueTreeState& apvts);
    ~Inspector() override;

    juce::Slider& getKnob(KnobId id) noexcept { return *knobs[static_cast<std::size_t>(id)].knob; }
    juce::Slider& getMixKnob() noexcept { return mixKnob; }
    juce::Button& getCompareButton() noexcept { return compareButton; }
    juce::Button& getModeButton(int mode) noexcept { return *modeButtons[static_cast<std::size_t>(mode)]; }
    juce::Button& getKeyUpButton() noexcept { return keyUp; }

    /** Re-reads the targets' values into the knobs. */
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Spec
    {
        const char* name;
        double minimum, maximum, interval, defaultValue;
        bool relative;
        bool bipolar;
        std::function<float(const dsp::Note&)> get;
        std::function<void(dsp::Note&, float)> set; // relative specs receive the change, others the value
        std::function<juce::String(float)> format;
        const char* tooltip;
    };

    struct Knob
    {
        Spec spec;
        std::unique_ptr<animalaudio::ui::AccentKnob> knob;
        juce::Rectangle<float> slot;
        double lastValue = 0.0;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void addKnob(Spec spec, juce::Colour colour);
    void knobChanged(Knob& knob);
    juce::Rectangle<float> panelArea(int index) const;
    void paintPanel(juce::Graphics& g, int index, const juce::String& title, juce::Colour key,
                    const juce::String& note) const;
    void paintKnobLabels(juce::Graphics& g, const Knob& knob) const;

    EditController& controller;
    animalaudio::ui::Theme theme;
    std::vector<Knob> knobs;
    animalaudio::ui::AccentKnob mixKnob;
    animalaudio::ui::PillButton compareButton;
    animalaudio::ui::PillButton keyDown;
    animalaudio::ui::PillButton keyUp;
    std::vector<std::unique_ptr<animalaudio::ui::PillButton>> modeButtons;
    std::unique_ptr<juce::SliderParameterAttachment> mixAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> compareAttachment;

    bool updating = false;
    Knob* active = nullptr; // knob being dragged
    std::vector<TakeInfo> origin;
    std::set<NoteRef> originTargets;
    bool originAll = false;
    double startValue = 0.0;
};
} // namespace monkeydyne::ui
