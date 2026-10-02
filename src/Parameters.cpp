#include "Parameters.h"

namespace delibab
{
juce::String ids::layer (int layerIndex, const char* suffix)
{
    return "l" + juce::String (layerIndex + 1) + "_" + suffix;
}

const juce::StringArray& modeNames()
{
    static const juce::StringArray names { "Cloud", "Pulse", "Slice", "Tonal" };
    return names;
}

const juce::StringArray& rateNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                           "1/2T", "1/4T", "1/8T", "1/16T",
                                           "1/4D", "1/8D", "1/16D" };
    return names;
}

double rateInBeats (int rateIndex)
{
    static constexpr double beats[] { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                      4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                      1.5, 0.75, 0.375 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, rateIndex)];
}

const juce::StringArray& keyNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

const juce::StringArray& scaleNames()
{
    static const juce::StringArray names { "Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian",
                                           "Mixolydian", "Harmonic minor", "Pentatonic major",
                                           "Pentatonic minor", "Whole tone", "Fifths", "Octaves" };
    return names;
}

namespace
{
    using Attr = juce::AudioParameterFloatAttributes;

    juce::String pct (float v, int)     { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String db (float v, int)     { return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; }
    juce::String ms (float v, int)     { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String secs (float v, int)   { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; }
    juce::String hz (float v, int)     { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; }
    juce::String semis (float v, int)  { return (v > 0 ? "+" : "") + juce::String (v, 0) + " st"; }
    juce::String cents (float v, int)  { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct"; }
    juce::String speed (float v, int)  { return juce::String (v, 2) + "x"; }
    juce::String panStr (float v, int)
    {
        if (std::abs (v) < 0.005f) return "C";
        return juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + (v < 0 ? " L" : " R");
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    void addFloat (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                   const juce::String& id, const juce::String& name,
                   juce::NormalisableRange<float> range, float def,
                   juce::String (*toText) (float, int))
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            Attr().withStringFromValueFunction (toText)));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Global -------------------------------------------------------------
    addFloat (layout, ids::master,  "Master",       { -48.0f, 6.0f, 0.01f }, -6.0f, db);
    addFloat (layout, ids::attack,  "Amp Attack",   skewed (0.001f, 10.0f, 0.5f), 0.01f, secs);
    addFloat (layout, ids::decay,   "Amp Decay",    skewed (0.001f, 10.0f, 0.5f), 0.4f, secs);
    addFloat (layout, ids::sustain, "Amp Sustain",  { 0.0f, 1.0f }, 1.0f, pct);
    addFloat (layout, ids::release, "Amp Release",  skewed (0.001f, 20.0f, 1.0f), 0.8f, secs);

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ids::rootNote, 1 }, "Sample Root", 0, 127, 60,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 3); })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::scaleKey, 1 },  "Scale Key",  keyNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::scaleType, 1 }, "Scale",      scaleNames(), 0));

    addFloat (layout, ids::dlyMix, "Delay Mix", { 0.0f, 1.0f }, 0.0f, pct);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::dlyTime, 1 }, "Delay Time", rateNames(), 11));
    addFloat (layout, ids::dlyFb,  "Delay Feedback", { 0.0f, 0.95f }, 0.45f, pct);
    addFloat (layout, ids::revMix, "Reverb Mix",  { 0.0f, 1.0f }, 0.25f, pct);
    addFloat (layout, ids::revSize, "Reverb Size", { 0.0f, 1.0f }, 0.75f, pct);
    addFloat (layout, ids::revDamp, "Reverb Damping", { 0.0f, 1.0f }, 0.45f, pct);

    // ---- Layers -------------------------------------------------------------
    // Each layer gets a different default mode so that switching a layer on
    // immediately shows off what it does.
    const int defaultModes[kNumLayers] { (int) LayerMode::cloud, (int) LayerMode::pulse,
                                         (int) LayerMode::tonal, (int) LayerMode::slice };

    for (int i = 0; i < kNumLayers; ++i)
    {
        const auto L = [i] (const char* s) { return ids::layer (i, s); };
        const auto N = [i] (const juce::String& s) { return "L" + juce::String (i + 1) + " " + s; };
        const auto mode = (LayerMode) defaultModes[i];

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { L (ids::on), 1 }, N ("On"), i == 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { L (ids::mode), 1 }, N ("Mode"), modeNames(), defaultModes[i]));

        addFloat (layout, L (ids::level),    N ("Level"),     { -60.0f, 6.0f, 0.01f }, -3.0f, db);
        addFloat (layout, L (ids::pan),      N ("Pan"),       { -1.0f, 1.0f }, 0.0f, panStr);
        addFloat (layout, L (ids::position), N ("Position"),  { 0.0f, 1.0f }, 0.25f + 0.15f * (float) i, pct);
        addFloat (layout, L (ids::spray),    N ("Spray"),     skewed (0.0f, 1.0f, 0.15f), mode == LayerMode::cloud ? 0.08f : 0.0f, pct);
        addFloat (layout, L (ids::scan),     N ("Scan"),      { -2.0f, 2.0f, 0.001f }, 0.0f, speed);
        addFloat (layout, L (ids::size),     N ("Size"),      skewed (2.0f, 2000.0f, 120.0f),
                  mode == LayerMode::tonal ? 25.0f : (mode == LayerMode::cloud ? 180.0f : 90.0f), ms);
        addFloat (layout, L (ids::sizeRand), N ("Size Rand"), { 0.0f, 1.0f }, mode == LayerMode::cloud ? 0.3f : 0.0f, pct);
        addFloat (layout, L (ids::shape),    N ("Shape"),     { 0.0f, 1.0f }, mode == LayerMode::pulse || mode == LayerMode::slice ? 0.15f : 0.5f, pct);
        addFloat (layout, L (ids::density),  N ("Density"),   skewed (0.5f, 200.0f, 20.0f), 24.0f, hz);

        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { L (ids::rate), 1 }, N ("Rate"), rateNames(), 4));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { L (ids::steps), 1 },  N ("Steps"),  1, 16, 16));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { L (ids::hits), 1 },   N ("Hits"),   0, 16, mode == LayerMode::slice ? 7 : 5));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { L (ids::rotate), 1 }, N ("Rotate"), 0, 15, 0));

        addFloat (layout, L (ids::chance),    N ("Chance"),      { 0.0f, 1.0f }, 1.0f, pct);
        addFloat (layout, L (ids::pitch),     N ("Pitch"),       { -36.0f, 36.0f, 1.0f }, 0.0f, semis);
        addFloat (layout, L (ids::fine),      N ("Fine"),        { -100.0f, 100.0f, 0.1f }, 0.0f, cents);
        addFloat (layout, L (ids::pitchRand), N ("Pitch Rand"),  { 0.0f, 24.0f, 0.01f }, 0.0f, semis);
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { L (ids::quantize), 1 }, N ("Quantize"), false));
        addFloat (layout, L (ids::formant),   N ("Formant"),     { -24.0f, 24.0f, 0.01f }, 0.0f, semis);
        addFloat (layout, L (ids::reverse),   N ("Reverse"),     { 0.0f, 1.0f }, 0.0f, pct);
        addFloat (layout, L (ids::spread),    N ("Spread"),      { 0.0f, 1.0f }, 0.4f, pct);
    }

    return layout;
}

void ParamRefs::bind (juce::AudioProcessorValueTreeState& s)
{
    const auto get = [&s] (const juce::String& id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    master    = get (ids::master);
    attack    = get (ids::attack);
    decay     = get (ids::decay);
    sustain   = get (ids::sustain);
    release   = get (ids::release);
    rootNote  = get (ids::rootNote);
    scaleKey  = get (ids::scaleKey);
    scaleType = get (ids::scaleType);
    dlyMix    = get (ids::dlyMix);
    dlyTime   = get (ids::dlyTime);
    dlyFb     = get (ids::dlyFb);
    revMix    = get (ids::revMix);
    revSize   = get (ids::revSize);
    revDamp   = get (ids::revDamp);

    for (int i = 0; i < kNumLayers; ++i)
    {
        auto& l = layers[(size_t) i];
        const auto L = [i] (const char* suffix) { return ids::layer (i, suffix); };
        l.on        = get (L (ids::on));
        l.mode      = get (L (ids::mode));
        l.level     = get (L (ids::level));
        l.pan       = get (L (ids::pan));
        l.position  = get (L (ids::position));
        l.spray     = get (L (ids::spray));
        l.scan      = get (L (ids::scan));
        l.size      = get (L (ids::size));
        l.sizeRand  = get (L (ids::sizeRand));
        l.shape     = get (L (ids::shape));
        l.density   = get (L (ids::density));
        l.rate      = get (L (ids::rate));
        l.steps     = get (L (ids::steps));
        l.hits      = get (L (ids::hits));
        l.rotate    = get (L (ids::rotate));
        l.chance    = get (L (ids::chance));
        l.pitch     = get (L (ids::pitch));
        l.fine      = get (L (ids::fine));
        l.pitchRand = get (L (ids::pitchRand));
        l.quantize  = get (L (ids::quantize));
        l.formant   = get (L (ids::formant));
        l.reverse   = get (L (ids::reverse));
        l.spread    = get (L (ids::spread));
    }
}

} // namespace delibab
