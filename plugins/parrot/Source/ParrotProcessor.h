#pragma once

#include "dsp/ParrotEngine.h"

#include <animalaudio/core/TextFormat.h>

#include <juce_audio_processors/juce_audio_processors.h>

namespace parrot
{
namespace ids
{
inline const juce::ParameterID pitch{"pitch", 1};
inline const juce::ParameterID formant{"formant", 1};
inline const juce::ParameterID fine{"fine", 1};
inline const juce::ParameterID link{"link", 1};
inline const juce::ParameterID mode{"mode", 1};
inline const juce::ParameterID note{"note", 1};
inline const juce::ParameterID glide{"glide", 1};
inline const juce::ParameterID drive{"drive", 1};
inline const juce::ParameterID mix{"mix", 1};
} // namespace ids

/** "+7 st", "-3.25 st", "0 st". */
using animalaudio::core::formatSemitones;

class ParrotProcessor : public juce::AudioProcessor
{
public:
    ParrotProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Parrot"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    const dsp::ParrotEngine& getEngine() const noexcept { return engine; }
    dsp::Mode getMode() const noexcept;

private:
    void pushParameters() noexcept;
    void handleMidi(const juce::MidiBuffer& midi) noexcept;

    dsp::ParrotEngine engine;
    double currentRate = 44100.0;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* fineParam = nullptr;
    std::atomic<float>* linkParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* noteParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParrotProcessor)
};
} // namespace parrot
