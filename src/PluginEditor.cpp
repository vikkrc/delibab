#include "PluginEditor.h"

namespace delibab
{
DelibabEditor::DelibabEditor (DelibabProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      waveform (p),
      layerPanel (p.getState()),
      globalPanel (p.getState()),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    loadButton.onClick = [this] { openFileChooser(); };
    loadButton.setTooltip ("Load an audio file (WAV, AIFF, FLAC, OGG, MP3). You can also drag one onto the window.");
    content.addAndMakeVisible (loadButton);

    embedButton.setColour (juce::ToggleButton::tickColourId, theme::text);
    embedButton.setToggleState (p.getEmbedSample(), juce::dontSendNotification);
    embedButton.setTooltip ("Save the sample inside your project, so it opens on any computer");
    embedButton.onClick = [this] { processor.setEmbedSample (embedButton.getToggleState()); };
    content.addAndMakeVisible (embedButton);

    waveform.onEmptyClick = [this] { openFileChooser(); };
    content.addAndMakeVisible (waveform);

    for (int i = 0; i < kNumLayers; ++i)
    {
        tabs[(size_t) i] = std::make_unique<LayerTab> (p.getState(), i);
        tabs[(size_t) i]->onSelect = [this] (int layer) { selectLayer (layer); };
        content.addAndMakeVisible (*tabs[(size_t) i]);
    }

    content.addAndMakeVisible (layerPanel);
    content.addAndMakeVisible (globalPanel);

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (24);
    keyboard.setScrollButtonsVisible (false);
    content.addAndMakeVisible (keyboard);

    selectLayer (0);
    processor.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 7 / 10, kBaseHeight * 7 / 10, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    startTimerHz (4);
}

DelibabEditor::~DelibabEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DelibabEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void DelibabEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseWidth;
    content.setBounds (0, 0, kBaseWidth, kBaseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));
}

void DelibabEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // Wordmark
    g.setColour (theme::text);
    g.setFont (theme::font (26.0f, true));
    g.drawText (juce::CharPointer_UTF8 ("d\xc3\xa9lib\xc3\xa1" "b"), 16, 12, 140, 40, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText ("granular  v" DELIBAB_VERSION_STRING, 142, 12, 140, 40, juce::Justification::centredLeft);

    g.setFont (theme::font (12.0f));
    g.drawText (editor.statusText, 300, 12, 470, 40, juce::Justification::centredRight);

    if (editor.dragHover)
    {
        g.setColour (theme::layer (0).withAlpha (0.12f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::layer (0));
        g.drawRect (getLocalBounds(), 3);
    }
}

void DelibabEditor::Content::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto header = r.removeFromTop (40);
    editor.loadButton.setBounds (header.removeFromRight (130).reduced (0, 5));
    header.removeFromRight (8);
    editor.embedButton.setBounds (header.removeFromRight (78).reduced (0, 8));
    r.removeFromTop (8);

    editor.waveform.setBounds (r.removeFromTop (210));
    r.removeFromTop (10);

    auto tabRow = r.removeFromTop (44);
    const int tabW = (tabRow.getWidth() - 8 * (kNumLayers - 1)) / kNumLayers;
    for (auto& tab : editor.tabs)
    {
        tab->setBounds (tabRow.removeFromLeft (tabW));
        tabRow.removeFromLeft (8);
    }
    r.removeFromTop (8);

    editor.keyboard.setBounds (r.removeFromBottom (62));
    // 50 white keys from C1 to C8: fill the width exactly.
    editor.keyboard.setKeyWidth ((float) editor.keyboard.getWidth() / 50.0f);
    r.removeFromBottom (8);
    editor.globalPanel.setBounds (r.removeFromBottom (108));
    r.removeFromBottom (8);
    editor.layerPanel.setBounds (r);
}

void DelibabEditor::selectLayer (int layer)
{
    selectedLayer = layer;
    for (int i = 0; i < kNumLayers; ++i)
        tabs[(size_t) i]->setSelected (i == layer);
    layerPanel.setLayer (layer);
    waveform.setSelectedLayer (layer);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::layer (layer).withAlpha (0.8f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::layer (layer).withAlpha (0.3f));
    keyboard.repaint();
}

void DelibabEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    waveform.sampleChanged();
    embedButton.setToggleState (processor.getEmbedSample(), juce::dontSendNotification);
}

void DelibabEditor::timerCallback()
{
    const auto text = juce::String (processor.getBpm(), 1) + " bpm    "
                    + juce::String (processor.getEngine().getActiveVoiceCount()) + " voices    "
                    + juce::String (processor.getEngine().getActiveGrainCount()) + " grains";
    if (text != statusText)
    {
        statusText = text;
        content.repaint (300, 12, 470, 40);
    }
}

void DelibabEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  processor.loadSampleAsync (file);
                          });
}

bool DelibabEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

void DelibabEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    for (const auto& f : files)
        if (isInterestedInFileDrag ({ f }))
        {
            processor.loadSampleAsync (juce::File (f));
            break;
        }
}

juce::Image DelibabEditor::snapshot()
{
    return content.createComponentSnapshot (content.getLocalBounds(), true, 1.0f);
}

} // namespace delibab
