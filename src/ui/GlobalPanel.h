#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace delibab
{
// Controls shared by all layers: amp envelope, scale, effects, output.
class GlobalPanel final : public juce::Component
{
public:
    explicit GlobalPanel (juce::AudioProcessorValueTreeState& state);
    void resized() override;

private:
    Knob attack { "Attack" }, decay { "Decay" }, sustain { "Sustain" }, release { "Release" };
    Knob root { "Root" };
    ChoiceBox key { "Key" }, scale { "Scale" };
    Knob dlyMix { "Mix" }, dlyFb { "Feedback" };
    ChoiceBox dlyTime { "Time" };
    Knob revMix { "Mix" }, revSize { "Size" }, revDamp { "Damping" };
    Knob master { "Master" };

    Group ampGroup { "Amp" }, scaleGroup { "Scale" }, delayGroup { "Delay" },
          reverbGroup { "Reverb" }, outGroup { "Out" };
};

} // namespace delibab
