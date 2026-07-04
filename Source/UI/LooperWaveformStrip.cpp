#include "LooperWaveformStrip.h"

#include <cmath>

#include "Core/Looper/LooperMath.h"
#include "PushLookAndFeel.h"

namespace conduit
{

LooperWaveformStrip::LooperWaveformStrip()
{
    setOpaque (false);
}

//==============================================================================
void LooperWaveformStrip::tick()
{
    if (getBeatNow != nullptr)
        beatNow = getBeatNow();

    pullBins();
    repaint();  // der Strip ist das animierte Element — jede VBlank gleitet
}

void LooperWaveformStrip::pullBins()
{
    if (tap == nullptr)
        return;

    LooperWaveformTap::Bin bin;
    while (tap->pop (bin))
        store (bin);
}

void LooperWaveformStrip::store (const LooperWaveformTap::Bin& bin)
{
    auto& entry = history[static_cast<std::size_t> (
        ((bin.index % historySize) + historySize) % historySize)];
    entry.index    = bin.index;
    entry.minValue = bin.minValue;
    entry.maxValue = bin.maxValue;
}

bool LooperWaveformStrip::aggregateColumn (int x, double beatAtRightEdge,
                                           float& minOut, float& maxOut) const
{
    const auto width = static_cast<double> (getWidth());
    if (width <= 0.0)
        return false;

    // Spalte deckt Beats [beatNow − offset(x), beatNow − offset(x+1)) —
    // im gestauchten linken Segment fallen mehrere Bins auf ein Pixel
    const auto beatLo = beatAtRightEdge - looper::beatOffsetForX (x, width);
    const auto beatHi = beatAtRightEdge - looper::beatOffsetForX (x + 1.0, width);

    const auto binLo = static_cast<std::int64_t> (
        std::floor (beatLo * LooperWaveformTap::binsPerBeat));
    const auto binHi = static_cast<std::int64_t> (
        std::floor (beatHi * LooperWaveformTap::binsPerBeat));

    bool anyData = false;
    for (auto bin = binLo; bin <= binHi; ++bin)
    {
        if (bin < 0)
            continue;

        const auto& entry = history[static_cast<std::size_t> (bin % historySize)];
        if (entry.index != bin)
            continue;  // leer oder von einem jüngeren Bin überschrieben

        minOut = anyData ? juce::jmin (minOut, entry.minValue) : entry.minValue;
        maxOut = anyData ? juce::jmax (maxOut, entry.maxValue) : entry.maxValue;
        anyData = true;
    }

    return anyData;
}

//==============================================================================
void LooperWaveformStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto wave = bounds.withTrimmedBottom (labelRowHeight).reduced (0.0f, 4.0f);
    const auto midY = wave.getCentreY();
    const auto halfHeight = wave.getHeight() * 0.5f;

    // Hover-Segment aufhellen (Klick-Ziel = Commit-Länge)
    const auto segmentWidth = bounds.getWidth() / static_cast<float> (looper::numSegments);
    if (hoveredSegment >= 0)
    {
        g.setColour (push::colours::text.withAlpha (0.06f));
        g.fillRoundedRectangle (bounds.withX (bounds.getX() + segmentWidth * (float) hoveredSegment)
                                      .withWidth (segmentWidth), 6.0f);
    }

    // Wellenform: pro Pixelspalte das Min/Max-Aggregat der Bin-Historie
    g.setColour (push::colours::ledGreen);
    for (int x = 0; x < getWidth(); ++x)
    {
        float minValue = 0.0f, maxValue = 0.0f;
        if (! aggregateColumn (x, beatNow, minValue, maxValue))
            continue;

        const auto top    = midY - juce::jlimit (0.0f, 1.0f,  maxValue) * halfHeight;
        const auto bottom = midY - juce::jlimit (-1.0f, 0.0f, minValue) * halfHeight;
        g.drawVerticalLine (x, top, juce::jmax (bottom, top + 1.0f));
    }

    // Segment-Grenzen + Labels ("8 Bars | 4 Bars | 2 Bars | 1 Bar")
    g.setColour (push::colours::textDim.withAlpha (0.4f));
    for (int i = 1; i < looper::numSegments; ++i)
        g.drawVerticalLine (static_cast<int> (bounds.getX() + segmentWidth * (float) i),
                            wave.getY(), wave.getBottom());

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (14.0f));
    for (int i = 0; i < looper::numSegments; ++i)
    {
        const auto bars = looper::barsForSegment (i);
        const auto label = juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
        g.drawText (label,
                    juce::Rectangle<float> (bounds.getX() + segmentWidth * (float) i,
                                            bounds.getBottom() - labelRowHeight,
                                            segmentWidth, labelRowHeight - 4.0f),
                    juce::Justification::centred);
    }
}

//==============================================================================
void LooperWaveformStrip::mouseUp (const juce::MouseEvent& event)
{
    if (! getLocalBounds().contains (event.getPosition()))
        return;

    const auto segment = looper::segmentForX (event.position.x, (double) getWidth());
    if (onSegmentClicked != nullptr)
        onSegmentClicked (looper::barsForSegment (segment));
}

void LooperWaveformStrip::mouseMove (const juce::MouseEvent& event)
{
    const auto segment = looper::segmentForX (event.position.x, (double) getWidth());
    if (segment != hoveredSegment)
    {
        hoveredSegment = segment;
        repaint();
    }
}

void LooperWaveformStrip::mouseExit (const juce::MouseEvent&)
{
    hoveredSegment = -1;
    repaint();
}

} // namespace conduit
