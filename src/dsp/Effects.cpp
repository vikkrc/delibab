#include "Effects.h"
#include <cmath>

namespace delibab
{
void StereoDelay::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const auto size = (size_t) std::ceil (sampleRate * 4.5) + 4; // longest sync time at 40 bpm ~ 6 s is clamped below
    lineL.assign (size, 0.0f);
    lineR.assign (size, 0.0f);
    time.reset (sampleRate, 0.25);
    mixSmoothed.reset (sampleRate, 0.05);
    reset();
}

void StereoDelay::reset()
{
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    writePos = 0;
    dampL = dampR = 0.0f;
}

void StereoDelay::process (juce::AudioBuffer<float>& buffer, int numSamples, double timeSamples, float feedback, float mix)
{
    mixSmoothed.setTargetValue (mix);
    if (mix <= 0.0001f && mixSmoothed.getCurrentValue() <= 0.0001f)
    {
        // Bypassed: clear once so re-enabling doesn't replay stale audio.
        if (! isClear)
        {
            reset();
            isClear = true;
        }
        return;
    }
    isClear = false;

    const auto lineSize = (int) lineL.size();
    timeSamples = juce::jlimit (16.0, (double) lineSize - 4.0, timeSamples);
    time.setTargetValue (timeSamples);
    if (time.getCurrentValue() < 1.0)
        time.setCurrentAndTargetValue (timeSamples);

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    const float damp = 0.35f; // one-pole lowpass amount in the feedback loop

    for (int n = 0; n < numSamples; ++n)
    {
        const double t = time.getNextValue();
        double readPos = writePos - t;
        while (readPos < 0) readPos += lineSize;
        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % lineSize;
        const float frac = (float) (readPos - i0);
        const float dL = lineL[(size_t) i0] + frac * (lineL[(size_t) i1] - lineL[(size_t) i0]);
        const float dR = lineR[(size_t) i0] + frac * (lineR[(size_t) i1] - lineR[(size_t) i0]);

        dampL += damp * (dL - dampL);
        dampR += damp * (dR - dampR);

        // Cross-feed gives a gentle ping-pong.
        lineL[(size_t) writePos] = L[n] + feedback * (0.7f * dampL + 0.3f * dampR);
        lineR[(size_t) writePos] = R[n] + feedback * (0.7f * dampR + 0.3f * dampL);
        if (++writePos >= lineSize) writePos = 0;

        const float m = mixSmoothed.getNextValue();
        L[n] += m * dL;
        R[n] += m * dR;
    }
}

void OutputStage::prepare (double sampleRate)
{
    gain.reset (sampleRate, 0.05);
}

void OutputStage::process (juce::AudioBuffer<float>& buffer, int numSamples, float gainDb)
{
    gain.setTargetValue (juce::Decibels::decibelsToGain (gainDb, -48.0f));

    const auto softLimit = [] (float x)
    {
        constexpr float knee = 0.8f, headroom = 1.0f - knee;
        const float a = std::abs (x);
        if (a <= knee) return x;
        const float y = knee + headroom * std::tanh ((a - knee) / headroom);
        return std::copysign (y, x);
    };

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = gain.getNextValue();
        L[n] = softLimit (L[n] * g);
        R[n] = softLimit (R[n] * g);
    }
}

} // namespace delibab
