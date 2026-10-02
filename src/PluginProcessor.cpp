#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace delibab
{
namespace
{
    const juce::Identifier stateType ("DELIBAB");
    const juce::Identifier sampleNode ("SAMPLE");
    const juce::Identifier propPath ("path");
    const juce::Identifier propName ("name");
    const juce::Identifier propFlac ("flac");
    const juce::Identifier propEmbed ("embed");
    const juce::Identifier propVersion ("version");
}

DelibabProcessor::DelibabProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, stateType, createParameterLayout()),
      loader (juce::ThreadPoolOptions{}.withThreadName ("Delibab loader").withNumberOfThreads (1))
{
    params.bind (apvts);
    startTimer (1000);
}

DelibabProcessor::~DelibabProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 10000);
}

// ---- Lifecycle ----------------------------------------------------------------

void DelibabProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int maxBlock = juce::jmax (32, samplesPerBlock);
    engine.prepare (sampleRate, maxBlock);
    engine.setSample (activeSample.get());
    delay.prepare (sampleRate, maxBlock);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    output.prepare (sampleRate);
    internalPpq = 0.0;
}

void DelibabProcessor::releaseResources()
{
    engine.allNotesOff (true);
}

bool DelibabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// ---- Audio --------------------------------------------------------------------

EngineSnapshot DelibabProcessor::makeSnapshot() const
{
    EngineSnapshot s;
    s.attack = params.attack->load();
    s.decay = params.decay->load();
    s.sustain = params.sustain->load();
    s.release = params.release->load();
    s.rootNote = (int) params.rootNote->load();
    s.scaleKey = (int) params.scaleKey->load();
    s.scaleType = (int) params.scaleType->load();

    for (int i = 0; i < kNumLayers; ++i)
    {
        const auto& p = params.layers[(size_t) i];
        auto& l = s.layers[(size_t) i];
        l.on = p.on->load() > 0.5f;
        l.mode = (LayerMode) juce::jlimit (0, modeNames().size() - 1, (int) p.mode->load());
        l.gain = juce::Decibels::decibelsToGain (p.level->load(), -59.9f);
        l.pan = p.pan->load();
        l.position = p.position->load();
        l.spray = p.spray->load();
        l.scan = p.scan->load();
        l.sizeSec = p.size->load() * 0.001f;
        l.sizeRand = p.sizeRand->load();
        l.shape = p.shape->load();
        l.density = p.density->load();
        l.rateBeats = rateInBeats ((int) p.rate->load());
        l.steps = (int) p.steps->load();
        l.hits = (int) p.hits->load();
        l.rotate = (int) p.rotate->load();
        l.chance = p.chance->load();
        l.pitch = p.pitch->load();
        l.fine = p.fine->load() * 0.01f;
        l.pitchRand = p.pitchRand->load();
        l.quantize = p.quantize->load() > 0.5f;
        l.formant = p.formant->load();
        l.reverse = p.reverse->load();
        l.spread = p.spread->load();
    }
    return s;
}

void DelibabProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())
        engine.setSustainPedal (true);
    else if (m.isSustainPedalOff())
        engine.setSustainPedal (false);
    else if (m.isAllSoundOff())
        engine.allNotesOff (true);
    else if (m.isAllNotesOff())
        engine.allNotesOff (false);
}

void DelibabProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // ---- Pick up a newly loaded sample (never blocks) ----
    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked() && pendingSample != activeSample)
        {
            activeSample = pendingSample;
            engine.setSample (activeSample.get());
        }
    }

    engine.setSnapshot (makeSnapshot());
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // ---- Musical time ----
    double bpm = 120.0;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto pos = hostPlayHead->getPosition())
        {
            if (auto hostBpm = pos->getBpm())
                bpm = juce::jlimit (20.0, 999.0, *hostBpm);
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                    internalPpq = *ppq; // follow the host while it plays, free-run otherwise
        }
    }
    bpmForUi.store (bpm, std::memory_order_relaxed);
    const double ppqPerSample = bpm / 60.0 / currentSampleRate;

    // ---- Render, splitting the block at MIDI events ----
    int pos = 0;
    for (const auto metadata : midi)
    {
        const int t = juce::jlimit (0, numSamples, metadata.samplePosition);
        if (t > pos)
        {
            engine.render (buffer, pos, t - pos, { internalPpq + pos * ppqPerSample, ppqPerSample });
            pos = t;
        }
        handleMidi (metadata.getMessage());
    }
    if (pos < numSamples)
        engine.render (buffer, pos, numSamples - pos, { internalPpq + pos * ppqPerSample, ppqPerSample });

    internalPpq += numSamples * ppqPerSample;

    // ---- Effects ----
    const double delaySamples = rateInBeats ((int) params.dlyTime->load()) * 60.0 / bpm * currentSampleRate;
    delay.process (buffer, numSamples, delaySamples, params.dlyFb->load(), params.dlyMix->load());

    const float revMix = params.revMix->load();
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.3f + 0.69f * params.revSize->load();
    rp.damping = params.revDamp->load();
    rp.wetLevel = 0.33f * revMix;
    rp.dryLevel = 0.5f * (1.0f - 0.5f * revMix); // Reverb scales dry by 2 internally
    rp.width = 1.0f;
    if (! juce::exactlyEqual (rp.roomSize, reverbParams.roomSize) || ! juce::exactlyEqual (rp.damping, reverbParams.damping)
        || ! juce::exactlyEqual (rp.wetLevel, reverbParams.wetLevel) || ! juce::exactlyEqual (rp.dryLevel, reverbParams.dryLevel))
    {
        reverbParams = rp;
        reverb.setParameters (rp);
    }
    if (revMix > 0.0001f)
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    output.process (buffer, numSamples, params.master->load());

    midi.clear();
}

// ---- Samples ------------------------------------------------------------------

void DelibabProcessor::installSample (SampleData::Ptr s)
{
    {
        const juce::ScopedLock pl (poolLock);
        if (s != nullptr)
            releasePool.addIfNotAlreadyThere (s.get());
    }
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        pendingSample = s;
    }
    {
        const juce::ScopedLock il (infoLock);
        lastError.clear();
        missingSamplePath.clear();
    }
    sendChangeMessage();
}

void DelibabProcessor::setSample (SampleData::Ptr newSample)
{
    installSample (std::move (newSample));
}

bool DelibabProcessor::loadSampleSync (const juce::File& file, juce::String& error)
{
    auto s = SampleData::loadFromFile (file, error);
    if (s == nullptr)
        return false;
    installSample (s);
    return true;
}

void DelibabProcessor::loadSampleAsync (const juce::File& file)
{
    ++loadsInFlight;
    sendChangeMessage();

    juce::WeakReference<DelibabProcessor> weakThis (this);
    loader.addJob ([file, weakThis]
    {
        juce::String error;
        auto s = SampleData::loadFromFile (file, error);
        juce::MessageManager::callAsync ([weakThis, s, error]
        {
            if (auto* self = weakThis.get())
            {
                --self->loadsInFlight;
                if (s != nullptr)
                {
                    self->installSample (s);
                }
                else
                {
                    {
                        const juce::ScopedLock il (self->infoLock);
                        self->lastError = error;
                    }
                    self->sendChangeMessage();
                }
            }
        });
    });
}

SampleData::Ptr DelibabProcessor::getCurrentSample() const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return pendingSample;
}

juce::String DelibabProcessor::getLastError() const
{
    const juce::ScopedLock il (infoLock);
    return lastError;
}

juce::String DelibabProcessor::getMissingSamplePath() const
{
    const juce::ScopedLock il (infoLock);
    return missingSamplePath;
}

void DelibabProcessor::timerCallback()
{
    // Free samples nobody uses any more. The pool holds one reference; the
    // audio thread's activeSample and the pending slot hold others.
    const auto current = getCurrentSample();
    const juce::ScopedLock pl (poolLock);
    for (int i = releasePool.size(); --i >= 0;)
    {
        auto* s = releasePool.getObjectPointerUnchecked (i);
        if (s != current.get() && s->getReferenceCount() == 1)
            releasePool.remove (i);
    }
}

// ---- State --------------------------------------------------------------------

void DelibabProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (propVersion, DELIBAB_VERSION_STRING, nullptr);

    juce::ValueTree sampleTree (sampleNode);
    sampleTree.setProperty (propEmbed, embedSample.load(), nullptr);

    if (auto s = getCurrentSample())
    {
        sampleTree.setProperty (propName, s->getName(), nullptr);
        if (s->getFile() != juce::File())
            sampleTree.setProperty (propPath, s->getFile().getFullPathName(), nullptr);
        if (embedSample.load() && s->getEmbeddedFlac().getSize() > 0)
            sampleTree.setProperty (propFlac, s->getEmbeddedFlac().toBase64Encoding(), nullptr);
    }
    else
    {
        // Keep a reference to a missing file so saving doesn't lose it.
        const auto missing = getMissingSamplePath();
        if (missing.isNotEmpty())
            sampleTree.setProperty (propPath, missing, nullptr);
    }

    state.appendChild (sampleTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DelibabProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    auto sampleTree = state.getChildWithName (sampleNode);
    state.removeChild (sampleTree, nullptr);
    apvts.replaceState (state);

    if (! sampleTree.isValid())
        return;

    embedSample.store ((bool) sampleTree.getProperty (propEmbed, true));
    const auto path = sampleTree.getProperty (propPath).toString();
    const auto name = sampleTree.getProperty (propName).toString();
    const auto flacB64 = sampleTree.getProperty (propFlac).toString();

    // Restore synchronously so an offline bounce right after loading a
    // project already has the sample. Prefer the embedded copy.
    SampleData::Ptr restored;
    juce::String error;
    if (flacB64.isNotEmpty())
    {
        juce::MemoryBlock flac;
        if (flac.fromBase64Encoding (flacB64))
            restored = SampleData::loadFromFlacMemory (flac, name, path.isNotEmpty() ? juce::File (path) : juce::File(), error);
    }
    if (restored == nullptr && path.isNotEmpty())
        restored = SampleData::loadFromFile (juce::File (path), error);

    if (restored != nullptr)
    {
        installSample (restored);
    }
    else if (path.isNotEmpty())
    {
        {
            const juce::ScopedLock il (infoLock);
            missingSamplePath = path;
            lastError = "Sample not found: " + path;
        }
        sendChangeMessage();
    }
}

// ---- Editor -------------------------------------------------------------------

juce::AudioProcessorEditor* DelibabProcessor::createEditor()
{
    return new DelibabEditor (*this);
}

} // namespace delibab

// Entry point used by the plugin wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new delibab::DelibabProcessor();
}
