#include "Theme.h"

namespace delibab
{
juce::Colour theme::layer (int index)
{
    static const juce::Colour colours[] {
        juce::Colour (0xfff2a541), // amber sun
        juce::Colour (0xffe8604c), // coral
        juce::Colour (0xff4fb7c2), // teal haze
        juce::Colour (0xffae92f7), // violet dusk
    };
    return colours[juce::jlimit (0, 3, index)];
}

juce::Font theme::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// ---- LookAndFeel --------------------------------------------------------------

DelibabLookAndFeel::DelibabLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::ComboBox::backgroundColourId, theme::panelHi);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::edge);
    setColour (juce::ComboBox::arrowColourId, theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, theme::panelHi);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::edge);
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text);
    setColour (juce::TextButton::buttonColourId, theme::panelHi);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, theme::bg);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::BubbleComponent::backgroundColourId, theme::panelHi);
    setColour (juce::BubbleComponent::outlineColourId, theme::edge);
    setColour (juce::TooltipWindow::backgroundColourId, theme::panelHi);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd9d0c0));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, theme::panel);
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::wave);
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::layer (0).withAlpha (0.35f));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::layer (0).withAlpha (0.8f));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, theme::panelHi);
    setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, theme::textDim);
}

void DelibabLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::edge);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc: from the centre for bipolar ranges, from the start otherwise.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = startAngle + zeroPos * (endAngle - startAngle);
    const float a1 = startAngle + pos * (endAngle - startAngle);
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body + pointer
    const float bodyR = arcR - lineW * 1.1f;
    g.setColour (theme::panelHi);
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (bodyR * 0.82f, a1);
    const auto inner = centre.getPointOnCircumference (bodyR * 0.25f, a1);
    g.setColour (theme::text);
    g.drawLine ({ inner, tip }, juce::jmax (1.5f, lineW * 0.55f));
}

void DelibabLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    g.setColour (on ? accent.withAlpha (0.9f) : theme::panelHi.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? accent : theme::edge);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (on ? theme::bg : theme::textDim);
    g.setFont (theme::font (juce::jmin (13.0f, r.getHeight() * 0.62f), true));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1);
}

void DelibabLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = theme::panelHi;
    if (highlighted) c = c.brighter (0.12f);
    if (down) c = c.brighter (0.25f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (theme::edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void DelibabLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font DelibabLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return theme::font (juce::jmin (14.0f, (float) box.getHeight() * 0.58f));
}

juce::Font DelibabLookAndFeel::getPopupMenuFont()
{
    return theme::font (14.0f);
}

juce::Font DelibabLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::font (juce::jmin (14.0f, (float) buttonHeight * 0.55f), true);
}

void DelibabLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ---- Knob ---------------------------------------------------------------------

Knob::Knob (const juce::String& label) : name (label)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, theme::layer (0));
    slider.onHoverChange = [this] { repaint(); };
    slider.onDragStart = [this] { dragging = true; repaint(); };
    slider.onDragEnd = [this] { dragging = false; repaint(); };
    slider.onValueChange = [this] { if (dragging || slider.hovering) repaint(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID)
{
    attachment.reset();
    param = state.getParameter (paramID);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.setTooltip (param->getName (64));
    repaint();
}

void Knob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.repaint();
}

void Knob::setDimmed (bool shouldDim)
{
    setAlpha (shouldDim ? 0.32f : 1.0f);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (16);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void Knob::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().removeFromBottom (16);
    const bool showValue = (dragging || slider.hovering) && param != nullptr;
    g.setColour (showValue ? theme::text : theme::textDim);
    g.setFont (theme::font (12.0f, showValue));
    g.drawFittedText (showValue ? param->getCurrentValueAsText() : name, r, juce::Justification::centred, 1, 0.8f);
}

// ---- ChoiceBox ----------------------------------------------------------------

ChoiceBox::ChoiceBox (const juce::String& label) : name (label)
{
    addAndMakeVisible (box);
}

void ChoiceBox::attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID)
{
    attachment.reset();
    box.clear (juce::dontSendNotification);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID)))
        box.addItemList (choice->choices, 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, box);
}

void ChoiceBox::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (16);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (26, r.getHeight())));
}

void ChoiceBox::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawFittedText (name, getLocalBounds().removeFromBottom (16), juce::Justification::centred, 1);
}

// ---- Group --------------------------------------------------------------------

int Group::getIdealWidth() const
{
    int w = 16;
    for (auto& [c, width] : items)
        w += width;
    return w;
}

void Group::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (22);
    r.removeFromBottom (6);
    for (auto& [c, width] : items)
        c->setBounds (r.removeFromLeft (width));
}

void Group::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (theme::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText (title.toUpperCase(), getLocalBounds().reduced (10, 0).removeFromTop (22), juce::Justification::centredLeft);
}

} // namespace delibab
