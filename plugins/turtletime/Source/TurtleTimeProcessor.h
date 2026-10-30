#pragma once

#include "dsp/TurtleTimeEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace turtletime
{
namespace ids
{
inline const juce::ParameterID pattern{"pattern", 1};
inline const juce::ParameterID mix{"mix", 1};
inline const juce::ParameterID keepPitch{"keepPitch", 1};
} // namespace ids

class TurtleTimeProcessor : public juce::AudioProcessor
{
public:
    TurtleTimeProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Turtle Time"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return dsp::kNumPatterns; }
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    float getPlayheadPhase() const noexcept { return engine.getPlayheadPhase(); }
    float getReadMarker() const noexcept { return engine.getReadMarker(); }
    bool isHostSynced() const noexcept { return engine.isHostSynced(); }
    const dsp::TurtleTimeEngine& getEngine() const noexcept { return engine; }

private:
    void pushParameters() noexcept;

    dsp::TurtleTimeEngine engine;
    std::atomic<float>* patternParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* keepPitchParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TurtleTimeProcessor)
};
} // namespace turtletime
