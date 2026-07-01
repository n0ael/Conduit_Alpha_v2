#include "LevelMeterBar.h"

namespace conduit
{

namespace
{
    constexpr int   clipZoneWidth = 14;    // klickbares Clip-Feld am rechten Ende
    constexpr float changeEpsilon = 0.002f; // repaint-Schwelle

    juce::Colour levelColour (float norm)
    {
        // grün bis ~-12 dB, gelb bis ~-3 dB, darüber rot
        if (norm > 0.95f) return juce::Colour (0xffe14b3b);
        if (norm > 0.80f) return juce::Colour (0xffd8b13a);
        return juce::Colour (0xff3fb56b);
    }
}

//==============================================================================
LevelMeterBar::LevelMeterBar (LevelMeter* meterToUse, int channelToShow)
    : meter (meterToUse), channel (channelToShow)
{
    if (meter != nullptr)
        startTimerHz (30);  // Meter-Refresh (CLAUDE.md 10)
}

void LevelMeterBar::stopUpdates()
{
    stopTimer();
}

//==============================================================================
float LevelMeterBar::normFromLinear (float linearGain) noexcept
{
    if (linearGain <= 0.0f)
        return 0.0f;

    const auto db = juce::Decibels::gainToDecibels (linearGain, minDb);
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (0.0f - minDb));
}

//==============================================================================
void LevelMeterBar::timerCallback()
{
    if (meter == nullptr)
        return;

    const auto rms  = meter->getRms (channel);
    const auto peak = meter->getPeak (channel);
    const auto hold = meter->getPeakHold (channel);
    const auto clip = meter->isClipped (channel);

    if (std::abs (rms - lastRms) > changeEpsilon
        || std::abs (peak - lastPeak) > changeEpsilon
        || std::abs (hold - lastHold) > changeEpsilon
        || clip != lastClipped)
    {
        lastRms = rms; lastPeak = peak; lastHold = hold; lastClipped = clip;
        repaint();
    }
}

//==============================================================================
void LevelMeterBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Track-Hintergrund
    g.setColour (juce::Colour (0xff1b1e22));
    g.fillRoundedRectangle (bounds, 2.0f);

    const auto usable = bounds.reduced (1.0f);
    const auto w = usable.getWidth();

    const auto rmsNorm  = normFromLinear (lastRms);
    const auto peakNorm = normFromLinear (lastPeak);
    const auto holdNorm = normFromLinear (lastHold);

    // RMS-Füllung (stetiger Balken), Farbe nach Pegel
    if (rmsNorm > 0.0f)
    {
        auto fill = usable.withWidth (w * rmsNorm);
        g.setColour (levelColour (rmsNorm).withAlpha (0.85f));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    // Peak-Marker-Linie (schnelle Ballistik)
    if (peakNorm > 0.0f)
    {
        const auto x = usable.getX() + w * peakNorm;
        g.setColour (levelColour (peakNorm));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, usable.getY(), 2.0f, usable.getHeight()));
    }

    // Peak-Hold-Marker (dünn, heller)
    if (holdNorm > peakNorm)
    {
        const auto x = usable.getX() + w * holdNorm;
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, usable.getY(), 1.0f, usable.getHeight()));
    }

    // Clip-Feld am 0-dB-Ende (Latch)
    if (lastClipped)
    {
        auto clipRect = bounds.removeFromRight (static_cast<float> (clipZoneWidth));
        g.setColour (juce::Colour (0xffe14b3b));
        g.fillRoundedRectangle (clipRect, 2.0f);
    }
}

//==============================================================================
bool LevelMeterBar::hitTest (int x, int)
{
    // Nur das Clip-Feld ist klickbar (zum Zurücksetzen); sonst fällt der Klick
    // an die Kachel durch (Node-Drag)
    return meter != nullptr && meter->isClipped (channel)
        && x >= getWidth() - clipZoneWidth;
}

void LevelMeterBar::mouseDown (const juce::MouseEvent&)
{
    if (meter != nullptr)
    {
        meter->resetClip (channel);
        lastClipped = false;
        repaint();
    }
}

} // namespace conduit
