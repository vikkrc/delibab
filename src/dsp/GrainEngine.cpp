#include "GrainEngine.h"
#include "Scales.h"
#include <cmath>

namespace delibab
{
namespace
{
    // ---- Grain windows ------------------------------------------------------
    // Three shapes; the Shape parameter morphs perc -> hann -> flat.
    constexpr int kWinSize = 1024;

    struct WindowTables
    {
        float t[3][kWinSize + 1];

        WindowTables()
        {
            for (int i = 0; i <= kWinSize; ++i)
            {
                const float x = juce::jmin (1.0f, (float) i / (float) (kWinSize - 1));

                // Percussive: very short raised-sine attack, exponential decay to zero.
                constexpr float atk = 0.02f;
                constexpr float k = 5.0f;
                const float endVal = std::exp (-k);
                float perc;
                if (x < atk)
                    perc = std::sin (juce::MathConstants<float>::halfPi * x / atk);
                else
                    perc = (std::exp (-k * (x - atk) / (1.0f - atk)) - endVal) / (1.0f - endVal);
                t[0][i] = perc;

                // Hann
                t[1][i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * x);

                // Flat (Tukey, 10 % cosine fades)
                constexpr float edge = 0.1f;
                float flat = 1.0f;
                if (x < edge)            flat = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x / edge);
                else if (x > 1.0f - edge) flat = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - x) / edge);
                t[2][i] = flat;
            }
        }
    };

    const WindowTables& windows()
    {
        static const WindowTables tables;
        return tables;
    }

    inline float windowAt (const WindowTables& w, int table, float mix, float phase) noexcept
    {
        const float idx = juce::jlimit (0.0f, (float) (kWinSize - 1), phase * (float) (kWinSize - 1));
        const int i = (int) idx;
        const float f = idx - (float) i;
        const float* a = w.t[table];
        const float* b = w.t[table + 1];
        const float va = a[i] + f * (a[i + 1] - a[i]);
        const float vb = b[i] + f * (b[i + 1] - b[i]);
        return va + mix * (vb - va);
    }

    // 4-point, 3rd-order Hermite interpolation. pos must be >= 0.
    inline float hermite (const float* d, double pos) noexcept
    {
        const int i = (int) pos;
        const float t = (float) (pos - (double) i);
        const float xm1 = d[i - 1], x0 = d[i], x1 = d[i + 1], x2 = d[i + 2];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    inline double wrap01 (double x) noexcept { return x - std::floor (x); }

    inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
}

GrainEngine::GrainEngine()
{
    (void) windows(); // build tables on the constructing (non-audio) thread
    lastPulseStep.fill (std::numeric_limits<juce::int64>::min());
}

float GrainEngine::nextRandom (juce::uint32& s) noexcept
{
    // xorshift32
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float) (s >> 8) * (1.0f / 16777216.0f);
}

void GrainEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    envBuffer.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    for (auto& t : pulseTriggers)
        t.reserve (256);
    for (auto& v : voices)
        v.adsr.setSampleRate (sr);
    reset();
}

void GrainEngine::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.numGrains = 0;
        v.adsr.reset();
    }
    lastPulseStep.fill (std::numeric_limits<juce::int64>::min());
    totalGrains = 0;
    sustainDown = false;
    activeGrainCount.store (0);
    activeVoiceCount.store (0);
}

void GrainEngine::setSample (const SampleData* newSample)
{
    sample = newSample;
    for (auto& v : voices)
        v.numGrains = 0;
    totalGrains = 0;
}

int GrainEngine::getActiveVoiceCount() const noexcept
{
    return activeVoiceCount.load (std::memory_order_relaxed);
}

// ---- Notes -----------------------------------------------------------------

void GrainEngine::noteOn (int note, float velocity)
{
    // A repeated note-on for a sounding note releases the old voice first.
    for (auto& v : voices)
        if (v.active && v.note == note && v.held)
        {
            v.held = false;
            v.releasing = true;
            v.adsr.noteOff();
        }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        // Steal: prefer the oldest releasing voice, otherwise the oldest voice.
        for (auto& v : voices)
            if (v.releasing && (target == nullptr || v.startedAt < target->startedAt))
                target = &v;
        if (target == nullptr)
            for (auto& v : voices)
                if (target == nullptr || v.startedAt < target->startedAt)
                    target = &v;
    }

    startVoice (*target, note, velocity);
}

void GrainEngine::startVoice (Voice& v, int note, float velocity)
{
    v.active = true;
    v.held = true;
    v.releasing = false;
    v.note = note;
    v.velocity = velocity;
    v.startedAt = ++voiceCounter;
    v.rng = 0x9E3779B9u ^ (voiceCounter * 2654435761u) ^ (juce::uint32) (note * 7919);
    if (v.rng == 0) v.rng = 1;
    v.adsr.reset();
    v.adsr.setParameters ({ snap.attack, snap.decay, snap.sustain, snap.release });
    v.adsr.noteOn();
    v.playhead.fill (0.0);
    v.countdown.fill (0.0);
    totalGrains -= v.numGrains;
    v.numGrains = 0;
}

void GrainEngine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
        {
            v.held = false;
            if (! sustainDown && ! v.releasing)
            {
                v.releasing = true;
                v.adsr.noteOff();
            }
        }
}

void GrainEngine::setSustainPedal (bool down)
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.active && ! v.held && ! v.releasing)
            {
                v.releasing = true;
                v.adsr.noteOff();
            }
}

void GrainEngine::allNotesOff (bool immediate)
{
    sustainDown = false;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        v.held = false;
        if (immediate)
        {
            v.active = false;
            v.numGrains = 0;
            v.adsr.reset();
        }
        else if (! v.releasing)
        {
            v.releasing = true;
            v.adsr.noteOff();
        }
    }
    if (immediate)
        totalGrains = 0;
}

// ---- Rendering -------------------------------------------------------------

void GrainEngine::computePulseTriggers (const TimeContext& time, int numSamples)
{
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& triggers = pulseTriggers[(size_t) l];
        triggers.clear();

        const auto& L = snap.layers[(size_t) l];
        if (! L.on || (L.mode != LayerMode::pulse && L.mode != LayerMode::slice) || time.ppqPerSample <= 0.0)
            continue;

        const double step = L.rateBeats;
        const double ppqEnd = time.ppq + numSamples * time.ppqPerSample;
        auto s = (juce::int64) std::ceil (time.ppq / step - 1.0e-6);

        // Transport jumped backwards (loop, rewind): forget the last step.
        if (lastPulseStep[(size_t) l] != std::numeric_limits<juce::int64>::min()
            && s < lastPulseStep[(size_t) l] - 1)
            lastPulseStep[(size_t) l] = std::numeric_limits<juce::int64>::min();

        const int steps = juce::jmax (1, L.steps);
        const int hits = juce::jlimit (0, steps, L.hits);

        for (; (double) s * step < ppqEnd; ++s)
        {
            if (s <= lastPulseStep[(size_t) l])
                continue;
            lastPulseStep[(size_t) l] = s;

            const auto stepIndex = (int) (((s % steps) + steps) % steps);
            const bool hit = (((stepIndex + L.rotate) % steps) * hits) % steps < hits;
            if (! hit)
                continue;

            const double offset = ((double) s * step - time.ppq) / time.ppqPerSample;
            triggers.push_back (juce::jlimit (0.0, (double) numSamples - 1.0, offset));
        }
    }
}

void GrainEngine::render (juce::AudioBuffer<float>& out, int startSample, int numSamples, const TimeContext& timeAtStart)
{
    jassert (out.getNumChannels() >= 2);
    const int maxChunk = (int) envBuffer.size();
    if (maxChunk == 0)
        return; // not prepared yet
    TimeContext time = timeAtStart;

    while (numSamples > 0)
    {
        const int n = juce::jmin (numSamples, maxChunk);

        computePulseTriggers (time, n);

        totalGrains = 0;
        for (auto& v : voices)
            if (v.active) totalGrains += v.numGrains;

        int voicesActive = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            renderVoice (v, out, startSample, n);
            if (v.active) ++voicesActive;
        }

        activeGrainCount.store (totalGrains, std::memory_order_relaxed);
        activeVoiceCount.store (voicesActive, std::memory_order_relaxed);

        startSample += n;
        numSamples -= n;
        time.ppq += n * time.ppqPerSample;
    }
}

void GrainEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& out, int startSample, int n)
{
    currentBlockSize = n;

    // ---- Amp envelope for this block ----
    v.adsr.setParameters ({ snap.attack, snap.decay, snap.sustain, snap.release });
    for (int i = 0; i < n; ++i)
        envBuffer[(size_t) i] = v.adsr.getNextSample();

    // ---- Spawn new grains ----
    if (sample != nullptr)
    {
        const double srRatio = sample->getSampleRate() / sampleRate;
        const double len = (double) sample->getLength();

        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto& L = snap.layers[(size_t) l];
            if (! L.on)
                continue;

            switch (L.mode)
            {
                case LayerMode::cloud:
                {
                    const double mean = sampleRate / juce::jmax (0.1f, L.density);
                    double t = v.countdown[(size_t) l];
                    while (t < n)
                    {
                        spawnGrain (v, l, t);
                        t += mean * (0.25 + 1.5 * nextRandom (v.rng));
                    }
                    v.countdown[(size_t) l] = t - n;
                    break;
                }
                case LayerMode::tonal:
                {
                    float notePitch = (float) v.note + L.pitch;
                    if (L.quantize) notePitch = quantizeToScale (notePitch, snap.scaleKey, snap.scaleType);
                    const double period = sampleRate / juce::jlimit (8.0f, 4000.0f, midiToHz (notePitch + L.fine));
                    double t = v.countdown[(size_t) l];
                    while (t < n)
                    {
                        spawnGrain (v, l, t);
                        t += period;
                    }
                    v.countdown[(size_t) l] = t - n;
                    break;
                }
                case LayerMode::pulse:
                case LayerMode::slice:
                    for (auto t : pulseTriggers[(size_t) l])
                        spawnGrain (v, l, t);
                    break;
            }

            // Advance the scan playhead (fraction of the sample per output sample).
            v.playhead[(size_t) l] = wrap01 (v.playhead[(size_t) l] + L.scan * srRatio / len * n);
        }
    }

    // ---- Render grains ----
    if (sample != nullptr && v.numGrains > 0)
    {
        const auto& win = windows();
        const float* srcL = sample->getChannel (0);
        const float* srcR = sample->getChannel (1);
        const bool stereo = sample->getNumChannels() > 1;
        float* outL = out.getWritePointer (0, startSample);
        float* outR = out.getWritePointer (1, startSample);
        const float* env = envBuffer.data();

        for (int k = 0; k < v.numGrains;)
        {
            auto& g = v.grains[(size_t) k];
            const int s0 = g.startOffset;
            const int count = juce::jmin (n - s0, g.remaining);

            double pos = g.pos;
            float phase = g.phase;
            for (int i = s0; i < s0 + count; ++i)
            {
                const float w = windowAt (win, g.table, g.tableMix, phase) * env[i];
                phase += g.phaseInc;
                const float sl = hermite (srcL, pos);
                const float sr = stereo ? hermite (srcR, pos) : sl;
                outL[i] += sl * w * g.gainL;
                outR[i] += sr * w * g.gainR;
                pos += g.inc;
            }
            g.pos = pos;
            g.phase = phase;
            g.remaining -= count;
            g.startOffset = 0;

            if (g.remaining <= 0)
            {
                // Swap-remove; the moved grain hasn't been rendered yet, so stay on k.
                g = v.grains[(size_t) (v.numGrains - 1)];
                --v.numGrains;
                --totalGrains;
            }
            else
            {
                ++k;
            }
        }
    }

    if (! v.adsr.isActive())
    {
        v.active = false;
        totalGrains -= v.numGrains;
        v.numGrains = 0;
    }
}

void GrainEngine::spawnGrain (Voice& v, int l, double when)
{
    if (sample == nullptr || v.numGrains >= kMaxGrainsPerVoice || totalGrains >= kMaxTotalGrains)
        return;

    const auto& L = snap.layers[(size_t) l];
    if (L.chance < 1.0f && nextRandom (v.rng) >= L.chance)
        return;

    const double len = (double) sample->getLength();
    if (len < 32.0)
        return;

    const double srRatio = sample->getSampleRate() / sampleRate;
    const auto bipolar = [&v] { return nextRandom (v.rng) * 2.0f - 1.0f; };

    // ---- Where in the sample ----
    const double scanPerSample = L.scan * srRatio / len;
    const double playhead = v.playhead[(size_t) l] + scanPerSample * when;
    const double base = wrap01 (L.position + playhead + L.spray * bipolar() * 0.5);
    double startFrame = base * (len - 1.0);
    if (L.mode == LayerMode::slice)
        startFrame = (double) sample->onsetAtOrBefore (startFrame);

    // ---- Pitch ----
    float semis;
    float tonalHz = 0.0f;
    if (L.mode == LayerMode::tonal)
    {
        // Pitch comes from the trigger rate; grain playback speed is the formant.
        semis = L.formant + L.pitchRand * bipolar();
        float notePitch = (float) v.note + L.pitch;
        if (L.quantize) notePitch = quantizeToScale (notePitch, snap.scaleKey, snap.scaleType);
        tonalHz = juce::jlimit (8.0f, 4000.0f, midiToHz (notePitch + L.fine));
    }
    else
    {
        float target = (float) v.note + L.pitch + L.pitchRand * bipolar();
        if (L.quantize) target = quantizeToScale (target, snap.scaleKey, snap.scaleType);
        semis = target - (float) snap.rootNote + L.fine;
    }
    const double ratio = std::exp2 ((double) semis / 12.0) * srRatio;

    // ---- Length ----
    const double sizeSec = juce::jlimit (0.002, 4.0, (double) L.sizeSec * std::exp2 (2.0 * L.sizeRand * bipolar()));
    const int wanted = juce::jmax (16, (int) (sizeSec * sampleRate));

    const bool reversed = L.reverse > 0.0f && nextRandom (v.rng) < L.reverse;
    double pos = startFrame;
    double inc = ratio;
    int maxSteps;
    if (reversed)
    {
        pos = juce::jmin (len - 1.0, startFrame + (wanted - 1) * ratio);
        inc = -ratio;
        maxSteps = (int) (pos / ratio) + 1;
    }
    else
    {
        maxSteps = (int) ((len - 1.0 - pos) / ratio) + 1;
    }

    const int count = juce::jmin (wanted, maxSteps);
    if (count < 16)
        return;

    // ---- Level normalisation: keep loudness roughly stable as overlap grows ----
    const double grainSec = count / sampleRate;
    float norm = 1.0f;
    if (L.mode == LayerMode::cloud)
        norm = 1.0f / std::sqrt ((float) juce::jmax (1.0, L.density * grainSec));
    else if (L.mode == LayerMode::tonal)
        norm = 1.0f / std::sqrt ((float) juce::jmax (1.0, tonalHz * grainSec * 0.5));

    const float velGain = 0.25f + 0.75f * v.velocity;
    const float gain = L.gain * velGain * norm;
    const float pan = juce::jlimit (-1.0f, 1.0f, L.pan + L.spread * bipolar());
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    // ---- Sub-sample start ----
    int startOffset = (int) std::ceil (when);
    if (startOffset > currentBlockSize - 1) startOffset = currentBlockSize - 1;
    const double delta = startOffset - when;

    auto& g = v.grains[(size_t) v.numGrains];
    g.phaseInc = 1.0f / (float) count;
    g.pos = juce::jlimit (0.0, len - 1.0, pos + delta * inc);
    g.inc = inc;
    g.phase = juce::jmax (0.0f, (float) delta * g.phaseInc);
    g.remaining = count;
    g.startOffset = startOffset;
    g.gainL = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    g.gainR = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;
    g.table = L.shape < 0.5f ? 0 : 1;
    g.tableMix = L.shape < 0.5f ? L.shape * 2.0f : (L.shape - 0.5f) * 2.0f;
    ++v.numGrains;
    ++totalGrains;

    playheads[(size_t) l].store ((float) base, std::memory_order_relaxed);

    // Tonal mode spawns hundreds of grains per second; only publish some.
    if (L.mode != LayerMode::tonal || (v.rng & 31u) == 0)
        events.push ({ l, (float) (startFrame / len), L.mode == LayerMode::tonal ? L.pitch : semis,
                       pan, (float) grainSec, gain });
}

} // namespace delibab
