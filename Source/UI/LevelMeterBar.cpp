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
LevelMeterBar::LevelMeterBar (LevelMeter* meterToUse, int channelToShow, int lanesToShow)
    : meter (meterToUse), channel (channelToShow),
      lanes (juce::jlimit (1, 2, lanesToShow))
{
    // Meter-Refresh: UiFramePacer (nativ per VBlank, global gedrosselt);
    // meter == nullptr (Tests) -> der Tick ist ein No-op.
}

void LevelMeterBar::stopUpdates()
{
    framePacer.stop();
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
void LevelMeterBar::refreshTick()
{
    if (meter == nullptr)
        return;

    // Vergleich im ANZEIGE-Maßstab (dB-Norm) — s. Member-Kommentar.
    bool changed = false;
    bool clip = false;

    for (int lane = 0; lane < lanes; ++lane)
    {
        const auto ch = channel + lane;
        const auto idx = static_cast<std::size_t> (lane);
        const auto rmsNorm  = normFromLinear (meter->getRms (ch));
        const auto peakNorm = normFromLinear (meter->getPeak (ch));
        const auto holdNorm = normFromLinear (meter->getPeakHold (ch));
        clip = clip || meter->isClipped (ch);

        changed = changed
               || std::abs (rmsNorm - lastRmsNorm[idx]) > changeEpsilon
               || std::abs (peakNorm - lastPeakNorm[idx]) > changeEpsilon
               || std::abs (holdNorm - lastHoldNorm[idx]) > changeEpsilon;

        lastRmsNorm[idx] = rmsNorm;
        lastPeakNorm[idx] = peakNorm;
        lastHoldNorm[idx] = holdNorm;
    }

    if (changed || clip != lastClipped)
    {
        lastClipped = clip;
        repaint();
    }
}

//==============================================================================
void LevelMeterBar::paintLane (juce::Graphics& g, juce::Rectangle<float> usable, int lane)
{
    const auto idx = static_cast<std::size_t> (lane);
    const auto w = usable.getWidth();

    const auto rmsNorm  = lastRmsNorm[idx];
    const auto peakNorm = lastPeakNorm[idx];
    const auto holdNorm = lastHoldNorm[idx];

    // Peak-Balken (schnelle Ballistik) UNTER der RMS-Füllung, halb so hell
    // (User-Feintuning 14.07.2026: Balken statt Marker-Linie).
    if (peakNorm > 0.0f)
    {
        auto fill = usable.withWidth (w * peakNorm);
        g.setColour (levelColour (peakNorm).withAlpha (0.4f));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    // RMS-Füllung (stetiger Balken), Farbe nach Pegel
    if (rmsNorm > 0.0f)
    {
        auto fill = usable.withWidth (w * rmsNorm);
        g.setColour (levelColour (rmsNorm).withAlpha (0.85f));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    // Peak-Hold-Marker (dünn, heller)
    if (holdNorm > peakNorm)
    {
        const auto x = usable.getX() + w * holdNorm;
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, usable.getY(), 1.0f, usable.getHeight()));
    }
}

void LevelMeterBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Track-Hintergrund
    g.setColour (juce::Colour (0xff1b1e22));
    g.fillRoundedRectangle (bounds, 2.0f);

    const auto usable = bounds.reduced (1.0f);

    if (lanes == 1)
    {
        paintLane (g, usable, 0);
    }
    else
    {
        // Kompakte Stereo-Variante: L oben, R unten (1 px Fuge)
        const auto laneHeight = (usable.getHeight() - 1.0f) / 2.0f;
        paintLane (g, usable.withHeight (laneHeight), 0);
        paintLane (g, usable.withTrimmedTop (laneHeight + 1.0f), 1);
    }

    // Clip-Feld am 0-dB-Ende (Latch, bei Stereo gemeinsam)
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
    return lastClipped && x >= getWidth() - clipZoneWidth;
}

void LevelMeterBar::mouseDown (const juce::MouseEvent&)
{
    if (meter != nullptr)
    {
        for (int lane = 0; lane < lanes; ++lane)
            meter->resetClip (channel + lane);
        lastClipped = false;
        repaint();
    }
}

} // namespace conduit
