#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace delibab
{
// Tempo-synced stereo delay with damped feedback (slight ping-pong).
class StereoDelay
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    // timeSamples may change every block; it is smoothed internally.
    void process (juce::AudioBuffer<float>& buffer, int numSamples,
                  double timeSamples, float feedback, float mix);

private:
    std::vector<float> lineL, lineR;
    int writePos = 0;
    double sr = 44100.0;
    juce::SmoothedValue<double> time;
    juce::SmoothedValue<float> mixSmoothed;
    float dampL = 0.0f, dampR = 0.0f;
    bool isClear = true;
};

// Final output stage: master gain plus a soft safety limiter so that dense
// grain clouds never hard-clip the host.
class OutputStage
{
public:
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& buffer, int numSamples, float gainDb);

private:
    juce::SmoothedValue<float> gain;
};

} // namespace delibab
