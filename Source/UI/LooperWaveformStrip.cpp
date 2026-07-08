#include "LooperWaveformStrip.h"

#include <cmath>
#include <memory>

#include "Core/Looper/LooperMath.h"
#include "PushLookAndFeel.h"

namespace conduit
{

LooperWaveformStrip::LooperWaveformStrip()
{
    setOpaque (false);

    spectrumTags.fill (-1);
    rebuildSpectrumLut();

    // Ring-Image auf "Stille" initialisieren (LUT-Nullpunkt = Schwarz —
    // Startup zeigt die ruhige LCD-Fläche)
    spectrumImage.clear (spectrumImage.getBounds(), spectrumLut[0]);
}

void LooperWaveformStrip::rebuildSpectrumLut()
{
    // Fire-Palette (Referenz: klassisches Spektrogramm): Schwarz →
    // Tiefrot → Orange (Push-Capture-Familie) → Gelbweiß. Stille (Pegel 0)
    // ist reines Schwarz — der Strip liegt auf schwarzem Grund (LCD-Optik,
    // User-Wunsch 07/2026), leere Bereiche verschwinden darin.
    // Mit gesetzter Quellfarbe (setSourceColour) wird die Palette zur
    // Quelle getönt: Schwarz → dunkle Farbe → Farbe → helle Farbe.
    struct Stop { float position; juce::Colour colour; };

    const Stop fire[] = {
        { 0.00f, juce::Colours::black },
        { 0.35f, juce::Colour (0xff5a1600) },   // Tiefrot-Braun
        { 0.65f, juce::Colour (0xffd35400) },   // Orange
        { 0.85f, juce::Colour (0xffffa726) },   // Capture-Orange (ledOrange)
        { 1.00f, juce::Colour (0xffffe9b8) },   // Gelbweiß
    };

    const Stop tinted[] = {
        { 0.00f, juce::Colours::black },
        { 0.35f, sourceColour.darker (1.8f) },
        { 0.70f, sourceColour },
        { 1.00f, sourceColour.interpolatedWith (juce::Colours::white, 0.65f) },
    };

    const Stop* stops    = sourceColour.isTransparent() ? fire : tinted;
    const std::size_t n  = sourceColour.isTransparent() ? std::size (fire) : std::size (tinted);

    for (int i = 0; i < 256; ++i)
    {
        const auto level = static_cast<float> (i) / 255.0f;
        auto colour = stops[0].colour;
        for (std::size_t s = 0; s + 1 < n; ++s)
        {
            if (level < stops[s].position || level > stops[s + 1].position)
                continue;
            const auto span = stops[s + 1].position - stops[s].position;
            colour = stops[s].colour.interpolatedWith (
                stops[s + 1].colour, span > 0.0f ? (level - stops[s].position) / span : 1.0f);
        }
        spectrumLut[static_cast<std::size_t> (i)] = colour;
    }
}

void LooperWaveformStrip::setSourceColour (juce::Colour colour)
{
    if (sourceColour == colour)
        return;

    sourceColour = colour;
    rebuildSpectrumLut();   // wirkt auf NEUE Spalten (Header-Doku)
    repaint();
}

//==============================================================================
void LooperWaveformStrip::tick()
{
    if (getBeatNow != nullptr)
        beatNow = getBeatNow();

    pullBins();
    clearStaleSpectrumColumns();
    repaint();  // der Strip ist das animierte Element — jede VBlank gleitet
}

void LooperWaveformStrip::pullBins()
{
    if (tap == nullptr)
        return;

    LooperWaveformTap::Bin bin;
    while (tap->pop (bin))
        store (bin);

    LooperWaveformTap::SpectralColumn column;
    if (tap->popSpectrum (column))
    {
        // Eine BitmapData-Sperre für den ganzen Batch (Backfill-Bursts)
        juce::Image::BitmapData pixels { spectrumImage, juce::Image::BitmapData::writeOnly };
        do
            storeSpectrum (column, pixels);
        while (tap->popSpectrum (column));
    }
}

void LooperWaveformStrip::store (const LooperWaveformTap::Bin& bin)
{
    auto& entry = history[static_cast<std::size_t> (
        ((bin.index % historySize) + historySize) % historySize)];
    entry.index    = bin.index;
    entry.minValue = bin.minValue;
    entry.maxValue = bin.maxValue;
}

void LooperWaveformStrip::storeSpectrum (const LooperWaveformTap::SpectralColumn& column,
                                         juce::Image::BitmapData& pixels)
{
    const auto slot = static_cast<int> (
        ((column.index % spectrumRingColumns) + spectrumRingColumns) % spectrumRingColumns);
    spectrumTags[static_cast<std::size_t> (slot)] = column.index;

    // Band 63 (hohe Frequenzen) liegt in Zeile 0 — Spektrogramm-Konvention
    for (int band = 0; band < looper::spectrumBands; ++band)
    {
        const auto level = juce::jlimit (0.0f, 1.0f,
                                         column.bands[static_cast<std::size_t> (band)]);
        const auto lutIndex = static_cast<std::size_t> (
            juce::roundToInt (level * 255.0f));
        pixels.setPixelColour (slot, looper::spectrumBands - 1 - band,
                               spectrumLut[lutIndex]);
    }
}

void LooperWaveformStrip::clearStaleSpectrumColumns()
{
    // Sichtbares Fenster: die letzten 512 Spalten bis "jetzt". Trägt ein
    // Ring-Slot NICHT die Spalte seines Fensters (Startup, Queue-Lücke,
    // Daten älter als der Ring), wird er auf Stille geschwärzt — sonst
    // erschiene dort Material von vor 64 Takten (Ring-Wrap).
    const auto newest = static_cast<std::int64_t> (
        std::floor (beatNow * looper::spectrumColumnsPerBeat));

    std::unique_ptr<juce::Image::BitmapData> pixels;  // lazy — meist ist nichts stale

    for (int offset = 0; offset < LooperWaveformTap::spectrumHistoryColumns; ++offset)
    {
        const auto index = newest - offset;
        if (index < 0)
            break;

        const auto slot = static_cast<std::size_t> (index % spectrumRingColumns);
        if (spectrumTags[slot] == index || spectrumTags[slot] == -1)
            continue;

        if (pixels == nullptr)
            pixels = std::make_unique<juce::Image::BitmapData> (
                spectrumImage, juce::Image::BitmapData::writeOnly);

        for (int row = 0; row < looper::spectrumBands; ++row)
            pixels->setPixelColour (static_cast<int> (slot), row, spectrumLut[0]);

        spectrumTags[slot] = -1;
    }
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
void LooperWaveformStrip::paintWaveform (juce::Graphics& g, juce::Rectangle<float> wave)
{
    const auto midY = wave.getCentreY();
    const auto halfHeight = wave.getHeight() * 0.5f;

    // Wellenform: pro Pixelspalte das Min/Max-Aggregat der Bin-Historie —
    // in der Quellfarbe, falls gesetzt (Kanal-/Node-Farbe, 08.07.2026)
    g.setColour (sourceColour.isTransparent() ? push::colours::ledGreen : sourceColour);
    for (int x = 0; x < getWidth(); ++x)
    {
        float minValue = 0.0f, maxValue = 0.0f;
        if (! aggregateColumn (x, beatNow, minValue, maxValue))
            continue;

        const auto top    = midY - juce::jlimit (0.0f, 1.0f,  maxValue) * halfHeight;
        const auto bottom = midY - juce::jlimit (-1.0f, 0.0f, minValue) * halfHeight;
        g.drawVerticalLine (x, top, juce::jmax (bottom, top + 1.0f));
    }
}

void LooperWaveformStrip::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> wave)
{
    // Pro Segment den Beat-Bereich [beatLo, beatHi) in Ring-Image-Spalten
    // abbilden und als skalierten Blit ziehen. Ring-Wrap = zweiter Blit
    // mit um die Ringbreite verschobener Quelle; alles außerhalb des
    // Segment-Rechtecks schneidet der Clip weg (Klassendoku).
    const auto columnsPerBeat = static_cast<double> (looper::spectrumColumnsPerBeat);
    const auto scaleY = wave.getHeight() / static_cast<float> (looper::spectrumBands);
    const auto segmentWidth = static_cast<float> (getWidth())
                              / static_cast<float> (looper::numSegments);

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

    for (int segment = 0; segment < looper::numSegments; ++segment)
    {
        const auto beatHi = beatNow - looper::segmentRightEdgeBeats (segment);
        const auto span   = looper::segmentSpanBeats (segment);
        const auto beatLo = beatHi - span;

        const auto colLo = beatLo * columnsPerBeat;
        const auto colSpan = span * columnsPerBeat;
        if (colSpan <= 0.0)
            continue;

        const auto pxPerColumn = static_cast<double> (segmentWidth) / colSpan;
        const auto segX = static_cast<double> (segmentWidth) * segment;

        // Quellposition im Ring (positiv gewrappt, Sub-Spalten-genau)
        auto ringLo = std::fmod (colLo, static_cast<double> (spectrumRingColumns));
        if (ringLo < 0.0)
            ringLo += spectrumRingColumns;

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (juce::Rectangle<float> (static_cast<float> (segX), wave.getY(),
                                                    segmentWidth, wave.getHeight())
                                .toNearestInt());

        const auto drawPass = [&] (double imageColumnAtSegX)
        {
            g.drawImageTransformed (
                spectrumImage,
                juce::AffineTransform::translation (static_cast<float> (-imageColumnAtSegX), 0.0f)
                    .scaled (static_cast<float> (pxPerColumn), scaleY)
                    .translated (static_cast<float> (segX), wave.getY()));
        };

        drawPass (ringLo);
        if (ringLo + colSpan > spectrumRingColumns)   // Wrap: zweiter Zug,
            drawPass (ringLo - spectrumRingColumns);  // um die Ringbreite versetzt
    }
}

//==============================================================================
void LooperWaveformStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Reines Schwarz statt Kachelgrau: Wellenform-Grün bzw. Fire-Palette
    // auf schwarzem Grund wirken wie ein LCD-Display (User-Wunsch 07/2026)
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto wave = bounds.withTrimmedBottom (labelRowHeight).reduced (0.0f, 4.0f);

    // Hover-Segment aufhellen (Klick-Ziel = Commit-Länge)
    const auto segmentWidth = bounds.getWidth() / static_cast<float> (looper::numSegments);

    if (view == View::waveform)
        paintWaveform (g, wave);
    else
        paintSpectrum (g, wave);

    if (hoveredSegment >= 0)
    {
        g.setColour (push::colours::text.withAlpha (0.06f));
        g.fillRoundedRectangle (bounds.withX (bounds.getX() + segmentWidth * (float) hoveredSegment)
                                      .withWidth (segmentWidth), 6.0f);
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
