#pragma once

#include "NativeWindowSupport.h"
#include "TestKit.h"

#include <atomic>
#include <functional>
#include <thread>

/**
 * Tests that drive the editor the way a host does: the editor is put on the desktop (a real native
 * window, so run under xvfb on Linux) and mouse events are injected through ComponentPeer, so they
 * travel the whole JUCE path: peer -> component under the mouse (hit-testing, z-order, transforms) ->
 * Button mouseDown/mouseUp -> onClick -> parameter -> host notification -> attachment -> pill state.
 * Nothing calls onClick or triggerClick.
 */
namespace animalaudio::testing::realmouse
{
inline void pump(int milliseconds = 30)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

enum class ScaleKind
{
    None,
    EditorScale, // AudioProcessorEditor::setScaleFactor (VST3 content scale)
    DesktopScale // Desktop::setGlobalScaleFactor (OS/UI scale)
};

/** Moves the real OS pointer to a raw (physical pixel) screen position. */
inline void putOsPointerAt(juce::Point<float> rawScreenPosition)
{
    juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(
        rawScreenPosition / juce::Desktop::getInstance().getGlobalScaleFactor());
}

/** The native window that hosts a plugin editor. Like JUCE's VST3 wrapper it is a plain component on the desktop
    with the editor as its child, so the host scale (AudioProcessorEditor::setScaleFactor) is a child transform. */
class HostWindow : public juce::Component
{
public:
    explicit HostWindow(juce::AudioProcessorEditor& child)
        : editor(child)
    {
        addAndMakeVisible(editor);
        setOpaque(true);
    }

    void fitToEditor() { setSize(editor.getBoundsInParent().getRight(), editor.getBoundsInParent().getBottom()); }

private:
    juce::AudioProcessorEditor& editor;
};

/** An editor in a native window, optionally scaled the way hosts scale plugin UIs. */
template <class E>
struct DesktopEditor
{
    template <class P>
    DesktopEditor(P& processor, ScaleKind kind, float scale)
        : scaleKind(kind)
    {
        if (kind == ScaleKind::DesktopScale)
            juce::Desktop::getInstance().setGlobalScaleFactor(scale);

        editor.reset(static_cast<E*>(processor.createEditor()));
        window = std::make_unique<HostWindow>(*editor);
        if (kind == ScaleKind::EditorScale)
            editor->setScaleFactor(scale);
        editor->setTopLeftPosition(0, 0);
        window->fitToEditor();

        // macOS only delivers a click to the window that is topmost at that screen point, and a test process
        // opens behind whatever is on screen (terminal, CI runner dialogs): float above everything.
        window->setAlwaysOnTop(true);
        window->addToDesktop(0);
        window->setTopLeftPosition(60, 60);
        window->setVisible(true);
        window->toFront(false);
        pump(50);
        peer = window->getPeer();
        REQUIRE(peer != nullptr);
    }

    ~DesktopEditor()
    {
        window.reset();
        editor.reset();
        if (scaleKind == ScaleKind::DesktopScale)
            juce::Desktop::getInstance().setGlobalScaleFactor(1.0f);
    }

    /** Where `point` (in `component`'s own coordinates) is inside the native window, in the physical pixels the OS reports. */
    juce::Point<float> peerPositionOf(juce::Component& component, juce::Point<float> point) const
    {
        return window->getLocalPoint(&component, point) * juce::Desktop::getInstance().getGlobalScaleFactor();
    }

    void send(juce::Point<float> peerPosition, bool leftDown)
    {
        if (osPointerFollows)
            putOsPointerAt(peer->localToGlobal(peerPosition));
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, peerPosition,
                               leftDown ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(),
                               juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                               juce::Time::currentTimeMillis());
        pump(10);
    }

    /** A full physical click: move there, press, release (mouse up is where JUCE buttons fire).
        `duringPress` runs while the button is held, e.g. to make the host take or lose keyboard focus.

        The OS pointer follows the synthetic one by default, as it does with a real mouse. Several JUCE
        components (Button::updateState, Component::isMouseOver) poll the OS pointer instead of using the
        event position, so a pointer left elsewhere is a different scenario, covered on purpose by
        `osPointerFollows = false`. */
    void click(juce::Point<float> peerPosition, const std::function<void()>& duringPress = {})
    {
        send(peerPosition, false);
        send(peerPosition, true);
        if (duringPress)
        {
            duringPress();
            pump(10);
        }
        send(peerPosition, false);
        pump(20);
    }

    void clickCentreOf(juce::Component& component)
    {
        click(peerPositionOf(component, component.getLocalBounds().toFloat().getCentre()));
    }

    void parkMouse() { send({2.0f, 2.0f}, false); }

    std::unique_ptr<E> editor;
    std::unique_ptr<HostWindow> window;
    juce::ComponentPeer* peer = nullptr;
    ScaleKind scaleKind;
    bool osPointerFollows = true;
};

struct Scenario
{
    ScaleKind kind;
    float scale;
};

inline std::vector<Scenario> scenarios()
{
    using K = ScaleKind;
    return {{K::None, 1.0f},        {K::EditorScale, 1.25f}, {K::EditorScale, 1.5f},
            {K::EditorScale, 2.0f}, {K::DesktopScale, 1.5f}, {K::DesktopScale, 2.0f}};
}

inline juce::String describe(const Scenario& s)
{
    using K = ScaleKind;
    const char* kind = s.kind == K::None ? "unscaled" : s.kind == K::EditorScale ? "editor scale" : "desktop scale";
    return juce::String(kind) + " " + juce::String(s.scale);
}

/** Colour at the left cap of a pill as actually painted (not the toggle state): accent when lit. */
inline juce::Colour paintedPillColour(juce::Button& pill)
{
    const auto image = pill.createComponentSnapshot(pill.getLocalBounds(), true, 1.0f);
    return image.getPixelAt(juce::roundToInt(static_cast<float>(pill.getWidth()) * 0.15f), pill.getHeight() / 2);
}

inline float colourDistance(juce::Colour a, juce::Colour b)
{
    return std::abs(a.getFloatRed() - b.getFloatRed()) + std::abs(a.getFloatGreen() - b.getFloatGreen()) +
           std::abs(a.getFloatBlue() - b.getFloatBlue());
}

/** Every observable view of the selection must agree: parameter, pill state, painted pill, DSP. */
template <class E>
void requireSelectionEverywhere(juce::AudioProcessorValueTreeState& apvts, E& editor, int expected)
{
    REQUIRE(divisionIndex(apvts) == expected);
    REQUIRE(static_cast<int>(apvts.getRawParameterValue("division")->load()) == expected);

    const auto& palette = editor.getTheme().palette;
    for (int i = 0; i < dsp::kNumDivisions; ++i)
    {
        INFO("pill " << i << ", expected selected " << expected);
        auto& pill = editor.getDivisionButton(i);
        REQUIRE(pill.getToggleState() == (i == expected));
        const auto painted = paintedPillColour(pill);
        REQUIRE(colourDistance(painted, i == expected ? palette.accent : palette.panelRaised) < 0.12f);
    }
}

/** Gain the DSP applies at a known host position: tells which division the audio thread is really using. */
template <class P>
struct DspProbe
{
    P& processor;
    FakePlayHead playHead;
    juce::MidiBuffer midi;

    explicit DspProbe(P& p)
        : processor(p)
    {
        processor.setPlayHead(&playHead);
        processor.setPlayConfigDetails(2, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        processor.apvts.getParameter("mix")->setValueNotifyingHost(1.0f);

        juce::AudioPlayHead::PositionInfo info;
        info.setIsPlaying(true);
        info.setBpm(120.0);
        info.setPpqPosition(0.3);
        playHead.position = info;
    }

    /** Division index implied by the gain at ppq 0.3 (cycle phases 0.6 / 0.3 / 0.15 / 0.075 for 1/8..1/1). */
    int divisionInUse()
    {
        processor.getCurveState().setCurve(dsp::ShapeCurve(), -1); // straight ramp: gain == phase
        juce::AudioBuffer<float> buffer(2, 256);
        for (int i = 0; i < 6; ++i) // let the declick smoother settle
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 1.0f, 256);
            processor.processBlock(buffer, midi);
        }
        const float gain = buffer.getSample(0, 255);
        const double beats[] = {0.5, 1.0, 2.0, 4.0};
        int best = 0;
        double bestError = 1.0e9;
        for (int i = 0; i < 4; ++i)
        {
            const double phase = std::fmod(0.3 / beats[i], 1.0);
            const double error = std::abs(static_cast<double>(gain) - phase);
            if (error < bestError)
            {
                bestError = error;
                best = i;
            }
        }
        REQUIRE(bestError < 0.06);
        return best;
    }
};

template <class P, class E>
void clickingPaintedCentreSelectsThatDivision()
{
    if (!prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;

    for (const auto& scenario : scenarios())
    {
        INFO(describe(scenario).toStdString());
        P processor;
        DspProbe<P> dspProbe(processor);
        DesktopEditor<E> window(processor, scenario.kind, scenario.scale);
        auto& editor = *window.editor;

        requireSelectionEverywhere(processor.apvts, editor, 1); // default 1/4

        const std::vector<int> order = {0, 1, 2, 3, 3, 2, 1, 0, 2, 0, 3, 1, 1, 2};
        for (int target : order)
        {
            INFO("clicking pill " << target);
            window.clickCentreOf(editor.getDivisionButton(target));
            window.parkMouse();
            requireSelectionEverywhere(processor.apvts, editor, target);
            REQUIRE(dspProbe.divisionInUse() == target);
        }

        // every starting selection x every target
        for (int from = 0; from < dsp::kNumDivisions; ++from)
        {
            for (int to = 0; to < dsp::kNumDivisions; ++to)
            {
                setDivisionFromHost(processor.apvts, from);
                pump();
                window.clickCentreOf(editor.getDivisionButton(to));
                window.parkMouse();
                INFO("from " << from << " to " << to);
                requireSelectionEverywhere(processor.apvts, editor, to);
                REQUIRE(dspProbe.divisionInUse() == to);
            }
        }
    }
}

/** Clicks across the whole pill row (left cap, middle, right cap of each pill) and in the gaps. */
template <class P, class E>
void clickingAnywhereOnAPillSelectsIt()
{
    if (!prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;

    for (const auto& scenario : scenarios())
    {
        INFO(describe(scenario).toStdString());
        P processor;
        DesktopEditor<E> window(processor, scenario.kind, scenario.scale);
        auto& editor = *window.editor;

        for (int target = 0; target < dsp::kNumDivisions; ++target)
        {
            auto& pill = editor.getDivisionButton(target);
            const auto b = pill.getLocalBounds().toFloat();
            for (float fx : {0.12f, 0.3f, 0.5f, 0.7f, 0.88f})
            {
                setDivisionFromHost(processor.apvts, (target + 2) % dsp::kNumDivisions);
                pump();
                INFO("pill " << target << " at x fraction " << fx);
                window.click(window.peerPositionOf(pill, {b.getX() + b.getWidth() * fx, b.getCentreY()}));
                window.parkMouse();
                requireSelectionEverywhere(processor.apvts, editor, target);
            }
        }

        // the component under the pointer is the pill that was painted there, not an overlay
        for (int target = 0; target < dsp::kNumDivisions; ++target)
        {
            auto& pill = editor.getDivisionButton(target);
            auto* under = editor.getComponentAt(editor.getLocalPoint(&pill, pill.getLocalBounds().getCentre()));
            REQUIRE(under != nullptr);
            REQUIRE((under == &pill || pill.isParentOf(under)));
        }
    }
}

/** Neighbours and the already-selected pill: each pill clicked while every possible pill is selected. */
template <class P, class E>
void selectedPillAndBothNeighbours()
{
    if (!prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;

    for (const auto& scenario : scenarios())
    {
        INFO(describe(scenario).toStdString());
        P processor;
        DspProbe<P> dspProbe(processor);
        DesktopEditor<E> window(processor, scenario.kind, scenario.scale);
        auto& editor = *window.editor;

        for (int selected = 0; selected < dsp::kNumDivisions; ++selected)
        {
            for (int offset : {0, -1, +1})
            {
                const int clicked = selected + offset;
                if (clicked < 0 || clicked >= dsp::kNumDivisions)
                    continue;
                INFO("selected " << selected << ", clicking " << clicked);
                setDivisionFromHost(processor.apvts, selected);
                pump();
                requireSelectionEverywhere(processor.apvts, editor, selected);

                window.clickCentreOf(editor.getDivisionButton(clicked));
                window.parkMouse();

                // clicking the selected pill keeps it; clicking a neighbour lands exactly on that neighbour
                requireSelectionEverywhere(processor.apvts, editor, clicked);
                REQUIRE(dspProbe.divisionInUse() == clicked);
            }
        }
    }
}

/** The OS pointer is not where the events say (hosts that virtualise DPI coordinates, remote desktops, a
    focus change in mid-click): the click must still act on the pill the event hit. */
template <class P, class E>
void clickDoesNotDependOnPolledPointerState()
{
    if (!prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;

    for (const auto& scenario : scenarios())
    {
        INFO(describe(scenario).toStdString());
        P processor;
        DspProbe<P> dspProbe(processor);
        DesktopEditor<E> window(processor, scenario.kind, scenario.scale);
        window.osPointerFollows = false;
        auto& editor = *window.editor;
        putOsPointerAt({2.0f, 2.0f});
        pump();

        for (int from = 0; from < dsp::kNumDivisions; ++from)
        {
            for (int to = 0; to < dsp::kNumDivisions; ++to)
            {
                INFO("from " << from << " to " << to);
                setDivisionFromHost(processor.apvts, from);
                pump();
                auto& pill = editor.getDivisionButton(to);

                window.click(window.peerPositionOf(pill, pill.getLocalBounds().toFloat().getCentre()),
                             [&]
                             {
                                 // keyboard focus moving mid-click makes JUCE buttons re-read the OS pointer
                                 editor.getDivisionButton((to + 1) % dsp::kNumDivisions).grabKeyboardFocus();
                                 pill.grabKeyboardFocus();
                             });
                window.parkMouse();
                requireSelectionEverywhere(processor.apvts, editor, to);
                REQUIRE(dspProbe.divisionInUse() == to);
            }
        }
    }
}

/** Host-side changes (automation on the audio thread, state restore, stale echoes) vs the highlight. */
template <class P, class E>
void highlightFollowsHostSidePaths()
{
    if (!prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;

    for (const auto& scenario : scenarios())
    {
        INFO(describe(scenario).toStdString());
        P processor;
        DspProbe<P> dspProbe(processor);
        DesktopEditor<E> window(processor, scenario.kind, scenario.scale);
        auto& editor = *window.editor;
        auto* param = processor.apvts.getParameter("division");

        // automation written from another thread, as a host does while playing
        for (int target : {0, 3, 1, 2, 2, 0})
        {
            std::thread audioThread(
                [&] { param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(target))); });
            audioThread.join();
            pump();
            requireSelectionEverywhere(processor.apvts, editor, target);
            REQUIRE(dspProbe.divisionInUse() == target);
        }

        // a click followed by a host echo of the new value (and by a stale value, which then wins consistently)
        window.clickCentreOf(editor.getDivisionButton(1));
        std::thread([&] { param->setValueNotifyingHost(param->convertTo0to1(1.0f)); }).join();
        pump();
        window.parkMouse();
        requireSelectionEverywhere(processor.apvts, editor, 1);

        std::thread([&] { param->setValueNotifyingHost(param->convertTo0to1(3.0f)); }).join();
        pump();
        requireSelectionEverywhere(processor.apvts, editor, 3);
        REQUIRE(dspProbe.divisionInUse() == 3);

        // click right after an off-thread change, before the async update ran
        std::thread([&] { param->setValueNotifyingHost(param->convertTo0to1(0.0f)); }).join();
        window.clickCentreOf(editor.getDivisionButton(2));
        window.parkMouse();
        requireSelectionEverywhere(processor.apvts, editor, 2);

        // DAW "reset to default" and gesture-style edits
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->getDefaultValue());
        param->endChangeGesture();
        pump();
        requireSelectionEverywhere(processor.apvts, editor, 1);
        REQUIRE(dspProbe.divisionInUse() == 1);

        // state restore while the editor is open
        for (int i = 0; i < dsp::kNumDivisions; ++i)
        {
            juce::MemoryBlock saved;
            {
                P source;
                setDivisionFromHost(source.apvts, i);
                source.getStateInformation(saved);
            }
            processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            pump();
            requireSelectionEverywhere(processor.apvts, editor, i);
            REQUIRE(dspProbe.divisionInUse() == i);
        }
    }
}

/** Closing and reopening the editor (hosts do this constantly) must show the stored selection. */
template <class P, class E>
void reopenedEditorShowsStoredSelection()
{
    if (!prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;
    P processor;
    for (int i = 0; i < dsp::kNumDivisions; ++i)
    {
        setDivisionFromHost(processor.apvts, i);
        DesktopEditor<E> window(processor, ScaleKind::None, 1.0f);
        requireSelectionEverywhere(processor.apvts, *window.editor, i);
    }
}
} // namespace animalaudio::testing::realmouse

#define AA_REAL_MOUSE_PLUGIN_TESTS(Processor, Editor, label)                                                           \
    TEST_CASE(label ": real mouse: clicking a pill's painted centre selects that division")                            \
    {                                                                                                                  \
        animalaudio::testing::realmouse::clickingPaintedCentreSelectsThatDivision<Processor, Editor>();                \
    }                                                                                                                  \
    TEST_CASE(label ": real mouse: clicking anywhere on a pill selects it")                                            \
    {                                                                                                                  \
        animalaudio::testing::realmouse::clickingAnywhereOnAPillSelectsIt<Processor, Editor>();                        \
    }                                                                                                                  \
    TEST_CASE(label ": real mouse: the selected pill and both neighbours")                                             \
    {                                                                                                                  \
        animalaudio::testing::realmouse::selectedPillAndBothNeighbours<Processor, Editor>();                           \
    }                                                                                                                  \
    TEST_CASE(label ": real mouse: a click does not depend on the polled OS pointer")                                  \
    {                                                                                                                  \
        animalaudio::testing::realmouse::clickDoesNotDependOnPolledPointerState<Processor, Editor>();                  \
    }                                                                                                                  \
    TEST_CASE(label ": real mouse: highlight follows host automation, echoes and state restore")                       \
    {                                                                                                                  \
        animalaudio::testing::realmouse::highlightFollowsHostSidePaths<Processor, Editor>();                           \
    }                                                                                                                  \
    TEST_CASE(label ": real mouse: reopened editor shows the stored selection")                                        \
    {                                                                                                                  \
        animalaudio::testing::realmouse::reopenedEditorShowsStoredSelection<Processor, Editor>();                      \
    }
