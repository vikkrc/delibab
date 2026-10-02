#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"
#include "ui/LayerPanel.h"
#include "ui/GlobalPanel.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace delibab
{
class DelibabEditor final : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1080;
    static constexpr int kBaseHeight = 760;

    explicit DelibabEditor (DelibabProcessor&);
    ~DelibabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    // Renders the editor at its base size (used by the headless test tool).
    juce::Image snapshot();

private:
    // Everything is laid out at a fixed base size and scaled as a whole, so
    // the UI stays proportional and crisp at any window size.
    class Content final : public juce::Component
    {
    public:
        explicit Content (DelibabEditor& e) : editor (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
    private:
        DelibabEditor& editor;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void selectLayer (int layer);
    void openFileChooser();

    DelibabProcessor& processor;
    DelibabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    Content content { *this };
    juce::TextButton loadButton { "Load sample" };
    juce::ToggleButton embedButton { "Embed" };
    WaveformView waveform;
    std::array<std::unique_ptr<LayerTab>, kNumLayers> tabs;
    LayerPanel layerPanel;
    GlobalPanel globalPanel;
    juce::MidiKeyboardComponent keyboard;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String statusText;
    bool dragHover = false;
    int selectedLayer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelibabEditor)
};

} // namespace delibab
