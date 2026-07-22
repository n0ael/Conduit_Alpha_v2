#include "TraceView.h"

namespace touchlab
{

TraceView::TraceView()
{
    setInterceptsMouseClicks (false, false); // Input fängt das NativeTouchSource-Overlay
}

void TraceView::addPoint (Lane lane, juce::Point<float> p, bool startsStroke)
{
    auto& buf = lanes[(int) lane];
    buf.pts[(size_t) buf.writeIndex] = { p.x, p.y, startsStroke };

    if (++buf.writeIndex >= cap)
    {
        buf.writeIndex = 0;
        buf.filled = true;
    }

    dirty = true;
}

void TraceView::clearTrails()
{
    for (auto& buf : lanes)
    {
        buf.writeIndex = 0;
        buf.filled = false;
    }
    dirty = true;
    repaint();
}

void TraceView::refreshIfDirty()
{
    if (dirty)
    {
        dirty = false;
        repaint();
    }
}

//==============================================================================
void TraceView::drawDots (juce::Graphics& g, const LaneBuf& buf, juce::Colour c) const
{
    g.setColour (c);
    const int n = buf.filled ? cap : buf.writeIndex;

    for (int k = 0; k < n; ++k)
    {
        const int idx = buf.filled ? (buf.writeIndex + k) % cap : k;
        const auto& pt = buf.pts[(size_t) idx];
        g.fillEllipse (pt.x - 1.5f, pt.y - 1.5f, 3.0f, 3.0f);
    }
}

void TraceView::drawLine (juce::Graphics& g, const LaneBuf& buf, juce::Colour c) const
{
    const int n = buf.filled ? cap : buf.writeIndex;
    if (n < 2)
        return;

    juce::Path path;
    bool penDown = false;

    for (int k = 0; k < n; ++k)
    {
        const int idx = buf.filled ? (buf.writeIndex + k) % cap : k;
        const auto& pt = buf.pts[(size_t) idx];

        if (pt.startsStroke || ! penDown)
        {
            path.startNewSubPath (pt.x, pt.y);
            penDown = true;
        }
        else
        {
            path.lineTo (pt.x, pt.y);
        }
    }

    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

//==============================================================================
void TraceView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xff15171a));
    g.fillRoundedRectangle (bounds, 4.0f);

    // dezentes Fadenkreuz
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (juce::roundToInt (bounds.getCentreY()), bounds.getX(), bounds.getRight());
    g.drawVerticalLine   (juce::roundToInt (bounds.getCentreX()), bounds.getY(), bounds.getBottom());

    // Rohpunkte
    const auto nativeCol = blindActive ? juce::Colour (0xff9aa0a6) : juce::Colour (0xff42d0ff); // cyan
    const auto rawCol    = blindActive ? juce::Colour (0xff9aa0a6) : juce::Colour (0xffffa53d); // orange
    drawDots (g, lanes[(int) Lane::NativeRaw], nativeCol.withAlpha (0.45f));
    drawDots (g, lanes[(int) Lane::RawRaw],    rawCol.withAlpha (0.45f));

    // verarbeitete Spuren
    drawLine (g, lanes[(int) Lane::NativeFiltered], juce::Colours::white.withAlpha (0.9f));
    drawLine (g, lanes[(int) Lane::RawFiltered],    juce::Colour (0xff7ee081).withAlpha (0.9f));

    // Legende
    g.setFont (12.0f);
    auto legend = bounds.reduced (8.0f).removeFromTop (16.0f);
    if (blindActive)
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String::fromUTF8 ("Blind-Modus — Quellen verdeckt"),
                    legend, juce::Justification::topLeft);
    }
    else
    {
        g.setColour (nativeCol);
        g.drawText ("Nativ (roh) • weiß=gefiltert", legend, juce::Justification::topLeft);
        g.setColour (rawCol);
        g.drawText ("Raw-Pointer (roh) • grün=gefiltert",
                    legend.translated (0.0f, 16.0f), juce::Justification::topLeft);
    }
}

} // namespace touchlab
