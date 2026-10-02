#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// -----------------------------------------------------------------------------
// The parameter pool.
//
// RULES (these protect users' DAW automation and saved projects):
//   1. A parameter ID, once released, never changes and is never reused.
//   2. Never remove a parameter. Deprecate it (hide it in the UI) instead.
//   3. Never reorder or insert into a choice list. Append only, and only
//      when that parameter is unlikely to be automated.
//   4. New parameters get a new ID and version hint (ParameterID{id, N}).
// Hosts address parameters by a hash of the string ID, so adding new ones
// later is safe; renaming or deleting is not.
// -----------------------------------------------------------------------------

namespace delibab
{
constexpr int kNumLayers = 4;

enum class LayerMode { cloud = 0, pulse, slice, tonal };

namespace ids
{
    // Global
    inline constexpr const char* master     = "master";
    inline constexpr const char* attack     = "amp_atk";
    inline constexpr const char* decay      = "amp_dec";
    inline constexpr const char* sustain    = "amp_sus";
    inline constexpr const char* release    = "amp_rel";
    inline constexpr const char* rootNote   = "root";
    inline constexpr const char* scaleKey   = "scale_key";
    inline constexpr const char* scaleType  = "scale";
    inline constexpr const char* dlyMix     = "dly_mix";
    inline constexpr const char* dlyTime    = "dly_time";
    inline constexpr const char* dlyFb      = "dly_fb";
    inline constexpr const char* revMix     = "rev_mix";
    inline constexpr const char* revSize    = "rev_size";
    inline constexpr const char* revDamp    = "rev_damp";

    // Per-layer suffixes; full ID is "l<N>_<suffix>", N = 1..4
    inline constexpr const char* on        = "on";
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* level     = "level";
    inline constexpr const char* pan       = "pan";
    inline constexpr const char* position  = "pos";
    inline constexpr const char* spray     = "spray";
    inline constexpr const char* scan      = "scan";
    inline constexpr const char* size      = "size";
    inline constexpr const char* sizeRand  = "size_rnd";
    inline constexpr const char* shape     = "shape";
    inline constexpr const char* density   = "density";
    inline constexpr const char* rate      = "rate";
    inline constexpr const char* steps     = "steps";
    inline constexpr const char* hits      = "hits";
    inline constexpr const char* rotate    = "rot";
    inline constexpr const char* chance    = "chance";
    inline constexpr const char* pitch     = "pitch";
    inline constexpr const char* fine      = "fine";
    inline constexpr const char* pitchRand = "pitch_rnd";
    inline constexpr const char* quantize  = "quant";
    inline constexpr const char* formant   = "formant";
    inline constexpr const char* reverse   = "reverse";
    inline constexpr const char* spread    = "spread";

    juce::String layer (int layerIndex, const char* suffix); // layerIndex is 0-based
}

// Fixed choice lists. APPEND ONLY (see rules above).
const juce::StringArray& modeNames();
const juce::StringArray& rateNames();
const juce::StringArray& keyNames();
const juce::StringArray& scaleNames();

double rateInBeats (int rateIndex);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Raw, lock-free pointers into the parameter values, for the audio thread.
struct LayerParamRefs
{
    std::atomic<float>* on {};
    std::atomic<float>* mode {};
    std::atomic<float>* level {};
    std::atomic<float>* pan {};
    std::atomic<float>* position {};
    std::atomic<float>* spray {};
    std::atomic<float>* scan {};
    std::atomic<float>* size {};
    std::atomic<float>* sizeRand {};
    std::atomic<float>* shape {};
    std::atomic<float>* density {};
    std::atomic<float>* rate {};
    std::atomic<float>* steps {};
    std::atomic<float>* hits {};
    std::atomic<float>* rotate {};
    std::atomic<float>* chance {};
    std::atomic<float>* pitch {};
    std::atomic<float>* fine {};
    std::atomic<float>* pitchRand {};
    std::atomic<float>* quantize {};
    std::atomic<float>* formant {};
    std::atomic<float>* reverse {};
    std::atomic<float>* spread {};
};

struct ParamRefs
{
    std::atomic<float>* master {};
    std::atomic<float>* attack {};
    std::atomic<float>* decay {};
    std::atomic<float>* sustain {};
    std::atomic<float>* release {};
    std::atomic<float>* rootNote {};
    std::atomic<float>* scaleKey {};
    std::atomic<float>* scaleType {};
    std::atomic<float>* dlyMix {};
    std::atomic<float>* dlyTime {};
    std::atomic<float>* dlyFb {};
    std::atomic<float>* revMix {};
    std::atomic<float>* revSize {};
    std::atomic<float>* revDamp {};
    std::array<LayerParamRefs, kNumLayers> layers;

    void bind (juce::AudioProcessorValueTreeState& state);
};

} // namespace delibab
