#pragma once

#include "Session.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

namespace whale::state
{
/**
 * Plugin state: "WHL1", a 32-bit length, the XML (parameters + session), then each take's audio as 24-bit FLAC
 * (32-bit length each). Takes are stored on the 24-bit grid TakeAudio already uses, so a reload gives back the
 * exact samples and therefore the same analysis.
 */
using FlacSource = std::function<const juce::MemoryBlock&(const std::shared_ptr<const TakeAudio>&)>;

/** `flacFor` supplies each take's encoded audio (so a caller can cache it; encodeFlac when empty). */
juce::MemoryBlock write(const juce::ValueTree& parameters, const Session& session, const FlacSource& flacFor = {});

struct Loaded
{
    juce::ValueTree parameters;
    juce::ValueTree session;
    std::vector<std::shared_ptr<const TakeAudio>> takes;
};

/** False if the data is not Whale state (a plain parameter XML from copyXmlToBinary still loads). */
bool read(const void* data, int size, Loaded& out);

juce::MemoryBlock encodeFlac(const TakeAudio& audio);
std::shared_ptr<TakeAudio> decodeFlac(const void* data, std::size_t size, float storageGain);
} // namespace whale::state
