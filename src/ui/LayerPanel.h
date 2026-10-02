#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace delibab
{
// One tab per layer: on/off, name, mode. Clicking selects the layer.
class LayerTab final : public juce::Component
{
public:
    LayerTab (juce::AudioProcessorValueTreeState& state, int layerIndex);

    void setSelected (bool s) { selected = s; repaint(); }
    std::function<void (int)> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int index;
    bool selected = false;
    juce::ToggleButton power;
    juce::ComboBox mode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
};

// All controls for the selected layer.
class LayerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit LayerPanel (juce::AudioProcessorValueTreeState& state);

    void setLayer (int layerIndex);
    void resized() override;

private:
    void timerCallback() override;
    void updateDimming();

    juce::AudioProcessorValueTreeState& state;
    int layer = 0;
    int lastMode = -1;

    Knob size { "Size" }, sizeRand { "Size rand" }, shape { "Shape" };
    Knob density { "Density" };
    ChoiceBox rate { "Rate" };
    Knob steps { "Steps" }, hits { "Hits" }, rotate { "Rotate" }, chance { "Chance" };
    Knob position { "Position" }, spray { "Spray" }, scan { "Scan" };
    Knob pitch { "Pitch" }, fine { "Fine" }, pitchRand { "Pitch rand" }, formant { "Formant" };
    juce::ToggleButton quantize { "Scale" };
    Knob level { "Level" }, pan { "Pan" }, spread { "Spread" }, reverse { "Reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> quantizeAttachment;

    Group grainGroup { "Grain" }, timingGroup { "Timing" }, positionGroup { "Position" },
          pitchGroup { "Pitch" }, mixGroup { "Mix" };
};

} // namespace delibab
