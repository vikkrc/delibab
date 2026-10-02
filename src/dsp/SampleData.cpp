#include "SampleData.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace delibab
{
SampleData::Ptr SampleData::loadFromFile (const juce::File& f, juce::String& error)
{
    if (! f.existsAsFile())
    {
        error = "File not found: " + f.getFullPathName();
        return nullptr;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + f.getFileName();
        return nullptr;
    }

    return build (*reader, f.getFileNameWithoutExtension(), f, error);
}

SampleData::Ptr SampleData::loadFromFlacMemory (const juce::MemoryBlock& flac, const juce::String& sampleName,
                                                const juce::File& originalFile, juce::String& error)
{
    juce::FlacAudioFormat flacFormat;
    auto stream = std::make_unique<juce::MemoryInputStream> (flac, false);
    std::unique_ptr<juce::AudioFormatReader> reader (flacFormat.createReaderFor (stream.release(), true));

    if (reader == nullptr)
    {
        error = "Could not decode the embedded sample.";
        return nullptr;
    }

    auto result = build (*reader, sampleName, originalFile, error);
    if (result != nullptr)
        result->embeddedFlac = flac; // reuse the original bytes, no need to re-encode
    return result;
}

SampleData::Ptr SampleData::fromBuffer (const juce::AudioBuffer<float>& audio, double sr, const juce::String& sampleName)
{
    Ptr s (new SampleData());
    s->numChannels = juce::jlimit (1, 2, audio.getNumChannels());
    s->length = audio.getNumSamples();
    s->sampleRate = sr;
    s->name = sampleName;
    s->padded.setSize (s->numChannels, s->length + 2 * kPad);
    s->padded.clear();
    for (int ch = 0; ch < s->numChannels; ++ch)
        s->padded.copyFrom (ch, kPad, audio, ch, 0, s->length);
    s->analyse();
    s->encodeEmbedded();
    return s;
}

SampleData::Ptr SampleData::build (juce::AudioFormatReader& reader, const juce::String& sampleName,
                                   const juce::File& f, juce::String& error)
{
    if (reader.sampleRate <= 0 || reader.lengthInSamples <= 0 || reader.numChannels == 0)
    {
        error = "The audio file is empty.";
        return nullptr;
    }

    const auto maxFrames = (juce::int64) (kMaxSeconds * reader.sampleRate);
    const auto frames = (int) juce::jmin (reader.lengthInSamples, maxFrames);

    Ptr s (new SampleData());
    s->numChannels = (int) juce::jmin (2u, reader.numChannels);
    s->length = frames;
    s->sampleRate = reader.sampleRate;
    s->name = sampleName;
    s->file = f;
    s->padded.setSize (s->numChannels, frames + 2 * kPad);
    s->padded.clear();

    // Read straight into the padded buffer, after the leading pad.
    float* dest[2] { s->padded.getWritePointer (0) + kPad,
                     s->padded.getWritePointer (s->numChannels - 1) + kPad };
    if (! reader.read (dest, s->numChannels, 0, frames))
    {
        error = "Failed to read the audio data.";
        return nullptr;
    }

    // Guard against NaN/Inf in malformed files.
    for (int ch = 0; ch < s->numChannels; ++ch)
    {
        auto* d = s->padded.getWritePointer (ch);
        for (int i = 0; i < s->padded.getNumSamples(); ++i)
            if (! std::isfinite (d[i])) d[i] = 0.0f;
    }

    s->analyse();
    s->encodeEmbedded();
    return s;
}

int SampleData::onsetAtOrBefore (double frame) const noexcept
{
    if (onsets.empty())
        return 0;
    auto it = std::upper_bound (onsets.begin(), onsets.end(), (int) frame);
    if (it == onsets.begin())
        return onsets.front();
    return *(it - 1);
}

void SampleData::analyse()
{
    // ---- Mono mix -----------------------------------------------------------
    std::vector<float> mono ((size_t) length);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* src = getChannel (ch);
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += src[i] / (float) numChannels;
    }

    // ---- Overview for drawing -----------------------------------------------
    overview.assign (kOverviewSize, { 0.0f, 0.0f });
    for (int b = 0; b < kOverviewSize; ++b)
    {
        const auto start = (int) ((juce::int64) b * length / kOverviewSize);
        const auto end = juce::jmax (start + 1, (int) ((juce::int64) (b + 1) * length / kOverviewSize));
        float lo = 0.0f, hi = 0.0f;
        for (int i = start; i < juce::jmin (end, length); ++i)
        {
            lo = juce::jmin (lo, mono[(size_t) i]);
            hi = juce::jmax (hi, mono[(size_t) i]);
        }
        overview[(size_t) b] = { lo, hi };
    }

    // ---- Transient detection --------------------------------------------------
    // Energy of the first-difference signal (emphasises attacks) per hop, then
    // positive log-energy flux, peak-picked against a local adaptive threshold.
    const int hop = juce::jmax (64, (int) std::round (sampleRate * 0.0058)); // ~256 @ 44.1k
    const int numHops = length / hop;
    onsets.clear();
    onsets.push_back (0);

    if (numHops > 8)
    {
        std::vector<float> energy ((size_t) numHops);
        float prev = 0.0f;
        for (int h = 0; h < numHops; ++h)
        {
            double e = 0.0;
            for (int i = h * hop; i < (h + 1) * hop; ++i)
            {
                const float d = mono[(size_t) i] - prev;
                prev = mono[(size_t) i];
                e += d * d;
            }
            energy[(size_t) h] = (float) std::log10 (1.0e-9 + e / hop);
        }

        std::vector<float> flux ((size_t) numHops, 0.0f);
        for (int h = 1; h < numHops; ++h)
            flux[(size_t) h] = juce::jmax (0.0f, energy[(size_t) h] - energy[(size_t) h - 1]);

        const int window = 12;
        const int minGapHops = juce::jmax (1, (int) (0.06 * sampleRate / hop));
        int lastOnsetHop = -minGapHops;
        const float peakEnergy = *std::max_element (energy.begin(), energy.end());

        for (int h = 1; h < numHops - 1; ++h)
        {
            const int a = juce::jmax (0, h - window), b = juce::jmin (numHops - 1, h + window);
            float mean = 0.0f;
            for (int k = a; k <= b; ++k) mean += flux[(size_t) k];
            mean /= (float) (b - a + 1);

            const bool isPeak = flux[(size_t) h] >= flux[(size_t) h - 1] && flux[(size_t) h] > flux[(size_t) h + 1];
            const bool loudEnough = energy[(size_t) h] > peakEnergy - 4.0f; // within 40 dB of the loudest hop

            if (isPeak && loudEnough && flux[(size_t) h] > mean * 1.6f + 0.12f && h - lastOnsetHop >= minGapHops)
            {
                onsets.push_back (juce::jmax (0, (h - 1) * hop));
                lastOnsetHop = h;
            }
        }
    }

    // Fall back to an even 16-step grid when the material has no clear transients.
    if (onsets.size() < 3)
    {
        onsets.clear();
        for (int i = 0; i < 16; ++i)
            onsets.push_back ((int) ((juce::int64) i * length / 16));
    }

    std::sort (onsets.begin(), onsets.end());
    onsets.erase (std::unique (onsets.begin(), onsets.end()), onsets.end());
}

void SampleData::encodeEmbedded()
{
    embeddedFlac.reset();
    if (getLengthSeconds() > kMaxEmbedSeconds)
        return;

    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream> (embeddedFlac, false);

    // FLAC only supports a fixed set of rates up to 655 kHz; all common rates are fine.
    auto writer = flac.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                 .withSampleRate (sampleRate)
                                                 .withNumChannels (numChannels)
                                                 .withBitsPerSample (24));
    if (writer == nullptr)
    {
        embeddedFlac.reset();
        return;
    }

    const float* chans[2] { getChannel (0), getChannel (numChannels - 1) };
    if (! writer->writeFromFloatArrays (chans, numChannels, length))
        embeddedFlac.reset();

    writer.reset(); // flushes into embeddedFlac
}

} // namespace delibab
