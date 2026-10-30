#pragma once

#include "EditController.h"
#include "NoteCanvas.h"

namespace whale::ui
{
/** The whole song in one strip: every take's waveform, the notes as ticks, the window the editor shows. Click or
 * drag to move that window. */
class Overview : public juce::Component, private juce::ChangeListener
{
public:
    Overview(EditController& controller, NoteCanvas& canvas, const animalaudio::ui::Theme& theme);
    ~Overview() override;

    void setPlayhead(double seconds) { playhead = seconds; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    std::pair<double, double> extent() const;
    float xFor(double seconds) const;
    double secondsFor(float x) const;
    void centreOn(float x);

    EditController& controller;
    NoteCanvas& canvas;
    animalaudio::ui::Theme theme;
    double playhead = 0.0;
};
} // namespace whale::ui
