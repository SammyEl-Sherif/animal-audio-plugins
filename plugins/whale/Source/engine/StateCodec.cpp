#include "StateCodec.h"

#include <cmath>
#include <cstring>

namespace whale::state
{
namespace
{
constexpr char kMagic[4] = {'W', 'H', 'L', '1'};
constexpr int kBlock = 1 << 15;
constexpr double kGrid = 8388608.0;

const juce::Identifier kRoot{"WhaleState"};
const juce::Identifier kTakeAudio{"TakeAudio"};
} // namespace

juce::MemoryBlock encodeFlac(const TakeAudio& audio)
{
    juce::MemoryBlock block;
    juce::FlacAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(block, false);
    const int channels = static_cast<int>(audio.channels.size());
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate(audio.rate)
                                                     .withNumChannels(channels)
                                                     .withBitsPerSample(24)
                                                     .withQualityOptionIndex(5));
    if (writer == nullptr)
        return {};
    const double scale = kGrid / static_cast<double>(audio.storageGain);
    std::vector<std::vector<int>> ints(static_cast<std::size_t>(channels), std::vector<int>(kBlock));
    std::vector<const int*> pointers(static_cast<std::size_t>(channels) + 1, nullptr);
    for (std::int64_t at = 0; at < audio.length(); at += kBlock)
    {
        const auto n = static_cast<int>(std::min<std::int64_t>(kBlock, audio.length() - at));
        for (int c = 0; c < channels; ++c)
        {
            const auto& source = audio.channels[static_cast<std::size_t>(c)];
            auto& dest = ints[static_cast<std::size_t>(c)];
            for (int i = 0; i < n; ++i)
            {
                const auto k = std::clamp<long long>(std::llround(source[static_cast<std::size_t>(at + i)] * scale),
                                                     -8388608, 8388607);
                dest[static_cast<std::size_t>(i)] = static_cast<int>(k * 256); // left-justified 32-bit
            }
            pointers[static_cast<std::size_t>(c)] = dest.data();
        }
        if (!writer->write(pointers.data(), n))
            return {};
    }
    writer.reset(); // flushes into `block`
    return block;
}

std::shared_ptr<TakeAudio> decodeFlac(const void* data, std::size_t size, float storageGain)
{
    juce::FlacAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(
        format.createReaderFor(new juce::MemoryInputStream(data, size, false), true));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples <= 0)
        return nullptr;
    const auto channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2));
    const auto length = reader->lengthInSamples;
    std::vector<std::vector<float>> out(static_cast<std::size_t>(channels),
                                        std::vector<float>(static_cast<std::size_t>(length)));
    std::vector<std::vector<int>> ints(static_cast<std::size_t>(channels), std::vector<int>(kBlock));
    std::vector<int*> pointers(static_cast<std::size_t>(channels), nullptr);
    for (int c = 0; c < channels; ++c)
        pointers[static_cast<std::size_t>(c)] = ints[static_cast<std::size_t>(c)].data();
    const double gain = storageGain;
    for (juce::int64 at = 0; at < length; at += kBlock)
    {
        const auto n = static_cast<int>(std::min<juce::int64>(kBlock, length - at));
        if (!reader->read(pointers.data(), channels, at, n, false))
            return nullptr;
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
            {
                const int k = ints[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] / 256;
                out[static_cast<std::size_t>(c)][static_cast<std::size_t>(at + i)] =
                    static_cast<float>(static_cast<double>(k) / kGrid * gain);
            }
    }
    return TakeAudio::fromChannels(std::move(out), reader->sampleRate, storageGain);
}

juce::MemoryBlock write(const juce::ValueTree& parameters, const Session& session, const FlacSource& flacFor)
{
    juce::ValueTree root(kRoot);
    root.appendChild(parameters.createCopy(), nullptr);
    root.appendChild(session.toValueTree(), nullptr);
    std::vector<juce::MemoryBlock> flacs;
    for (const auto& audio : session.takeAudio())
    {
        juce::ValueTree info(kTakeAudio);
        info.setProperty("gain", audio != nullptr ? audio->storageGain : 1.0f, nullptr);
        root.appendChild(info, nullptr);
        if (audio == nullptr)
            flacs.emplace_back();
        else
            flacs.push_back(flacFor ? flacFor(audio) : encodeFlac(*audio));
    }
    const auto xml = root.toXmlString().toStdString();

    juce::MemoryOutputStream out;
    out.write(kMagic, sizeof(kMagic));
    out.writeInt(static_cast<int>(xml.size()));
    out.write(xml.data(), xml.size());
    for (const auto& flac : flacs)
    {
        out.writeInt(static_cast<int>(flac.getSize()));
        out.write(flac.getData(), flac.getSize());
    }
    return out.getMemoryBlock();
}

bool read(const void* data, int size, Loaded& result)
{
    if (data == nullptr || size < 8 || std::memcmp(data, kMagic, sizeof(kMagic)) != 0)
        return false;
    juce::MemoryInputStream in(data, static_cast<std::size_t>(size), false);
    in.skipNextBytes(sizeof(kMagic));
    const int xmlSize = in.readInt();
    if (xmlSize <= 0 || xmlSize > size - 8)
        return false;
    juce::MemoryBlock xmlBytes;
    in.readIntoMemoryBlock(xmlBytes, xmlSize);
    const auto root = juce::ValueTree::fromXml(xmlBytes.toString());
    if (!root.hasType(kRoot))
        return false;

    result = {};
    for (const auto& child : root)
    {
        if (child.hasType(kTakeAudio))
        {
            const int flacSize = in.readInt();
            if (flacSize <= 0 || in.getNumBytesRemaining() < flacSize)
                return false;
            juce::MemoryBlock flac;
            in.readIntoMemoryBlock(flac, flacSize);
            result.takes.push_back(decodeFlac(flac.getData(), flac.getSize(), child.getProperty("gain", 1.0f)));
        }
        else if (child.getType().toString() == "Session")
            result.session = child;
        else
            result.parameters = child;
    }
    return true;
}
} // namespace whale::state
