#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace delibab
{
namespace theme
{
    // "Délibáb": a mirage over the Great Plain at dusk. Deep plum night,
    // warm sand text, four heat-haze colours for the layers.
    inline const juce::Colour bg        { 0xff120f19 };
    inline const juce::Colour panel     { 0xff1a1724 };
    inline const juce::Colour panelHi   { 0xff241f31 };
    inline const juce::Colour edge      { 0xff2e2840 };
    inline const juce::Colour text      { 0xffece4d4 };
    inline const juce::Colour textDim   { 0xff8d8499 };
    inline const juce::Colour wave      { 0xff5d5570 };

    juce::Colour layer (int index);

    juce::Font font (float height, bool bold = false);
}

class DelibabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DelibabLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

// A rotary knob with its name underneath. Shows the value while hovered or dragged.
class Knob final : public juce::Component
{
public:
    explicit Knob (const juce::String& label);

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    void setAccent (juce::Colour c);
    void setDimmed (bool shouldDim);

    void resized() override;
    void paint (juce::Graphics&) override;

    struct HoverSlider : juce::Slider
    {
        std::function<void()> onHoverChange;
        void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); hovering = true; if (onHoverChange) onHoverChange(); }
        void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e);  hovering = false; if (onHoverChange) onHoverChange(); }
        bool hovering = false;
    };

    HoverSlider slider;

private:
    juce::String name;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

// A labelled choice box bound to a choice parameter.
class ChoiceBox final : public juce::Component
{
public:
    explicit ChoiceBox (const juce::String& label);
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    void setDimmed (bool shouldDim) { setAlpha (shouldDim ? 0.35f : 1.0f); }
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::ComboBox box;

private:
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// A titled group of controls laid out in a row.
class Group final : public juce::Component
{
public:
    explicit Group (const juce::String& groupTitle) : title (groupTitle) {}
    void add (juce::Component& c, int width) { items.push_back ({ &c, width }); addAndMakeVisible (c); }
    int getIdealWidth() const;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    std::vector<std::pair<juce::Component*, int>> items;
};

} // namespace delibab
