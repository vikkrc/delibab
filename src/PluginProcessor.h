#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "dsp/GrainEngine.h"
#include "dsp/Effects.h"
#include "dsp/SampleData.h"

namespace delibab
{
class DelibabProcessor final : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::Timer
{
public:
    DelibabProcessor();
    ~DelibabProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Delibab"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Sample management (message thread) ----
    // Loads on a background thread; listeners get a change message when done.
    void loadSampleAsync (const juce::File& file);
    // Loads on the calling thread. Returns false and fills `error` on failure.
    bool loadSampleSync (const juce::File& file, juce::String& error);
    // Installs an already-built sample (used by tests and state restore).
    void setSample (SampleData::Ptr newSample);

    SampleData::Ptr getCurrentSample() const;
    juce::String getLastError() const;
    juce::String getMissingSamplePath() const;
    bool isLoading() const noexcept { return loadsInFlight.load() > 0; }

    bool getEmbedSample() const noexcept { return embedSample.load(); }
    void setEmbedSample (bool shouldEmbed) noexcept { embedSample.store (shouldEmbed); }

    // ---- For the UI ----
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    GrainEngine& getEngine() noexcept { return engine; }
    double getBpm() const noexcept { return bpmForUi.load (std::memory_order_relaxed); }

    // Notes played on the on-screen keyboard are merged into the MIDI input.
    juce::MidiKeyboardState keyboardState;

private:
    void timerCallback() override;
    EngineSnapshot makeSnapshot() const;
    void handleMidi (const juce::MidiMessage& m);
    void installSample (SampleData::Ptr s); // any non-audio thread

    juce::AudioProcessorValueTreeState apvts;
    ParamRefs params;

    GrainEngine engine;
    StereoDelay delay;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    OutputStage output;

    // Sample hand-off: written under the lock by non-audio threads, picked up
    // by the audio thread with a try-lock (never blocks the audio thread).
    juce::SpinLock sampleLock;
    SampleData::Ptr pendingSample;
    SampleData::Ptr activeSample; // audio thread only

    // Keeps every sample alive until neither the audio thread nor the UI
    // references it, so deallocation never happens on the audio thread.
    juce::CriticalSection poolLock;
    juce::ReferenceCountedArray<SampleData> releasePool;

    mutable juce::CriticalSection infoLock;
    juce::String lastError;
    juce::String missingSamplePath;

    juce::ThreadPool loader;
    std::atomic<int> loadsInFlight { 0 };
    std::atomic<bool> embedSample { true };

    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;
    std::atomic<double> bpmForUi { 120.0 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (DelibabProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelibabProcessor)
};

} // namespace delibab
