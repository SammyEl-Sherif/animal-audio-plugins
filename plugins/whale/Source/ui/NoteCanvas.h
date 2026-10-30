#pragma once

#include "EditController.h"

#include <animalaudio/ui/Theme.h>

#include <functional>

namespace whale::ui
{
enum class Tool
{
    Edit = 0,
    Cut
};

/**
 * The note editor: a piano roll of every take on the song timeline. Notes are blobs shaped by their loudness at
 * their target pitch, with the pitch the edit will sing drawn through them (and, faintly, the pitch as recorded).
 *
 * Edit tool: click / shift-click / drag a band to select (a click takes the note, word or phrase); drag up and down
 * to move notes onto other notes (Alt: in cents), sideways to move them in time; drag a note's edge to stretch it;
 * drag the handle between linked notes sideways for the glide time and up / down for its curve (double-click it:
 * back to the slide as sung); double-click snaps a note to the scale (again: unsnaps). Cut tool: click a note to cut it, Alt-click a separation to link or
 * unlink, double-click one to merge. Wheel scrolls, Cmd / Ctrl + wheel zooms time, Alt + wheel zooms pitch.
 */
class NoteCanvas : public juce::Component, private juce::ChangeListener
{
public:
    static constexpr float kKeysWidth = 46.0f;
    static constexpr float kRulerHeight = 22.0f;

    NoteCanvas(EditController& controller, const animalaudio::ui::Theme& theme);
    ~NoteCanvas() override;

    std::function<void(std::uint32_t takeId, double fromSample, double seconds)> onAudition;
    std::function<void()> onViewChanged;
    std::function<void(Tool)> onToolChanged;

    void setTool(Tool newTool);
    Tool getTool() const noexcept { return tool; }
    void setGranularity(Granularity newGranularity) { granularity = newGranularity; }
    Granularity getGranularity() const noexcept { return granularity; }
    void setFollow(bool shouldFollow) { follow = shouldFollow; }
    bool isFollowing() const noexcept { return follow; }
    /** Host playhead (song seconds) and the note audition position, from the editor's timer. */
    void setTransport(double hostSeconds, bool playing, std::uint32_t auditionTake, double auditionSample);
    /** Clock for the background animation (bubbles drift with the host). */
    void setAmbientTime(double seconds) { ambientTime = seconds; }

    // View -----------------------------------------------------------------------------------------------------------
    juce::Rectangle<float> noteArea() const;
    double getViewStart() const noexcept { return viewStart; }
    double getVisibleSeconds() const;
    void setViewStart(double seconds);
    void zoomTime(double factor, float anchorX);
    void zoomPitch(float factor, float anchorY);
    void fitAll();
    float xForSeconds(double seconds) const noexcept;
    double secondsForX(float x) const noexcept;
    float yForMidi(float midi) const noexcept;
    float midiForY(float y) const noexcept;
    float getRowHeight() const noexcept { return rowHeight; }

    /** Where a note is drawn (its blob's bounds at the current view), empty if it is not known. */
    juce::Rectangle<float> noteBounds(const NoteRef& ref) const;
    /** Centre of the glide handle after `ref` (which must be linked to its next note). */
    juce::Point<float> glideHandle(const NoteRef& ref) const;

    // Actions (also on keys and the context menu) --------------------------------------------------------------------
    void nudgePitch(float semitones);
    void nudgeTime(double seconds);
    void toggleSnap();
    void toggleMute();
    void resetSound();
    void resetTiming();
    bool splitAt(const NoteRef& ref, double songSeconds);
    bool mergeWithNext(const NoteRef& ref);
    bool toggleLink(const NoteRef& ref);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Hit
    {
        enum class Kind
        {
            None,
            Body,
            StartEdge,
            EndEdge,
            Glide,
            Separation
        };
        Kind kind = Kind::None;
        NoteRef ref; // for Glide / Separation: the earlier note
    };

    enum class Drag
    {
        None,
        Pending,
        Pitch,
        Time,
        Stretch,
        Glide,
        Band,
        Pan
    };

    struct DragState
    {
        Drag mode = Drag::None;
        juce::Point<float> start;
        NoteRef ref;
        bool startEdge = false;
        bool clickedSelected = false;
        std::vector<TakeInfo> origin;
        std::set<NoteRef> bandBase;
        juce::Rectangle<float> band;
        float pitchDelta = 0.0f;
        double viewStartAtDown = 0.0;
        juce::String readout;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    std::size_t readyTakes() const;
    Hit findHit(juce::Point<float> p) const;
    float takeX(const TakeInfo& take, double outSample) const noexcept;
    double outSampleAt(const TakeInfo& take, float x) const noexcept;
    float blobHalfHeight() const noexcept { return rowHeight * 0.62f; }
    juce::Rectangle<float> blobBounds(const TakeInfo& take, const dsp::Note& note) const;
    juce::Path blobPath(const TakeInfo& take, const dsp::EditPlan& plan, const dsp::Note& note, float centreY) const;
    void clampView();
    void viewChanged();
    void audition(const NoteRef& ref);
    void showMenu(const Hit& hit, juce::Point<float> at);
    juce::String describe(const NoteRef& ref) const;

    void dragPitch(float dy, bool fine);
    void dragTime(float dx);
    void dragStretch(float dx);
    void dragGlide(juce::Point<float> delta);
    void glideAsSung(const NoteRef& ref);
    void dragBand(juce::Point<float> to);

    void paintGrid(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintTake(juce::Graphics& g, const TakeInfo& take) const;
    void paintAnalysing(juce::Graphics& g, const TakeInfo& take) const;
    void paintNotes(juce::Graphics& g, const TakeInfo& take, const dsp::EditPlan& plan) const;
    void paintRuler(juce::Graphics& g) const;
    void paintKeys(juce::Graphics& g) const;
    void paintEmpty(juce::Graphics& g) const;
    void paintStatus(juce::Graphics& g) const;

    EditController& controller;
    animalaudio::ui::Theme theme;
    Tool tool = Tool::Edit;
    Granularity granularity = Granularity::Note;
    bool follow = true;

    double viewStart = -0.25;       // song seconds at the left edge of the note area
    double pixelsPerSecond = 160.0; // time zoom
    float topMidi = 77.0f;          // MIDI value at the top edge
    float rowHeight = 14.0f;        // pixels per semitone
    std::size_t takesSeen = 0;

    double playhead = 0.0;
    bool playing = false;
    std::uint32_t auditionTake = 0;
    double auditionSample = -1.0;
    double ambientTime = 0.0;

    Hit hover;
    juce::Point<float> hoverPoint{-1.0f, -1.0f};
    DragState drag;
};
} // namespace whale::ui
