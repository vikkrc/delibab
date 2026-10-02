#include "GlobalPanel.h"

namespace delibab
{
GlobalPanel::GlobalPanel (juce::AudioProcessorValueTreeState& state)
{
    constexpr int k = 60;

    attack.attach (state, ids::attack);
    decay.attach (state, ids::decay);
    sustain.attach (state, ids::sustain);
    release.attach (state, ids::release);
    root.attach (state, ids::rootNote);
    key.attach (state, ids::scaleKey);
    scale.attach (state, ids::scaleType);
    dlyMix.attach (state, ids::dlyMix);
    dlyTime.attach (state, ids::dlyTime);
    dlyFb.attach (state, ids::dlyFb);
    revMix.attach (state, ids::revMix);
    revSize.attach (state, ids::revSize);
    revDamp.attach (state, ids::revDamp);
    master.attach (state, ids::master);

    root.slider.setTooltip ("The note at which the sample plays at its original pitch");

    for (auto* knob : { &attack, &decay, &sustain, &release, &root, &dlyMix, &dlyFb,
                        &revMix, &revSize, &revDamp, &master })
        knob->setAccent (theme::text.withAlpha (0.85f));

    ampGroup.add (attack, k);
    ampGroup.add (decay, k);
    ampGroup.add (sustain, k);
    ampGroup.add (release, k);

    scaleGroup.add (root, k);
    scaleGroup.add (key, 64);
    scaleGroup.add (scale, 108);

    delayGroup.add (dlyMix, k);
    delayGroup.add (dlyTime, 70);
    delayGroup.add (dlyFb, k);

    reverbGroup.add (revMix, k);
    reverbGroup.add (revSize, k);
    reverbGroup.add (revDamp, k);

    outGroup.add (master, k);

    for (auto* g : { &ampGroup, &scaleGroup, &delayGroup, &reverbGroup, &outGroup })
        addAndMakeVisible (g);
}

void GlobalPanel::resized()
{
    auto r = getLocalBounds();
    for (auto* g : { &ampGroup, &scaleGroup, &delayGroup, &reverbGroup })
    {
        g->setBounds (r.removeFromLeft (g->getIdealWidth()));
        r.removeFromLeft (8);
    }
    outGroup.setBounds (r);
}

} // namespace delibab
