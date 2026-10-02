#include "LayerPanel.h"

namespace delibab
{
// ---- LayerTab -----------------------------------------------------------------

LayerTab::LayerTab (juce::AudioProcessorValueTreeState& state, int layerIndex) : index (layerIndex)
{
    power.setButtonText ("ON");
    power.setColour (juce::ToggleButton::tickColourId, theme::layer (index));
    power.setTooltip ("Turn layer " + juce::String (index + 1) + " on or off");
    addAndMakeVisible (power);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ids::layer (index, ids::on), power);

    mode.addItemList (modeNames(), 1);
    mode.setTooltip ("Cloud: free grain clouds.  Pulse: grains on a tempo-synced Euclidean grid.  "
                     "Slice: like Pulse, snapped to transients.  Tonal: audio-rate grains, playable pitch.");
    mode.onChange = [this] { if (onSelect) onSelect (index); };
    addAndMakeVisible (mode);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::layer (index, ids::mode), mode);
}

void LayerTab::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto c = theme::layer (index);
    g.setColour (selected ? theme::panelHi : theme::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (selected ? c : theme::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, selected ? 1.5f : 1.0f);

    g.setColour (selected ? theme::text : theme::textDim);
    g.setFont (theme::font (14.0f, true));
    g.drawText ("Layer " + juce::String (index + 1), getLocalBounds().reduced (12, 0).withTrimmedLeft (46),
                juce::Justification::centredLeft);
}

void LayerTab::resized()
{
    auto r = getLocalBounds().reduced (8, 7);
    power.setBounds (r.removeFromLeft (40));
    mode.setBounds (r.removeFromRight (juce::jmin (100, r.getWidth() / 2)));
}

void LayerTab::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (index);
}

// ---- LayerPanel ---------------------------------------------------------------

LayerPanel::LayerPanel (juce::AudioProcessorValueTreeState& s) : state (s)
{
    constexpr int k = 66; // knob column width

    grainGroup.add (size, k);
    grainGroup.add (sizeRand, k);
    grainGroup.add (shape, k);

    timingGroup.add (density, k);
    timingGroup.add (rate, 76);
    timingGroup.add (steps, k);
    timingGroup.add (hits, k);
    timingGroup.add (rotate, k);
    timingGroup.add (chance, k);

    positionGroup.add (position, k);
    positionGroup.add (spray, k);
    positionGroup.add (scan, k);

    pitchGroup.add (pitch, k);
    pitchGroup.add (fine, k);
    pitchGroup.add (pitchRand, k);
    pitchGroup.add (formant, k);
    pitchGroup.add (quantize, 58);

    mixGroup.add (level, k);
    mixGroup.add (pan, k);
    mixGroup.add (spread, k);
    mixGroup.add (reverse, k);

    for (auto* g : { &grainGroup, &timingGroup, &positionGroup, &pitchGroup, &mixGroup })
        addAndMakeVisible (g);

    density.slider.setTooltip ("Grains per second (Cloud)");
    quantize.setTooltip ("Snap grain pitches to the global scale");

    setLayer (0);
    startTimerHz (10);
}

void LayerPanel::setLayer (int layerIndex)
{
    layer = layerIndex;
    const auto L = [this] (const char* suffix) { return ids::layer (layer, suffix); };

    size.attach (state, L (ids::size));
    sizeRand.attach (state, L (ids::sizeRand));
    shape.attach (state, L (ids::shape));
    density.attach (state, L (ids::density));
    rate.attach (state, L (ids::rate));
    steps.attach (state, L (ids::steps));
    hits.attach (state, L (ids::hits));
    rotate.attach (state, L (ids::rotate));
    chance.attach (state, L (ids::chance));
    position.attach (state, L (ids::position));
    spray.attach (state, L (ids::spray));
    scan.attach (state, L (ids::scan));
    pitch.attach (state, L (ids::pitch));
    fine.attach (state, L (ids::fine));
    pitchRand.attach (state, L (ids::pitchRand));
    formant.attach (state, L (ids::formant));
    level.attach (state, L (ids::level));
    pan.attach (state, L (ids::pan));
    spread.attach (state, L (ids::spread));
    reverse.attach (state, L (ids::reverse));

    quantizeAttachment.reset();
    quantizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, L (ids::quantize), quantize);

    const auto c = theme::layer (layer);
    for (auto* knob : { &size, &sizeRand, &shape, &density, &steps, &hits, &rotate, &chance, &position, &spray,
                        &scan, &pitch, &fine, &pitchRand, &formant, &level, &pan, &spread, &reverse })
        knob->setAccent (c);
    quantize.setColour (juce::ToggleButton::tickColourId, c);

    lastMode = -1;
    updateDimming();
}

void LayerPanel::timerCallback()
{
    updateDimming();
}

void LayerPanel::updateDimming()
{
    const int mode = (int) state.getRawParameterValue (ids::layer (layer, ids::mode))->load();
    if (mode == lastMode)
        return;
    lastMode = mode;

    const auto m = (LayerMode) mode;
    const bool gridMode = m == LayerMode::pulse || m == LayerMode::slice;
    density.setDimmed (m != LayerMode::cloud);
    rate.setDimmed (! gridMode);
    steps.setDimmed (! gridMode);
    hits.setDimmed (! gridMode);
    rotate.setDimmed (! gridMode);
    formant.setDimmed (m != LayerMode::tonal);
}

void LayerPanel::resized()
{
    auto r = getLocalBounds();
    const int rowH = (r.getHeight() - 8) / 2;
    auto row1 = r.removeFromTop (rowH);
    r.removeFromTop (8);
    auto row2 = r;

    const auto place = [] (juce::Rectangle<int>& row, Group& g, bool last)
    {
        g.setBounds (last ? row : row.removeFromLeft (g.getIdealWidth()));
        if (! last) row.removeFromLeft (8);
    };

    place (row1, positionGroup, false);
    place (row1, grainGroup, false);
    place (row1, pitchGroup, true);
    place (row2, timingGroup, false);
    place (row2, mixGroup, true);

    // The scale-lock toggle is a small pill, not a full-height cell.
    quantize.setBounds (quantize.getBounds().withSizeKeepingCentre (54, 24).translated (0, -8));
}

} // namespace delibab
