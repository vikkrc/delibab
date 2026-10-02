// Headless test & render tool.
//
//   DelibabTests [outputDir]
//
// Builds a synthetic source sample, renders every grain mode offline to WAV
// files (so they can be listened to), and checks: no NaN/Inf, no clipping, no
// silence, tempo-accurate Pulse timing, state save/restore incl. the embedded
// sample, CPU headroom, and that the editor can be created and drawn.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace delibab;

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ ok ] " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// A host transport that is playing at a fixed tempo.
struct TestPlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

// 4 s stereo: a sustained harmonic tone with vibrato (0-2 s) then eight
// percussive hits (2-4 s).
juce::AudioBuffer<float> makeSourceAudio()
{
    const int n = (int) (kRate * 4.0);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (42);
    const double f0 = 130.81; // C3

    double phase = 0.0;
    for (int i = 0; i < n / 2; ++i)
    {
        const double t = i / kRate;
        // Gentle vibrato via phase accumulation (+/- 0.3 %).
        phase += 2.0 * juce::MathConstants<double>::pi * f0 * (1.0 + 0.003 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / kRate;
        double s = 0.0;
        for (int h = 1; h <= 12; ++h)
            s += std::sin (h * phase + h * 0.7) / (h * 1.3);
        const double env = juce::jmin (1.0, t * 20.0) * juce::jmin (1.0, (2.0 - t) * 20.0);
        b.setSample (0, i, (float) (0.35 * s * env));
        b.setSample (1, i, (float) (0.35 * s * env * (0.9 + 0.1 * std::cos (t * 3.0))));
    }

    for (int hit = 0; hit < 8; ++hit)
    {
        const int start = n / 2 + hit * (n / 16);
        for (int i = 0; i < n / 16 && start + i < n; ++i)
        {
            const double t = i / kRate;
            const double noise = (rng.nextDouble() * 2.0 - 1.0) * std::exp (-t * (hit % 2 ? 40.0 : 18.0));
            const double thump = std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-t * 30.0)) * t)
                               * std::exp (-t * 12.0) * (hit % 2 ? 0.2 : 1.0);
            const float v = (float) (0.6 * noise + 0.7 * thump);
            b.addSample (0, start + i, v);
            b.addSample (1, start + i, v);
        }
    }
    return b;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);
    if (! static_cast<juce::FileOutputStream*> (out.get())->openedOk())
        return false;
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                .withSampleRate (kRate)
                                                .withNumChannels (b.getNumChannels())
                                                .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

void setParam (DelibabProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.getState().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

void soloLayer (DelibabProcessor& p, int layer, LayerMode mode)
{
    for (int l = 0; l < kNumLayers; ++l)
        setParam (p, ids::layer (l, ids::on), l == layer ? 1.0f : 0.0f);
    setParam (p, ids::layer (layer, ids::mode), (float) (int) mode);
}

struct NoteEvent { double time; int note; bool on; };

struct RenderResult
{
    juce::AudioBuffer<float> audio;
    double seconds = 0.0;
    int grainEvents = 0;
};

RenderResult render (DelibabProcessor& p, double seconds, const std::vector<NoteEvent>& notes,
                     TestPlayHead* playHead = nullptr, bool consumeEvents = true)
{
    RenderResult r;
    const int total = (int) (seconds * kRate);
    r.audio.setSize (2, total);
    r.audio.clear();
    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    p.setPlayHead (playHead);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = juce::jmin (kBlock, total - pos);
        block.setSize (2, n, false, false, true);
        midi.clear();
        for (const auto& e : notes)
        {
            const int t = (int) (e.time * kRate);
            if (t >= pos && t < pos + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100)
                                    : juce::MidiMessage::noteOff (1, e.note), t - pos);
        }
        p.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            r.audio.copyFrom (ch, pos, block, ch, 0, n);
        if (playHead != nullptr)
            playHead->ppq += n * playHead->bpm / 60.0 / kRate;

        if (consumeEvents)
            p.getEngine().getEvents().popAll ([&r] (const GrainEvent&) { ++r.grainEvents; });
    }
    r.seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    p.setPlayHead (nullptr);
    return r;
}

void checkAudio (const RenderResult& r, const juce::String& name, float minRms)
{
    bool finite = true;
    float peak = 0.0f;
    double sum = 0.0;
    for (int ch = 0; ch < r.audio.getNumChannels(); ++ch)
        for (int i = 0; i < r.audio.getNumSamples(); ++i)
        {
            const float v = r.audio.getSample (ch, i);
            if (! std::isfinite (v)) finite = false;
            peak = juce::jmax (peak, std::abs (v));
            sum += (double) v * v;
        }
    const float rms = (float) std::sqrt (sum / (r.audio.getNumSamples() * r.audio.getNumChannels()));
    std::cout << "         " << name << ": peak " << juce::Decibels::gainToDecibels (peak)
              << " dB, rms " << juce::Decibels::gainToDecibels (rms) << " dB, "
              << r.grainEvents << " grain events, render x" << (r.audio.getNumSamples() / kRate) / juce::jmax (1.0e-6, r.seconds)
              << " realtime" << std::endl;
    check (finite, name + ": output is finite");
    check (peak <= 1.0f, name + ": no clipping");
    check (rms >= minRms, name + ": not silent");
}


// Fundamental frequency of a stretch of the left channel, by autocorrelation.
double estimateF0 (const juce::AudioBuffer<float>& b, double fromSec, double lenSec)
{
    const int start = (int) (fromSec * kRate), n = (int) (lenSec * kRate);
    const float* x = b.getReadPointer (0) + start;
    const int minLag = (int) (kRate / 1200.0), maxLag = (int) (kRate / 50.0);
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double sum = 0.0, e1 = 0.0, e2 = 0.0;
        for (int i = 0; i < n - maxLag - 2; ++i)
        {
            sum += (double) x[i] * x[i + lag];
            e1 += (double) x[i] * x[i];
            e2 += (double) x[i + lag] * x[i + lag];
        }
        ac[(size_t) lag] = sum / std::sqrt (e1 * e2 + 1.0e-12);
        best = juce::jmax (best, ac[(size_t) lag]);
    }
    // First local peak close to the global best avoids octave errors.
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] > 0.9 * best && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
        {
            // Parabolic interpolation for sub-sample lag.
            const double a = ac[(size_t) lag - 1], c = ac[(size_t) lag], d = ac[(size_t) lag + 1];
            const double shift = 0.5 * (a - d) / (a - 2.0 * c + d);
            return kRate / (lag + shift);
        }
    return 0.0;
}

std::unique_ptr<DelibabProcessor> makeProcessor()
{
    auto p = std::make_unique<DelibabProcessor>();
    p->setRateAndBufferSizeDetails (kRate, kBlock);
    p->prepareToPlay (kRate, kBlock);
    return p;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("test-output");
    outDir.createDirectory();
    std::cout << "Delibab tests, output: " << outDir.getFullPathName() << std::endl;

    const auto sourceFile = outDir.getChildFile ("source.wav");
    check (writeWav (sourceFile, makeSourceAudio()), "write synthetic source sample");

    // ---- Loading & analysis ----
    std::cout << "Sample loading" << std::endl;
    {
        juce::String error;
        auto s = SampleData::loadFromFile (sourceFile, error);
        check (s != nullptr, "load WAV from disk " + error);
        if (s != nullptr)
        {
            check (s->getLength() == (int) (kRate * 4.0), "length is correct");
            int onsetsInDrums = 0;
            for (auto o : s->getOnsets())
                if (o >= (int) (kRate * 2.0) - 512) ++onsetsInDrums;
            std::cout << "         " << s->getOnsets().size() << " onsets total, " << onsetsInDrums << " in the drum half" << std::endl;
            check (onsetsInDrums >= 7 && onsetsInDrums <= 10, "transient detector finds the 8 hits");
            check (s->getEmbeddedFlac().getSize() > 1000, "sample is FLAC-encoded for embedding");
        }
        auto bad = SampleData::loadFromFile (outDir.getChildFile ("does-not-exist.wav"), error);
        check (bad == nullptr && error.isNotEmpty(), "missing file fails gracefully");
    }

    // ---- Every mode renders sound ----
    std::cout << "Modes" << std::endl;
    const std::vector<NoteEvent> chord { { 0.0, 48, true }, { 0.0, 55, true }, { 0.0, 60, true }, { 0.0, 64, true },
                                         { 4.5, 48, false }, { 4.5, 55, false }, { 4.5, 60, false }, { 4.5, 64, false } };
    const std::vector<NoteEvent> melody { { 0.0, 60, true }, { 0.75, 60, false }, { 0.75, 63, true }, { 1.5, 63, false },
                                          { 1.5, 67, true }, { 2.25, 67, false }, { 2.25, 70, true }, { 3.0, 70, false },
                                          { 3.0, 72, true }, { 4.5, 72, false } };

    {
        auto p = makeProcessor();
        juce::String err;
        check (p->loadSampleSync (sourceFile, err), "processor loads sample");

        // Cloud: a slow-scanning pad from the tonal half
        soloLayer (*p, 0, LayerMode::cloud);
        setParam (*p, ids::layer (0, ids::position), 0.1f);
        setParam (*p, ids::layer (0, ids::spray), 0.1f);
        setParam (*p, ids::layer (0, ids::scan), 0.15f);
        setParam (*p, ids::layer (0, ids::size), 220.0f);
        setParam (*p, ids::layer (0, ids::density), 30.0f);
        setParam (*p, ids::layer (0, ids::pitchRand), 0.3f);
        auto cloud = render (*p, 6.0, chord);
        checkAudio (cloud, "Cloud pad", 0.01f);
        writeWav (outDir.getChildFile ("mode-cloud.wav"), cloud.audio);
    }
    {
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);

        // Pulse: Euclidean 7/16 at 1/16, octave randomness quantised to minor pentatonic
        soloLayer (*p, 1, LayerMode::pulse);
        setParam (*p, ids::layer (1, ids::position), 0.2f);
        setParam (*p, ids::layer (1, ids::hits), 7.0f);
        setParam (*p, ids::layer (1, ids::pitchRand), 12.0f);
        setParam (*p, ids::layer (1, ids::quantize), 1.0f);
        setParam (*p, ids::scaleType, 9.0f);
        setParam (*p, ids::dlyMix, 0.25f);
        TestPlayHead ph;
        auto pulse = render (*p, 6.0, { { 0.0, 60, true }, { 4.5, 60, false } }, &ph);
        checkAudio (pulse, "Pulse arp", 0.003f);
        writeWav (outDir.getChildFile ("mode-pulse.wav"), pulse.audio);
    }
    {
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);

        // Tonal: playable lead from the harmonic half
        soloLayer (*p, 2, LayerMode::tonal);
        setParam (*p, ids::layer (2, ids::position), 0.2f);
        setParam (*p, ids::layer (2, ids::size), 20.0f);
        setParam (*p, ids::layer (2, ids::scan), 0.05f);
        setParam (*p, ids::layer (2, ids::formant), 5.0f);
        auto tonal = render (*p, 6.0, melody);
        checkAudio (tonal, "Tonal lead", 0.01f);
        writeWav (outDir.getChildFile ("mode-tonal.wav"), tonal.audio);
    }
    {
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);

        // Slice: beat from the drum half, walking through the hits
        soloLayer (*p, 3, LayerMode::slice);
        setParam (*p, ids::layer (3, ids::position), 0.5f);
        setParam (*p, ids::layer (3, ids::scan), 0.6f);
        setParam (*p, ids::layer (3, ids::size), 160.0f);
        setParam (*p, ids::layer (3, ids::rate), 4.0f); // 1/16
        setParam (*p, ids::layer (3, ids::hits), 9.0f);
        setParam (*p, ids::revMix, 0.15f);
        TestPlayHead ph;
        auto slice = render (*p, 6.0, { { 0.0, 60, true }, { 5.5, 60, false } }, &ph);
        checkAudio (slice, "Slice beat", 0.003f);
        writeWav (outDir.getChildFile ("mode-slice.wav"), slice.audio);
    }


    // ---- Pitch is correct ----
    std::cout << "Pitch" << std::endl;
    {
        // Cloud: the tonal half is C3 (130.81 Hz) and the root is C4 (60), so
        // playing C5 (72) must double it twice -> 523.25 Hz. Reverb off.
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);
        soloLayer (*p, 0, LayerMode::cloud);
        setParam (*p, ids::revMix, 0.0f);
        setParam (*p, ids::layer (0, ids::position), 0.2f);
        setParam (*p, ids::layer (0, ids::spray), 0.02f);
        setParam (*p, ids::layer (0, ids::sizeRand), 0.0f);
        setParam (*p, ids::layer (0, ids::size), 150.0f);
        setParam (*p, ids::rootNote, 48.0f); // sample is C3
        auto r = render (*p, 2.0, { { 0.0, 72, true } });
        writeWav (outDir.getChildFile ("pitch-cloud-c5.wav"), r.audio);
        const double f = estimateF0 (r.audio, 0.8, 0.4);
        std::cout << "         Cloud, C5 from a C3 sample with root C3: " << f << " Hz (expect 523.25)" << std::endl;
        check (std::abs (f - 523.25) < 523.25 * 0.01, "Cloud plays the right pitch");
    }
    {
        // Tonal: pitch comes from the grain rate. Note 57 (A3) = 220 Hz.
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);
        soloLayer (*p, 2, LayerMode::tonal);
        setParam (*p, ids::revMix, 0.0f);
        setParam (*p, ids::layer (2, ids::position), 0.2f);
        setParam (*p, ids::layer (2, ids::spray), 0.0f);
        setParam (*p, ids::layer (2, ids::spread), 0.0f);
        auto r = render (*p, 2.0, { { 0.0, 57, true } });
        writeWav (outDir.getChildFile ("pitch-tonal-a3.wav"), r.audio);
        const double f = estimateF0 (r.audio, 0.8, 0.4);
        std::cout << "         Tonal, A3: " << f << " Hz (expect 220)" << std::endl;
        check (std::abs (f - 220.0) < 220.0 * 0.01, "Tonal plays the right pitch");

        auto q = makeProcessor();
        q->loadSampleSync (sourceFile, err);
        soloLayer (*q, 2, LayerMode::tonal);
        setParam (*q, ids::revMix, 0.0f);
        setParam (*q, ids::layer (2, ids::position), 0.2f);
        setParam (*q, ids::layer (2, ids::spray), 0.0f);
        setParam (*q, ids::layer (2, ids::spread), 0.0f);
        auto r2 = render (*q, 2.0, { { 0.0, 81, true } });
        writeWav (outDir.getChildFile ("pitch-tonal-a5.wav"), r2.audio);
        const double f2 = estimateF0 (r2.audio, 0.8, 0.4);
        std::cout << "         Tonal, A5: " << f2 << " Hz (expect 880)" << std::endl;
        check (std::abs (f2 - 880.0) < 880.0 * 0.01, "Tonal tracks high notes");
    }

    // ---- Pulse timing is locked to the host tempo ----
    std::cout << "Timing" << std::endl;
    {
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);
        soloLayer (*p, 1, LayerMode::pulse);
        setParam (*p, ids::layer (1, ids::rate), 4.0f);   // 1/16
        setParam (*p, ids::layer (1, ids::steps), 16.0f);
        setParam (*p, ids::layer (1, ids::hits), 16.0f);  // every step
        setParam (*p, ids::layer (1, ids::chance), 1.0f);
        TestPlayHead ph;
        ph.bpm = 120.0;
        auto r = render (*p, 4.0, { { 0.0, 60, true }, { 3.999, 60, false } }, &ph);
        // 120 bpm, 1/16 = 8 per second -> 32 grains in 4 s
        std::cout << "         " << r.grainEvents << " grains in 4 s at 120 bpm 1/16 (expect 32)" << std::endl;
        check (r.grainEvents >= 31 && r.grainEvents <= 33, "Pulse fires exactly on the 1/16 grid");

        auto q = makeProcessor();
        q->loadSampleSync (sourceFile, err);
        soloLayer (*q, 1, LayerMode::pulse);
        setParam (*q, ids::layer (1, ids::rate), 4.0f);
        setParam (*q, ids::layer (1, ids::steps), 16.0f);
        setParam (*q, ids::layer (1, ids::hits), 5.0f); // Euclidean 5 of 16
        TestPlayHead ph2;
        auto r2 = render (*q, 4.0, { { 0.0, 62, true }, { 3.999, 62, false } }, &ph2);
        std::cout << "         " << r2.grainEvents << " grains for 5/16 over 2 bars (expect 10)" << std::endl;
        check (r2.grainEvents >= 9 && r2.grainEvents <= 11, "Euclidean pattern density is correct");
    }

    // ---- State save / restore ----
    std::cout << "State" << std::endl;
    {
        auto a = makeProcessor();
        juce::String err;
        const auto copyFile = outDir.getChildFile ("source-copy.wav");
        sourceFile.copyFileTo (copyFile);
        a->loadSampleSync (copyFile, err);
        setParam (*a, ids::layer (2, ids::size), 333.0f);
        setParam (*a, ids::layer (3, ids::mode), (float) (int) LayerMode::tonal);
        setParam (*a, ids::scaleType, 4.0f);

        juce::MemoryBlock state;
        a->getStateInformation (state);
        std::cout << "         state size " << (int) state.getSize() / 1024 << " KB (with embedded sample)" << std::endl;

        copyFile.deleteFile(); // the original file disappears: the embedded copy must be used
        auto b = makeProcessor();
        b->setStateInformation (state.getData(), (int) state.getSize());

        const auto value = [] (DelibabProcessor& p, const juce::String& id)
        { return p.getState().getRawParameterValue (id)->load(); };
        check (std::abs (value (*b, ids::layer (2, ids::size)) - 333.0f) < 0.5f, "float parameter restored");
        check ((int) value (*b, ids::layer (3, ids::mode)) == (int) LayerMode::tonal, "choice parameter restored");
        check ((int) value (*b, ids::scaleType) == 4, "global parameter restored");
        auto restored = b->getCurrentSample();
        check (restored != nullptr && restored->getLength() == (int) (kRate * 4.0), "embedded sample restored without the file");

        // Path-only (embedding off) with a missing file keeps the reference.
        a->setEmbedSample (false);
        juce::MemoryBlock state2;
        a->getStateInformation (state2);
        auto c = makeProcessor();
        c->setStateInformation (state2.getData(), (int) state2.getSize());
        check (c->getCurrentSample() == nullptr && c->getMissingSamplePath().endsWith ("source-copy.wav"),
               "missing file is reported and its path kept");
        check (state2.getSize() < state.getSize() / 10, "path-only state is small");

        // Garbage state must not crash.
        juce::MemoryBlock junk;
        junk.setSize (1000, true);
        c->setStateInformation (junk.getData(), (int) junk.getSize());
        check (true, "garbage state ignored");
    }

    // ---- CPU ----
    std::cout << "Performance" << std::endl;
    {
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);
        for (int l = 0; l < kNumLayers; ++l)
            setParam (*p, ids::layer (l, ids::on), 1.0f);
        setParam (*p, ids::layer (0, ids::density), 80.0f);
        setParam (*p, ids::layer (0, ids::size), 300.0f);
        setParam (*p, ids::revMix, 0.3f);
        setParam (*p, ids::dlyMix, 0.2f);
        std::vector<NoteEvent> notes;
        for (int i = 0; i < 8; ++i)
            notes.push_back ({ 0.0, 48 + i * 3, true });
        TestPlayHead ph;
        auto r = render (*p, 10.0, notes, &ph);
        checkAudio (r, "All 4 layers, 8 voices, dense", 0.01f);
        const double factor = 10.0 / r.seconds;
        check (factor > 4.0, "heavy patch renders at > 4x realtime (" + juce::String (factor, 1) + "x)");
    }

    // ---- Editor ----
    std::cout << "Editor" << std::endl;
    {
        auto p = makeProcessor();
        juce::String err;
        p->loadSampleSync (sourceFile, err);
        setParam (*p, ids::layer (1, ids::on), 1.0f);
        setParam (*p, ids::layer (2, ids::on), 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
        check (editor != nullptr, "editor created");
        if (auto* ed = dynamic_cast<DelibabEditor*> (editor.get()))
        {
            TestPlayHead ph;
            render (*p, 0.6, { { 0.0, 60, true }, { 0.0, 67, true } }, &ph);
            // Keep the last grains queued, then let the UI timers run so particles appear.
            render (*p, 0.3, {}, &ph, false);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            const auto img = ed->snapshot();
            check (img.isValid() && img.getWidth() == DelibabEditor::kBaseWidth, "editor paints");
            juce::PNGImageFormat png;
            const auto pngFile = outDir.getChildFile ("editor.png");
            pngFile.deleteFile();
            juce::FileOutputStream os (pngFile);
            check (os.openedOk() && png.writeImageToStream (img, os), "editor snapshot saved");
        }
        editor.reset();
    }

    std::cout << (failures == 0 ? "\nALL TESTS PASSED" : "\nFAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
