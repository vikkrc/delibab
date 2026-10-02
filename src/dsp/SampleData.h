#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace delibab
{
// An immutable, analysed audio sample. Created off the audio thread, then
// handed to the audio thread by reference-counted pointer. Never modified
// after creation, so any thread may read it freely.
class SampleData final : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    // Zero padding on both sides of the audio, so the interpolator can read a
    // few samples past either end without bounds checks.
    static constexpr int kPad = 4;
    static constexpr int kOverviewSize = 2048;
    static constexpr double kMaxSeconds = 600.0;     // longer files are truncated
    static constexpr double kMaxEmbedSeconds = 180.0; // longer files are saved by path only

    static Ptr loadFromFile (const juce::File& file, juce::String& error);
    static Ptr loadFromFlacMemory (const juce::MemoryBlock& flac, const juce::String& name,
                                   const juce::File& originalFile, juce::String& error);
    static Ptr fromBuffer (const juce::AudioBuffer<float>& audio, double sampleRate,
                           const juce::String& name);

    int getNumChannels() const noexcept     { return numChannels; }
    int getLength() const noexcept          { return length; }
    double getSampleRate() const noexcept   { return sampleRate; }
    double getLengthSeconds() const noexcept { return length / sampleRate; }

    // Valid indices: [-kPad, length + kPad)
    const float* getChannel (int ch) const noexcept
    {
        return padded.getReadPointer (juce::jmin (ch, numChannels - 1)) + kPad;
    }

    const juce::String& getName() const noexcept         { return name; }
    const juce::File& getFile() const noexcept           { return file; }
    const juce::MemoryBlock& getEmbeddedFlac() const noexcept { return embeddedFlac; }

    // Min/max pairs of the mono mix, kOverviewSize entries, for drawing.
    const std::vector<std::pair<float, float>>& getOverview() const noexcept { return overview; }
    // Detected transient positions in frames, ascending, always starts with 0.
    const std::vector<int>& getOnsets() const noexcept { return onsets; }

    // Index of the last onset at or before frame (binary search).
    int onsetAtOrBefore (double frame) const noexcept;

private:
    SampleData() = default;
    static Ptr build (juce::AudioFormatReader& reader, const juce::String& name,
                      const juce::File& file, juce::String& error);
    void analyse();
    void encodeEmbedded();

    juce::AudioBuffer<float> padded;
    int numChannels = 0;
    int length = 0;
    double sampleRate = 44100.0;
    juce::String name;
    juce::File file;
    juce::MemoryBlock embeddedFlac;
    std::vector<std::pair<float, float>> overview;
    std::vector<int> onsets;
};

} // namespace delibab
