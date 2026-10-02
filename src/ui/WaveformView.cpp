#include "WaveformView.h"

namespace delibab
{
WaveformView::WaveformView (DelibabProcessor& p) : processor (p)
{
    particles.reserve (1024);
    sampleChanged();
    startTimerHz (60);
}

juce::Rectangle<float> WaveformView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f, 10.0f);
}

void WaveformView::sampleChanged()
{
    sample = processor.getCurrentSample();
    particles.clear();
    rebuildWaveImage();
    repaint();
}

void WaveformView::resized()
{
    rebuildWaveImage();
}

void WaveformView::rebuildWaveImage()
{
    const auto area = plotArea();
    if (sample == nullptr || area.getWidth() < 4 || area.getHeight() < 4)
    {
        waveImage = {};
        return;
    }

    const float scale = 2.0f; // draw at 2x for crisp scaling
    const int w = juce::roundToInt (area.getWidth() * scale);
    const int h = juce::roundToInt (area.getHeight() * scale);
    waveImage = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (waveImage);

    const auto& ov = sample->getOverview();
    float peak = 0.001f;
    for (auto [lo, hi] : ov)
        peak = juce::jmax (peak, -lo, hi);

    const float mid = (float) h * 0.5f;
    const float amp = (float) h * 0.46f / peak;
    juce::Path path;
    for (int x = 0; x < w; ++x)
    {
        const auto i0 = (size_t) ((juce::int64) x * (juce::int64) ov.size() / w);
        const auto i1 = juce::jmax (i0 + 1, (size_t) ((juce::int64) (x + 1) * (juce::int64) ov.size() / w));
        float lo = 0.0f, hi = 0.0f;
        for (auto i = i0; i < juce::jmin (i1, ov.size()); ++i)
        {
            lo = juce::jmin (lo, ov[i].first);
            hi = juce::jmax (hi, ov[i].second);
        }
        path.addRectangle ((float) x, mid - hi * amp, 1.0f, juce::jmax (1.0f, (hi - lo) * amp));
    }
    g.setColour (theme::wave);
    g.fillPath (path);
}

void WaveformView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = lastFrameMs == 0 ? 1.0f / 60.0f : juce::jlimit (0.001f, 0.1f, (float) (now - lastFrameMs) * 0.001f);
    lastFrameMs = now;

    const auto area = plotArea();

    // New grains -> particles
    processor.getEngine().getEvents().popAll ([&] (const GrainEvent& e)
    {
        if (particles.size() >= 900)
            particles.erase (particles.begin(), particles.begin() + 100);

        // Height = pitch: +/- 24 semitones spans the view.
        const float yNorm = 0.5f - juce::jlimit (-24.0f, 24.0f, e.semitones) / 48.0f * 0.9f;
        Particle pt;
        pt.x = area.getX() + e.position * area.getWidth();
        pt.y = area.getY() + yNorm * area.getHeight() + (rng.nextFloat() - 0.5f) * 10.0f;
        pt.vx = e.pan * 14.0f;
        pt.size = juce::jlimit (2.0f, 9.0f, 2.0f + std::sqrt (e.duration * 1000.0f) * 0.35f) * juce::jlimit (0.4f, 1.3f, 0.5f + e.gain);
        pt.life = juce::jlimit (0.18f, 1.6f, e.duration * 1.4f);
        pt.age = 0.0f;
        pt.layer = e.layer;
        particles.push_back (pt);
    });

    for (auto& pt : particles)
    {
        pt.age += dt;
        pt.x += pt.vx * dt;
        pt.y -= 6.0f * dt; // heat-haze drift upwards
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(),
                                     [] (const Particle& pt) { return pt.age >= pt.life; }),
                     particles.end());

    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme::edge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const auto area = plotArea();
    auto& state = processor.getState();

    if (sample == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (16.0f));
        const auto missing = processor.getMissingSamplePath();
        const auto error = processor.getLastError();
        juce::String msg = processor.isLoading() ? "Loading..." : "Drop an audio file here, or click to browse";
        if (error.isNotEmpty() && ! processor.isLoading())
            msg = error;
        g.drawFittedText (msg, area.toNearestInt(), juce::Justification::centred, 2);
        return;
    }

    // Centre line + waveform
    g.setColour (theme::edge);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    if (waveImage.isValid())
        g.drawImage (waveImage, area, juce::RectanglePlacement::stretchToFit);

    // Slice markers, if any active layer uses Slice mode
    bool anySlice = false;
    for (int l = 0; l < kNumLayers; ++l)
        if (state.getRawParameterValue (ids::layer (l, ids::on))->load() > 0.5f
            && (int) state.getRawParameterValue (ids::layer (l, ids::mode))->load() == (int) LayerMode::slice)
            anySlice = true;
    if (anySlice)
    {
        g.setColour (theme::text.withAlpha (0.18f));
        const auto len = (float) sample->getLength();
        for (auto onset : sample->getOnsets())
        {
            const float x = area.getX() + (float) onset / len * area.getWidth();
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        }
    }

    // Layer regions: position marker and spray band
    for (int l = 0; l < kNumLayers; ++l)
    {
        if (state.getRawParameterValue (ids::layer (l, ids::on))->load() < 0.5f)
            continue;
        const auto colour = theme::layer (l);
        const bool selected = l == selectedLayer;
        const float pos = state.getRawParameterValue (ids::layer (l, ids::position))->load();
        const float spray = state.getRawParameterValue (ids::layer (l, ids::spray))->load();
        const float live = processor.getEngine().getLayerPlayhead (l);

        const float x = area.getX() + pos * area.getWidth();
        const float halfW = spray * 0.5f * area.getWidth();
        g.setColour (colour.withAlpha (selected ? 0.12f : 0.06f));
        g.fillRect (juce::Rectangle<float> (x - halfW, area.getY(), halfW * 2.0f, area.getHeight()).getIntersection (area));
        g.setColour (colour.withAlpha (selected ? 0.95f : 0.5f));
        g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());

        // Handle
        juce::Path handle;
        handle.addTriangle (x - 6.0f, area.getY() - 2.0f, x + 6.0f, area.getY() - 2.0f, x, area.getY() + 7.0f);
        g.fillPath (handle);

        // Where grains are actually coming from right now (scan moves it)
        if (processor.getEngine().getActiveVoiceCount() > 0)
        {
            const float lx = area.getX() + live * area.getWidth();
            g.setColour (colour.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (lx), area.getY(), area.getBottom());
        }
    }

    // Particles
    for (const auto& pt : particles)
    {
        const float t = pt.age / pt.life;
        const float alpha = (t < 0.1f ? t / 0.1f : 1.0f - (t - 0.1f) / 0.9f) * 0.85f;
        const float s = pt.size * (0.7f + 0.6f * t);
        g.setColour (theme::layer (pt.layer).withAlpha (alpha * 0.25f));
        g.fillEllipse (pt.x - s, pt.y - s, s * 2.0f, s * 2.0f);
        g.setColour (theme::layer (pt.layer).withAlpha (alpha));
        g.fillEllipse (pt.x - s * 0.4f, pt.y - s * 0.4f, s * 0.8f, s * 0.8f);
    }

    // Sample name
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (sample->getName() + "   " + juce::String (sample->getLengthSeconds(), 2) + " s   "
                    + juce::String (sample->getSampleRate() / 1000.0, 1) + " kHz",
                area.toNearestInt().removeFromBottom (16).reduced (4, 0), juce::Justification::bottomLeft);
}

void WaveformView::setPositionFromMouse (const juce::MouseEvent& e)
{
    if (draggingParam == nullptr)
        return;
    const auto area = plotArea();
    const float pos = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
    draggingParam->setValueNotifyingHost (draggingParam->convertTo0to1 (pos));
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (sample == nullptr)
    {
        if (onEmptyClick) onEmptyClick();
        return;
    }
    draggingParam = processor.getState().getParameter (ids::layer (selectedLayer, ids::position));
    if (draggingParam != nullptr)
    {
        draggingParam->beginChangeGesture();
        setPositionFromMouse (e);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    setPositionFromMouse (e);
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (draggingParam != nullptr)
        draggingParam->endChangeGesture();
    draggingParam = nullptr;
}

} // namespace delibab
