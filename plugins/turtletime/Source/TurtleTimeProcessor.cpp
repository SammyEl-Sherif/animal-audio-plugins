#include "TurtleTimeProcessor.h"

#include "TurtleTimeEditor.h"

#include <animalaudio/core/TransportReader.h>

#include <algorithm>

namespace turtletime
{
namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray patternNames;
    for (const auto& pattern : dsp::kPatterns)
        patternNames.add(juce::String(pattern.name.data(), pattern.name.size()));

    layout.add(
        std::make_unique<juce::AudioParameterChoice>(ids::pattern, "Pattern", patternNames, dsp::kDefaultPattern));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ids::mix, "Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int)
                                         { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue() * 0.01f; })));

    layout.add(std::make_unique<juce::AudioParameterBool>(ids::keepPitch, "Keep Pitch", false));

    return layout;
}
} // namespace

TurtleTimeProcessor::TurtleTimeProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "TurtleTimeState", createParameterLayout())
{
    patternParam = apvts.getRawParameterValue(ids::pattern.getParamID());
    mixParam = apvts.getRawParameterValue(ids::mix.getParamID());
    keepPitchParam = apvts.getRawParameterValue(ids::keepPitch.getParamID());
}

bool TurtleTimeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return output == layouts.getMainInputChannelSet();
}

void TurtleTimeProcessor::pushParameters() noexcept
{
    engine.setPatternIndex(juce::roundToInt(patternParam->load()));
    engine.setMix(mixParam->load());
    engine.setKeepPitch(keepPitchParam->load() >= 0.5f);
}

void TurtleTimeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    pushParameters();
    engine.prepare(sampleRate, samplesPerBlock);
}

int TurtleTimeProcessor::getCurrentProgram()
{
    return juce::roundToInt(patternParam->load());
}

void TurtleTimeProcessor::setCurrentProgram(int index)
{
    if (auto* param = apvts.getParameter(ids::pattern.getParamID()))
        param->setValueNotifyingHost(
            param->convertTo0to1(static_cast<float>(std::clamp(index, 0, dsp::kNumPatterns - 1))));
}

const juce::String TurtleTimeProcessor::getProgramName(int index)
{
    const auto name = dsp::patternAt(index).name;
    return juce::String(name.data(), name.size());
}

void TurtleTimeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    pushParameters();

    const int channels = std::min(buffer.getNumChannels(), dsp::TurtleTimeEngine::kMaxChannels);
    float* pointers[dsp::TurtleTimeEngine::kMaxChannels] = {};
    for (int channel = 0; channel < channels; ++channel)
        pointers[channel] = buffer.getWritePointer(channel);

    engine.process(pointers, channels, buffer.getNumSamples(), animalaudio::core::readTransport(getPlayHead()));
}

void TurtleTimeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void TurtleTimeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* TurtleTimeProcessor::createEditor()
{
    return new TurtleTimeEditor(*this);
}
} // namespace turtletime

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new turtletime::TurtleTimeProcessor();
}
