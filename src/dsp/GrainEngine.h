#pragma once

#include "../Parameters.h"
#include "SampleData.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <limits>
#include <vector>

namespace delibab
{
// A grain that was just born. Published to the UI (particles) and, later,
// to OSC/Syphon/Spout outputs for audio-visual work.
struct GrainEvent
{
    int layer = 0;
    float position = 0.0f;    // 0..1 within the sample
    float semitones = 0.0f;   // playback pitch relative to original
    float pan = 0.0f;         // -1..1
    float duration = 0.0f;    // seconds
    float gain = 0.0f;        // linear
};

// Single-producer (audio thread) / single-consumer (UI thread) queue.
class GrainEventQueue
{
public:
    static constexpr int kCapacity = 4096;

    void push (const GrainEvent& e) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0) buffer[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) buffer[(size_t) scope.startIndex2] = e;
        // If full, the event is dropped: the UI simply misses a particle.
    }

    template <typename Fn>
    void popAll (Fn&& fn)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) fn (buffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) fn (buffer[(size_t) (scope.startIndex2 + i)]);
    }

private:
    juce::AbstractFifo fifo { kCapacity };
    std::array<GrainEvent, kCapacity> buffer;
};

// Per-layer parameter values resolved once per block (plain values, no atomics).
struct LayerSnapshot
{
    bool on = false;
    LayerMode mode = LayerMode::cloud;
    float gain = 1.0f, pan = 0.0f;
    float position = 0.0f, spray = 0.0f, scan = 0.0f;
    float sizeSec = 0.1f, sizeRand = 0.0f, shape = 0.5f;
    float density = 10.0f;
    double rateBeats = 0.25;
    int steps = 16, hits = 4, rotate = 0;
    float chance = 1.0f;
    float pitch = 0.0f;          // semitones (coarse)
    float fine = 0.0f;           // semitones (fine tune, -1..1)
    float pitchRand = 0.0f;
    bool quantize = false;
    float formant = 0.0f;
    float reverse = 0.0f, spread = 0.0f;
};

struct EngineSnapshot
{
    std::array<LayerSnapshot, kNumLayers> layers;
    float attack = 0.01f, decay = 0.3f, sustain = 1.0f, release = 0.5f;
    int rootNote = 60;
    int scaleKey = 0, scaleType = 0;
};

// Musical time at the start of a render call.
struct TimeContext
{
    double ppq = 0.0;          // quarter notes
    double ppqPerSample = 0.0; // tempo / 60 / sampleRate
};

class GrainEngine
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kMaxGrainsPerVoice = 192;
    static constexpr int kMaxTotalGrains = 1024;

    GrainEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // The sample must stay alive for as long as it is set (the processor
    // guarantees this). Changing it silences all grains.
    void setSample (const SampleData* newSample);
    void setSnapshot (const EngineSnapshot& s) noexcept { snap = s; }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void setSustainPedal (bool down);
    void allNotesOff (bool immediate);

    // Adds into out[startSample, startSample + numSamples). Stereo only.
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, const TimeContext& time);

    GrainEventQueue& getEvents() noexcept { return events; }
    int getActiveGrainCount() const noexcept { return activeGrainCount.load (std::memory_order_relaxed); }
    // Last position (0..1) a grain was spawned from, per layer, for the UI.
    float getLayerPlayhead (int layer) const noexcept { return playheads[(size_t) layer].load (std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept;

private:
    struct Grain
    {
        double pos = 0.0;       // read position in source frames
        double inc = 1.0;       // source frames per output sample (signed)
        float phase = 0.0f;     // window phase 0..1
        float phaseInc = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        int remaining = 0;      // output samples left
        int startOffset = 0;    // first sample to render in the current block
        int table = 0;          // window table pair (0 = perc->hann, 1 = hann->flat)
        float tableMix = 0.0f;
        bool active = false;
    };

    struct Voice
    {
        bool active = false;
        bool held = false;      // key physically down
        bool releasing = false; // envelope in release stage
        int note = 0;
        float velocity = 0.0f;
        juce::uint32 startedAt = 0;
        juce::ADSR adsr;
        std::array<double, kNumLayers> playhead {};   // scan offset, fraction of sample
        std::array<double, kNumLayers> countdown {};  // samples until next spawn (cloud/tonal)
        std::array<Grain, kMaxGrainsPerVoice> grains {};
        int numGrains = 0;
        juce::uint32 rng = 1;
    };

    void startVoice (Voice& v, int note, float velocity);
    void renderVoice (Voice& v, juce::AudioBuffer<float>& out, int startSample, int numSamples);
    void spawnGrain (Voice& v, int layerIndex, double when);
    void computePulseTriggers (const TimeContext& time, int numSamples);

    static float nextRandom (juce::uint32& state) noexcept; // 0..1

    const SampleData* sample = nullptr;
    EngineSnapshot snap;
    double sampleRate = 44100.0;
    std::array<Voice, kMaxVoices> voices;
    juce::uint32 voiceCounter = 0;
    bool sustainDown = false;

    std::vector<float> envBuffer;
    std::array<std::vector<double>, kNumLayers> pulseTriggers;
    std::array<juce::int64, kNumLayers> lastPulseStep;
    int totalGrains = 0;
    int currentBlockSize = 0;

    GrainEventQueue events;
    std::array<std::atomic<float>, kNumLayers> playheads {};
    std::atomic<int> activeGrainCount { 0 };
    std::atomic<int> activeVoiceCount { 0 };
};

} // namespace delibab
