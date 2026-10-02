#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace delibab
{
// The centre of the instrument: the waveform, each layer's scan region, and
// every grain drawn as a particle at the moment it is born.
class WaveformView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit WaveformView (DelibabProcessor& p);

    void setSelectedLayer (int layer) { selectedLayer = layer; repaint(); }
    void sampleChanged();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onEmptyClick; // e.g. open the file chooser

private:
    struct Particle
    {
        float x, y, vx, size, life, age;
        int layer;
    };

    void timerCallback() override;
    void rebuildWaveImage();
    void setPositionFromMouse (const juce::MouseEvent&);
    juce::Rectangle<float> plotArea() const;

    DelibabProcessor& processor;
    SampleData::Ptr sample;
    juce::Image waveImage;
    std::vector<Particle> particles;
    int selectedLayer = 0;
    juce::RangedAudioParameter* draggingParam = nullptr;
    juce::uint32 lastFrameMs = 0;
    juce::Random rng;
};

} // namespace delibab
