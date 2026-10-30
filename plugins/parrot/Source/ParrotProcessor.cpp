#include "ParrotProcessor.h"

#include "ParrotEditor.h"

#include <algorithm>
#include <cmath>

namespace parrot
{
namespace
{
juce::String percentText(float value)
{
    return juce::String(juce::roundToInt(value * 100.0f)) + " %";
}

std::unique_ptr<juce::AudioParameterFloat> shiftParameter(const juce::ParameterID& id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat>(
        id, name, juce::NormalisableRange<float>(-dsp::kMaxShift, dsp::kMaxShift, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("st")
            .withStringFromValueFunction([](float value, int) { return formatSemitones(value); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); }));
}

std::unique_ptr<juce::AudioParameterFloat> percentParameter(const juce::ParameterID& id, const juce::String& name,
                                                            float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat>(
        id, name, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), defaultValue,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int) { return percentText(value); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue() * 0.01f; }));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(shiftParameter(ids::pitch, "Pitch"));
    layout.add(shiftParameter(ids::formant, "Formant"));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::fine, "Fine", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::link, "Link", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::mode, "Mode",
                                                            juce::StringArray{"Shift", "Snap", "Robot"}, 0));

    juce::StringArray notes;
    for (int i = 0; i < dsp::kNumNotes; ++i)
        notes.add(dsp::noteName(dsp::kLowestNote + i));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::note, "Robot Note", notes,
                                                            dsp::kDefaultNote - dsp::kLowestNote));

    juce::NormalisableRange<float> glideRange(0.0f, dsp::kMaxGlideMs, 0.1f);
    glideRange.setSkewForCentre(60.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ids::glide, "Glide", glideRange, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int) { return juce::String(juce::roundToInt(value)) + " ms"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));

    layout.add(percentParameter(ids::drive, "Drive", 0.0f));
    layout.add(percentParameter(ids::mix, "Mix", 1.0f));
    return layout;
}
} // namespace

ParrotProcessor::ParrotProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "ParrotState", createParameterLayout())
{
    pitchParam = apvts.getRawParameterValue(ids::pitch.getParamID());
    formantParam = apvts.getRawParameterValue(ids::formant.getParamID());
    fineParam = apvts.getRawParameterValue(ids::fine.getParamID());
    linkParam = apvts.getRawParameterValue(ids::link.getParamID());
    modeParam = apvts.getRawParameterValue(ids::mode.getParamID());
    noteParam = apvts.getRawParameterValue(ids::note.getParamID());
    glideParam = apvts.getRawParameterValue(ids::glide.getParamID());
    driveParam = apvts.getRawParameterValue(ids::drive.getParamID());
    mixParam = apvts.getRawParameterValue(ids::mix.getParamID());

    pushParameters();
    engine.prepare(currentRate);
    setLatencySamples(engine.getLatencySamples());
}

bool ParrotProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return output == layouts.getMainInputChannelSet();
}

dsp::Mode ParrotProcessor::getMode() const noexcept
{
    return static_cast<dsp::Mode>(std::clamp(juce::roundToInt(modeParam->load()), 0, dsp::kNumModes - 1));
}

void ParrotProcessor::pushParameters() noexcept
{
    engine.setPitch(pitchParam->load());
    engine.setFormant(formantParam->load());
    engine.setFine(fineParam->load() > 0.5f);
    engine.setLink(linkParam->load() > 0.5f);
    engine.setMode(getMode());
    engine.setNote(dsp::kLowestNote + std::clamp(juce::roundToInt(noteParam->load()), 0, dsp::kNumNotes - 1));
    engine.setGlide(glideParam->load());
    engine.setDrive(driveParam->load());
    engine.setMix(mixParam->load());
}

void ParrotProcessor::prepareToPlay(double sampleRate, int)
{
    currentRate = sampleRate;
    pushParameters();
    engine.prepare(sampleRate);
    setLatencySamples(engine.getLatencySamples());
}

double ParrotProcessor::getTailLengthSeconds() const
{
    return static_cast<double>(engine.getLatencySamples()) / currentRate + 0.05;
}

void ParrotProcessor::handleMidi(const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            engine.noteOn(message.getNoteNumber());
        else if (message.isNoteOff())
            engine.noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }
}

void ParrotProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    pushParameters();
    handleMidi(midi);

    const int channels = std::min(buffer.getNumChannels(), dsp::ParrotEngine::kMaxChannels);
    float* pointers[dsp::ParrotEngine::kMaxChannels] = {};
    for (int channel = 0; channel < channels; ++channel)
        pointers[channel] = buffer.getWritePointer(channel);
    engine.process(pointers, channels, buffer.getNumSamples());
}

void ParrotProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ParrotProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* ParrotProcessor::createEditor()
{
    return new ParrotEditor(*this);
}
} // namespace parrot

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new parrot::ParrotProcessor();
}
