#include "LooperTrackStrip.h"

namespace conduit
{

namespace
{
    constexpr int headerHeight = 24;
    constexpr int meterHeight  = 26;   // Mitte-raus-Stereo-Meter (ex Fader-Zeile)
    constexpr int tileRow      = 44;   // Touch-Target (CLAUDE.md 10)
    constexpr int footerHeight = 44;
    constexpr int cellGap      = 4;

    constexpr int xyFullHeight    = 96;   // XY-Panner (1–2 Tracks)
    constexpr int xyCompactHeight = 56;   // Kompakt (3–4 Tracks, FAR-Label weg)

    constexpr float faderDragRange = 150.0f;   // Pixel für vollen Gain-Weg
    constexpr juce::uint32 longPressMs = 600;
}

//==============================================================================
void LooperXyPad::setValues (float newPan, float newDistance)
{
    const auto clampedPan = juce::jlimit (-1.0f, 1.0f, newPan);
    const auto clampedDistance = juce::jlimit (0.0f, 1.0f, newDistance);
    if (juce::exactlyEqual (panValue, clampedPan)
        && juce::exactlyEqual (distanceValue, clampedDistance))
        return;

    panValue = clampedPan;
    distanceValue = clampedDistance;
    repaint();
}

void LooperXyPad::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;
    compact = shouldBeCompact;
    repaint();
}

void LooperXyPad::setSendLevels (const std::array<float, 4>& levels, int visibleSendCount)
{
    bool changed = sendCount != visibleSendCount;
    for (std::size_t s = 0; s < levels.size(); ++s)
        changed = changed || std::abs (sendLevels[s] - levels[s]) > 0.004f;
    if (! changed)
        return;

    sendLevels = levels;
    sendCount = visibleSendCount;
    repaint();
}

void LooperXyPad::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Gekreuzte Diagonalen (Geometrie der Referenz-SVGs) — dezent
    g.setColour (push::colours::outline.withAlpha (0.6f));
    g.drawLine ({ bounds.getTopLeft(), bounds.getBottomRight() }, 1.0f);
    g.drawLine ({ bounds.getBottomLeft(), bounds.getTopRight() }, 1.0f);

    if (! compact)
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (9.0f));
        g.drawText ("FAR", bounds.reduced (4.0f, 2.0f),
                    juce::Justification::topRight, false);
        g.drawText ("L", bounds.reduced (4.0f, 2.0f),
                    juce::Justification::bottomLeft, false);
        g.drawText ("C", bounds.reduced (4.0f, 2.0f),
                    juce::Justification::centredBottom, false);
        g.drawText ("R", bounds.reduced (4.0f, 2.0f),
                    juce::Justification::bottomRight, false);
    }

    // Puck: Ring + Send-Farb-Overlays (Alpha = Level, Referenz-SVG)
    const auto radius = compact ? 8.0f : 12.0f;
    const auto inner = bounds.reduced (radius + 2.0f);
    const auto centre = juce::Point<float> (
        inner.getX() + inner.getWidth() * (panValue * 0.5f + 0.5f),
        inner.getBottom() - inner.getHeight() * distanceValue);
    const auto puck = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                          .withCentre (centre);

    g.setColour (push::colours::background);
    g.fillEllipse (puck);
    for (int s = 0; s < juce::jmin (4, sendCount); ++s)
        if (sendLevels[(std::size_t) s] > 0.001f)
        {
            g.setColour (looperui::sendColour (s)
                             .withAlpha (0.25f + 0.6f * sendLevels[(std::size_t) s]));
            g.fillEllipse (puck.reduced (2.0f));
        }
    g.setColour (juce::Colour (0xff9d9d9d));
    g.drawEllipse (puck, 2.0f);
}

void LooperXyPad::mouseDown (const juce::MouseEvent& event)
{
    applyFromPosition (event.position, true);
}

void LooperXyPad::mouseDrag (const juce::MouseEvent& event)
{
    applyFromPosition (event.position, true);
}

void LooperXyPad::mouseDoubleClick (const juce::MouseEvent&)
{
    setValues (0.0f, 0.0f);
    if (onChanged != nullptr)
        onChanged (panValue, distanceValue);
}

void LooperXyPad::applyFromPosition (juce::Point<float> position, bool notify)
{
    const auto radius = compact ? 8.0f : 12.0f;
    const auto inner = getLocalBounds().toFloat().reduced (1.0f).reduced (radius + 2.0f);
    if (inner.getWidth() <= 0.0f || inner.getHeight() <= 0.0f)
        return;

    const auto newPan = juce::jlimit (-1.0f, 1.0f,
        ((position.x - inner.getX()) / inner.getWidth()) * 2.0f - 1.0f);
    const auto newDistance = juce::jlimit (0.0f, 1.0f,
        (inner.getBottom() - position.y) / inner.getHeight());

    if (juce::exactlyEqual (panValue, newPan)
        && juce::exactlyEqual (distanceValue, newDistance))
        return;

    panValue = newPan;
    distanceValue = newDistance;
    repaint();

    if (notify && onChanged != nullptr)
        onChanged (panValue, distanceValue);
}

//==============================================================================
LooperSendTile::LooperSendTile (int sendIndexToUse)
    : sendIndex (sendIndexToUse)
{
    setName ("looperSendTile" + juce::String (sendIndexToUse + 1));
}

void LooperSendTile::setLevel (float level01)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, level01);
    if (juce::exactlyEqual (level, clamped))
        return;
    level = clamped;
    repaint();
}

void LooperSendTile::setShowLabel (bool show)
{
    if (showLabel == show)
        return;
    showLabel = show;
    repaint();
}

void LooperSendTile::setYLinked (bool linked)
{
    if (yLinked == linked)
        return;
    yLinked = linked;
    repaint();
}

juce::Path LooperSendTile::shapePath (juce::Rectangle<float> bounds) const
{
    juce::Path path;
    switch (sendIndex)
    {
        case 0:   // S1 ●
            path.addEllipse (bounds);
            break;
        case 1:   // S2 ■
            path.addRectangle (bounds);
            break;
        case 2:   // S3 ▲
            path.addTriangle (bounds.getCentreX(), bounds.getY(),
                              bounds.getRight(), bounds.getBottom(),
                              bounds.getX(), bounds.getBottom());
            break;
        default:  // S4 ⬡ (regelmäßiges Sechseck, Spitze oben)
            for (int corner = 0; corner < 6; ++corner)
            {
                const auto angle = juce::MathConstants<float>::pi / 3.0f
                                       * static_cast<float> (corner);
                const auto point = bounds.getCentre()
                    + juce::Point<float> (std::sin (angle), -std::cos (angle))
                          * (bounds.getWidth() * 0.5f);
                if (corner == 0)
                    path.startNewSubPath (point);
                else
                    path.lineTo (point);
            }
            path.closeSubPath();
            break;
    }
    return path;
}

void LooperSendTile::paint (juce::Graphics& g)
{
    // 38-px-Optik in der (≥44 px) Hit-Fläche
    auto tile = getLocalBounds().toFloat();
    tile = tile.withSizeKeepingCentre (juce::jmin (tile.getWidth(), 44.0f),
                                       juce::jmin (tile.getHeight(), 38.0f));
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (tile, 4.0f);

    const auto side = juce::jmin (20.0f, tile.getHeight() - 12.0f);
    auto iconBox = juce::Rectangle<float> (side, side);
    iconBox.setCentre (showLabel ? juce::Point<float> (tile.getX() + 8.0f + side * 0.5f,
                                                       tile.getCentreY())
                                 : tile.getCentre());

    const auto shape = shapePath (iconBox);
    const auto colour = looperui::sendColour (sendIndex);

    // Füllstand von unten (Referenz-SVGs): Clip auf die untere Teilfläche
    if (level > 0.001f)
    {
        juce::Graphics::ScopedSaveState clipState (g);
        g.reduceClipRegion (iconBox.withTop (iconBox.getBottom()
                                             - iconBox.getHeight() * level)
                                .getSmallestIntegerContainer());
        g.setColour (colour);
        g.fillPath (shape);
    }

    g.setColour (level > 0.001f ? colour : push::colours::textDim);
    g.strokePath (shape, juce::PathStrokeType (1.4f));

    if (showLabel)
    {
        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (11.0f));
        g.drawText ("S" + juce::String (sendIndex + 1),
                    tile.reduced (8.0f, 0.0f).withLeft (iconBox.getRight() + 6.0f),
                    juce::Justification::centredLeft, false);
    }

    if (yLinked)
    {
        g.setColour (push::colours::ledCyan);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f)
                           .withCentre ({ tile.getRight() - 6.0f, tile.getY() + 6.0f }));
    }
}

void LooperSendTile::mouseDown (const juce::MouseEvent& event)
{
    dragStartLevel = level;
    dragStartPosition = event.getPosition();
}

void LooperSendTile::mouseDrag (const juce::MouseEvent& event)
{
    const auto delta = (float) (dragStartPosition.y - event.getPosition().y);
    const auto next = juce::jlimit (0.0f, 1.0f, dragStartLevel + delta / faderDragRange);
    if (juce::exactlyEqual (next, level))
        return;

    level = next;
    repaint();
    if (onLevelChanged != nullptr)
        onLevelChanged (level);
}

void LooperSendTile::mouseDoubleClick (const juce::MouseEvent&)
{
    setLevel (0.0f);
    if (onLevelChanged != nullptr)
        onLevelChanged (0.0f);
}

//==============================================================================
LooperSlotCell::LooperSlotCell()
{
    setName ("looperSlotCell");
}

void LooperSlotCell::setState (const State& newState)
{
    const auto changed = state.hasClip != newState.hasClip
                      || state.playing != newState.playing
                      || state.target != newState.target
                      || state.active != newState.active
                      || state.reversed != newState.reversed
                      || std::abs (state.progress01 - newState.progress01) > 0.005f
                      || std::abs (state.loopStart01 - newState.loopStart01) > 0.004f
                      || std::abs (state.loopLen01 - newState.loopLen01) > 0.004f
                      || state.divBadge != newState.divBadge
                      || state.label != newState.label
                      || state.rateBadge != newState.rateBadge;
    state = newState;
    if (changed)
        repaint();
}

void LooperSlotCell::setProgress (float progress01)
{
    if (! state.playing || juce::exactlyEqual (state.progress01, progress01))
        return;

    state.progress01 = progress01;
    repaint();
}

float LooperSlotCell::computeInkCoverage (const juce::Image& ink,
                                          juce::Rectangle<float> normalisedZone)
{
    if (! ink.isValid())
        return 0.0f;

    const auto zone = normalisedZone
                          .transformedBy (juce::AffineTransform::scale (
                              (float) ink.getWidth(), (float) ink.getHeight()))
                          .getSmallestIntegerContainer()
                          .getIntersection (ink.getBounds());
    if (zone.isEmpty())
        return 0.0f;

    const juce::Image::BitmapData pixels { ink, zone.getX(), zone.getY(),
                                           zone.getWidth(), zone.getHeight() };

    std::int64_t alphaSum = 0;
    for (int y = 0; y < zone.getHeight(); ++y)
        for (int x = 0; x < zone.getWidth(); ++x)
            alphaSum += pixels.getPixelColour (x, y).getAlpha();

    return static_cast<float> (alphaSum)
           / (255.0f * static_cast<float> (zone.getWidth())
                     * static_cast<float> (zone.getHeight()));
}

void LooperSlotCell::setThumbnail (juce::Image inkImage, juce::Colour background,
                                   juce::uint32 clipId, juce::String sourceLabel)
{
    thumbnail = std::move (inkImage);
    thumbnailBackground = background;
    thumbnailClipId = clipId;
    thumbnailSourceLabel = std::move (sourceLabel);

    // Kopfzeilen-Kontrast einmal beim Commit messen (nie in paint):
    // Dreieck-Ecke oben links + volle obere Zone für Label/Badges
    iconZoneInk     = computeInkCoverage (thumbnail, { 0.0f, 0.0f, 0.18f, 0.35f });
    headlineZoneInk = computeInkCoverage (thumbnail, { 0.0f, 0.0f, 1.0f,  0.35f });

    repaint();
}

void LooperSlotCell::clearThumbnail()
{
    if (! thumbnail.isValid())
        return;   // no-op ohne Repaint — läuft im 30-Hz-Timer

    thumbnail = {};
    thumbnailBackground = {};
    thumbnailClipId = 0;
    thumbnailSourceLabel = {};
    iconZoneInk = 0.0f;
    headlineZoneInk = 0.0f;
    repaint();
}

void LooperSlotCell::setPulsePhase (float phase01)
{
    pulsePhase = phase01;
    if (state.target)
        repaint();   // nur der pulsierende Zustand animiert
}

void LooperSlotCell::mouseUp (const juce::MouseEvent& event)
{
    if (getLocalBounds().contains (event.getPosition()) && onTap != nullptr)
        onTap();
}

void LooperSlotCell::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    // Invertierte Strip-Optik mit Thumbnail (User-Idee 09.07.2026): die
    // Fläche trägt die Quellfarbe, die geschnappte Waveform/Spektrum-
    // Tinte und alle Aufbauten (Sweep, Dreieck, Text) wechseln auf Schwarz
    const auto inked = state.hasClip && thumbnail.isValid();

    g.setColour (inked ? thumbnailBackground : push::colours::tile);
    g.fillRoundedRectangle (bounds, 4.0f);

    if (state.hasClip)
    {
        if (inked)
            g.drawImage (thumbnail, bounds.reduced (2.0f),
                         juce::RectanglePlacement::stretchToFit);

        // LEN/POS (07/2026): der NICHT aktiv loopende Clip-Teil dunkelt
        // ab — das Fenster bleibt hell (Handoff „abgedunkelt")
        if (state.loopLen01 < 0.999f || state.loopStart01 > 0.001f)
        {
            const auto window = bounds.reduced (2.0f);
            const auto startX = window.getX() + window.getWidth() * state.loopStart01;
            const auto endX = juce::jmin (window.getRight(),
                                          startX + window.getWidth() * state.loopLen01);
            g.setColour ((inked ? juce::Colours::black : push::colours::background)
                             .withAlpha (0.55f));
            if (startX > window.getX() + 0.5f)
                g.fillRect (juce::Rectangle<float> (window.getX(), window.getY(),
                                                    startX - window.getX(),
                                                    window.getHeight()));
            if (endX < window.getRight() - 0.5f)
                g.fillRect (juce::Rectangle<float> (endX, window.getY(),
                                                    window.getRight() - endX,
                                                    window.getHeight()));
        }

        // Progress-Sweep als Fade-Schweif (User 09.07.2026): nicht der
        // gesamte abgespielte Teil dunkelt ab — ein begrenztes Band hinter
        // der Abspielkante läuft nach hinten transparent aus. Der Loop ist
        // ZYKLISCH: um die Loop-Grenze wickelt der Schweif ans andere
        // Zellende weiter (alpha-stetig), statt zu verschwinden; Reverse
        // spiegelt Richtung und Verlauf (Übergabe §2)
        if (state.playing)
        {
            const auto sweep = bounds.reduced (2.0f);
            const auto width = sweep.getWidth() * juce::jlimit (0.0f, 1.0f, state.progress01);
            const auto tailLength = sweep.getWidth() * 0.35f;
            const auto edgeColour = inked ? juce::Colours::black.withAlpha (0.45f)
                                          : push::colours::tileActive;

            // Ein Schweif-Segment [xStart, xEnd] mit linearem Alpha-Verlauf
            // (Anteile 0..1 der Schweif-Deckung an den Segment-Enden)
            const auto drawTail = [&] (float xStart, float xEnd,
                                       float alphaStart, float alphaEnd)
            {
                if (xEnd - xStart < 0.5f)
                    return;

                const juce::ColourGradient gradient (
                    edgeColour.withMultipliedAlpha (alphaStart), xStart, sweep.getY(),
                    edgeColour.withMultipliedAlpha (alphaEnd),   xEnd,   sweep.getY(),
                    false);
                g.setGradientFill (gradient);
                g.fillRect (juce::Rectangle<float> (xStart, sweep.getY(),
                                                    xEnd - xStart, sweep.getHeight()));
            };

            if (tailLength > 0.5f)
            {
                const auto head = juce::jmin (width, tailLength);
                const auto rest = tailLength - head;   // wickelt über die Loop-Grenze

                if (state.reversed)
                {
                    // Kante links des gefüllten Anteils, Schweif nach rechts;
                    // Rest-Schweif setzt am LINKEN Zellrand fort
                    const auto edgeX = sweep.getRight() - width;
                    drawTail (edgeX, edgeX + head, 1.0f, 1.0f - head / tailLength);
                    drawTail (sweep.getX(), sweep.getX() + rest, rest / tailLength, 0.0f);
                }
                else
                {
                    // Kante rechts des gefüllten Anteils, Schweif nach links;
                    // Rest-Schweif setzt am RECHTEN Zellrand fort
                    const auto edgeX = sweep.getX() + width;
                    drawTail (edgeX - head, edgeX, 1.0f - head / tailLength, 1.0f);
                    drawTail (sweep.getRight() - rest, sweep.getRight(),
                              0.0f, rest / tailLength);
                }
            }
        }

        // Kopfzeile OBEN (User 09.07.2026): Play-Dreieck + Label + Badges —
        // die Waveform-Tinte ballt sich um die Zell-Mitte, oben bleibt sie
        // meist frei. Auf dunklen Stellen nehmen die Aufbauten die
        // Gegenfarbe an (vorberechnete Zonen-Deckung) und bleiben sichtbar.
        auto headline = bounds.reduced (4.0f);
        headline = headline.removeFromTop (juce::jmin (16.0f, headline.getHeight()));

        constexpr float inkContrastThreshold = 0.4f;
        const auto iconColour = inked
            ? (iconZoneInk > inkContrastThreshold ? thumbnailBackground
                                                  : juce::Colours::black)
            : push::colours::ledGreen;
        const auto headlineBase = inked
            ? (headlineZoneInk > inkContrastThreshold ? thumbnailBackground
                                                      : juce::Colours::black)
            : (state.playing ? push::colours::text : push::colours::textDim);
        const auto textColour = inked && ! state.playing
                              ? headlineBase.withAlpha (0.75f) : headlineBase;

        const auto iconSide = juce::jmin (10.0f, headline.getHeight() - 1.0f);
        auto iconBox = headline.removeFromLeft (iconSide + 4.0f)
                           .withSizeKeepingCentre (iconSide, iconSide);

        if (state.playing)
        {
            juce::Path triangle;
            if (state.reversed)
            {
                triangle.addTriangle (iconBox.getRight(), iconBox.getY(),
                                      iconBox.getRight(), iconBox.getBottom(),
                                      iconBox.getX(), iconBox.getCentreY());
            }
            else
            {
                triangle.addTriangle (iconBox.getX(), iconBox.getY(),
                                      iconBox.getX(), iconBox.getBottom(),
                                      iconBox.getRight(), iconBox.getCentreY());
            }
            g.setColour (iconColour);
            g.fillPath (triangle);
        }

        g.setColour (textColour);
        g.setFont (push::scaledFont (juce::jmin (12.0f, headline.getHeight() + 2.0f)));
        auto textArea = headline;
        textArea.removeFromLeft (2.0f);

        // Badges rechts: "/n" (LEN), Rate ("0.71×") und Reverse (◁)
        juce::String badge = state.rateBadge;
        if (state.divBadge.isNotEmpty())
            badge = badge.isEmpty() ? state.divBadge : state.divBadge + " " + badge;
        if (state.reversed)
            badge = badge.isEmpty() ? juce::String::fromUTF8 ("◁")
                                    : badge + juce::String::fromUTF8 (" ◁");
        if (badge.isNotEmpty())
        {
            const auto badgeWidth = juce::jmin (textArea.getWidth() * 0.5f, 44.0f);
            const auto badgeArea = textArea.removeFromRight (badgeWidth);
            g.setColour (inked ? headlineBase : push::colours::ledOrange);
            g.drawText (badge, badgeArea, juce::Justification::centredRight, false);
            g.setColour (textColour);
        }

        // Thumbnail-Zellen tragen den eingefrorenen Quell-Text („Live /
        // wavetable"), klassische Zellen weiter "Clip N · X Bars"
        const auto label = inked && thumbnailSourceLabel.isNotEmpty()
                         ? thumbnailSourceLabel : state.label;
        g.drawText (label, textArea, juce::Justification::centredLeft, true);
    }
    else if (state.target)
    {
        // Rot pulsierender Target-Kreis + Label (Design-Mock)
        const auto pulse = 0.55f + 0.45f * std::sin (pulsePhase
                                                     * juce::MathConstants<float>::twoPi);
        const auto radius = 5.0f;
        const auto circle = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                .withCentre ({ bounds.getX() + 12.0f, bounds.getCentreY() });
        g.setColour (push::colours::ledRed.withAlpha (juce::jlimit (0.2f, 1.0f, pulse)));
        g.fillEllipse (circle);

        g.setColour (push::colours::ledRed);
        g.setFont (push::scaledFont (11.0f));
        auto textArea = bounds.reduced (4.0f);
        textArea.removeFromLeft (20.0f);
        g.drawText ("TARGET", textArea, juce::Justification::centredLeft, false);
    }
    else
    {
        // Leerer Slot: dezenter Kreis
        g.setColour (push::colours::outline);
        const auto circle = juce::Rectangle<float> (10.0f, 10.0f)
                                .withCentre (bounds.getCentre());
        g.drawEllipse (circle, 1.2f);
    }

    // Aktiv-Kontur (Clip-Controls-Ziel) über allem
    if (state.active && state.hasClip)
    {
        g.setColour (push::colours::ledWhite);
        g.drawRoundedRectangle (bounds, 4.0f, 1.5f);
    }
}

//==============================================================================
LooperTrackStrip::LooperTrackStrip (int number)
    : trackNumber (number)
{
    setName ("looperTrackStrip" + juce::String (number));

    muteTile.onClick = [this]
    {
        if (onMuteToggled != nullptr)
            onMuteToggled (! mute);
    };
    soloTile.onClick = [this]
    {
        if (onSoloToggled != nullptr)
            onSoloToggled (! solo);
    };
    stopTile.onClick = [this]
    {
        if (onStop != nullptr)
            onStop();
    };

    // Footer-Play (07/2026): Kurzklick startet den aktiven, sonst den
    // ersten belegten Slot; Long-Press = ReSet (Rate 1×, vorwärts,
    // Re-Sync — aus der Controls-Zeile hierher gewandert)
    playTile.onShortClick = [this]
    {
        if (onPlay != nullptr)
            onPlay();
    };
    playTile.onHoldChanged = [this] (bool holding)
    {
        if (holding && onResetSync != nullptr)
            onResetSync();
    };

    // XY-Panner: X = Pan, Y = Distanz (beide Hooks getrennt nach oben)
    xyPad.onChanged = [this] (float newPan, float newDistance)
    {
        const auto panChanged = ! juce::exactlyEqual (pan, newPan);
        const auto distanceChanged = ! juce::exactlyEqual (distance, newDistance);
        pan = newPan;
        distance = newDistance;
        if (panChanged && onPanChanged != nullptr)
            onPanChanged (pan);
        if (distanceChanged && onDistanceChanged != nullptr)
            onDistanceChanged (distance);
    };

    for (int s = 0; s < 4; ++s)
    {
        auto tile = std::make_unique<LooperSendTile> (s);
        tile->onLevelChanged = [this, s] (float level)
        {
            sendLevels[(std::size_t) s] = level;
            xyPad.setSendLevels (sendLevels, sendCount);
            if (onSendLevelChanged != nullptr)
                onSendLevelChanged (s, level);
        };
        addAndMakeVisible (*tile);
        sendTiles[(std::size_t) s] = std::move (tile);
    }

    addAndMakeVisible (muteTile);
    addAndMakeVisible (soloTile);
    addAndMakeVisible (xyPad);
    addAndMakeVisible (playTile);
    addAndMakeVisible (stopTile);

    setVisibleSlots (8);
}

void LooperTrackStrip::setDisplayNumber (int number)
{
    if (trackNumber == number)
        return;

    trackNumber = number;
    repaint();
}

void LooperTrackStrip::setVisibleSlots (int count)
{
    count = juce::jlimit (1, 12, count);
    if ((int) cells.size() == count)
        return;

    while ((int) cells.size() > count)
        cells.pop_back();

    while ((int) cells.size() < count)
    {
        auto cell = std::make_unique<LooperSlotCell>();
        const auto slotIndex = (int) cells.size();
        cell->onTap = [this, slotIndex]
        {
            if (onSlotTapped != nullptr)
                onSlotTapped (slotIndex);
        };
        addAndMakeVisible (*cell);
        cells.push_back (std::move (cell));
    }

    resized();
}

LooperSlotCell& LooperTrackStrip::getSlotCell (int slotIndex)
{
    jassert (slotIndex >= 0 && slotIndex < (int) cells.size());
    return *cells[(size_t) juce::jlimit (0, (int) cells.size() - 1, slotIndex)];
}

void LooperTrackStrip::setGain (float gain01)
{
    gain = juce::jlimit (0.0f, 1.0f, gain01);
    repaint (meterArea());
}

void LooperTrackStrip::setPan (float newPan)
{
    pan = juce::jlimit (-1.0f, 1.0f, newPan);
    xyPad.setValues (pan, distance);
}

void LooperTrackStrip::setDistance (float distance01)
{
    distance = juce::jlimit (0.0f, 1.0f, distance01);
    xyPad.setValues (pan, distance);
}

void LooperTrackStrip::setMute (bool muted)
{
    mute = muted;
    muteTile.setActive (muted);
}

void LooperTrackStrip::setSolo (bool newSolo)
{
    solo = newSolo;
    soloTile.setActive (newSolo);
}

void LooperTrackStrip::setSendLevels (const std::array<float, 4>& levels)
{
    sendLevels = levels;
    pushSendStateToChildren();
}

void LooperTrackStrip::setSendCount (int count)
{
    const auto clamped = juce::jlimit (0, 4, count);
    if (sendCount == clamped)
        return;
    sendCount = clamped;
    pushSendStateToChildren();
    resized();
}

void LooperTrackStrip::setYLinkSend (int sendIndex)
{
    if (yLinkSend == sendIndex)
        return;
    yLinkSend = sendIndex;
    pushSendStateToChildren();
}

void LooperTrackStrip::setSendLabelsVisible (bool visible)
{
    if (sendLabels == visible)
        return;
    sendLabels = visible;
    pushSendStateToChildren();
}

void LooperTrackStrip::setShowMuteSolo (bool show)
{
    if (showMuteSolo == show)
        return;
    showMuteSolo = show;
    muteTile.setVisible (show);
    soloTile.setVisible (show);
    resized();
}

void LooperTrackStrip::setShowXy (bool show)
{
    if (showXy == show)
        return;
    showXy = show;
    xyPad.setVisible (show);
    resized();
}

void LooperTrackStrip::setXyCompact (bool compact)
{
    if (xyCompact == compact)
        return;
    xyCompact = compact;
    xyPad.setCompact (compact);
    resized();
}

void LooperTrackStrip::pushSendStateToChildren()
{
    for (int s = 0; s < 4; ++s)
    {
        auto& tile = *sendTiles[(std::size_t) s];
        tile.setLevel (sendLevels[(std::size_t) s]);
        tile.setShowLabel (sendLabels);
        tile.setYLinked (s == yLinkSend);
        tile.setVisible (s < sendCount);
    }
    xyPad.setSendLevels (sendLevels, sendCount);
}

LooperSendTile& LooperTrackStrip::getSendTile (int sendIndex)
{
    return *sendTiles[(std::size_t) juce::jlimit (0, 3, sendIndex)];
}

void LooperTrackStrip::setMeter (float rmsLeft, float rmsRight,
                                 float newPeakLeft, float newPeakRight, bool audible)
{
    const auto changed = std::abs (meterLeft - rmsLeft) > 0.004f
                      || std::abs (meterRight - rmsRight) > 0.004f
                      || std::abs (peakLeft - newPeakLeft) > 0.004f
                      || std::abs (peakRight - newPeakRight) > 0.004f
                      || ledOn != audible;
    meterLeft = rmsLeft;
    meterRight = rmsRight;
    peakLeft = newPeakLeft;
    peakRight = newPeakRight;
    ledOn = audible;
    if (changed)
    {
        repaint (headerArea());
        repaint (meterArea());
    }
}

void LooperTrackStrip::setBarDisplay (int currentBar, int totalBars, float progress01)
{
    // Exakter Vergleich statt Epsilon: der VBlank-Pfad des Editors treibt
    // den Pie monitor-synchron — jede echte Phasen-Änderung wird sichtbar
    const auto changed = barCurrent != currentBar || barTotal != totalBars
                      || ! juce::exactlyEqual (barProgress, progress01);
    barCurrent = currentBar;
    barTotal = totalBars;
    barProgress = progress01;
    if (changed)
        repaint (barArea());
}

void LooperTrackStrip::setPulsePhase (float phase01)
{
    for (auto& cell : cells)
        cell->setPulsePhase (phase01);
}

//==============================================================================
juce::Rectangle<int> LooperTrackStrip::headerArea() const
{
    return getLocalBounds().removeFromTop (headerHeight);
}

juce::Rectangle<int> LooperTrackStrip::meterArea() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (headerHeight);
    return bounds.removeFromTop (meterHeight);
}

int LooperTrackStrip::xyHeight() const noexcept
{
    return showXy ? (xyCompact ? xyCompactHeight : xyFullHeight) : 0;
}

int LooperTrackStrip::sendRowHeight() const noexcept
{
    if (sendCount <= 0)
        return 0;

    // Breite Strips: eine Zeile; schmale (3–4 Tracks): 2×2-Raster
    return getWidth() >= 132 || sendCount <= 2 ? tileRow : tileRow * 2;
}

juce::Rectangle<int> LooperTrackStrip::barArea() const
{
    auto bounds = getLocalBounds();
    auto footer = bounds.removeFromBottom (footerHeight);
    footer.removeFromLeft (footer.getWidth() * 2 / 3);   // ▶ + ■
    return footer;
}

void LooperTrackStrip::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (headerHeight + meterHeight);

    // XY-Panner (ausblendbar; kompakt bei 3–4 Tracks)
    if (showXy)
        xyPad.setBounds (bounds.removeFromTop (xyHeight()).reduced (2, 2));

    // Send-Kacheln: eine Zeile (breit / ≤2 Sends) oder 2×2-Raster
    if (sendCount > 0)
    {
        auto sendArea = bounds.removeFromTop (sendRowHeight());
        if (sendRowHeight() > tileRow)
        {
            auto upper = sendArea.removeFromTop (tileRow);
            const auto upperCount = juce::jmin (2, sendCount);
            for (int s = 0; s < upperCount; ++s)
                sendTiles[(std::size_t) s]->setBounds (
                    upper.removeFromLeft (upper.getWidth() / (upperCount - s)));
            const auto lowerCount = sendCount - upperCount;
            for (int s = 0; s < lowerCount; ++s)
                sendTiles[(std::size_t) (upperCount + s)]->setBounds (
                    sendArea.removeFromLeft (sendArea.getWidth() / (lowerCount - s)));
        }
        else
        {
            for (int s = 0; s < sendCount; ++s)
                sendTiles[(std::size_t) s]->setBounds (
                    sendArea.removeFromLeft (sendArea.getWidth() / (sendCount - s)));
        }
    }

    // M/S (ausblendbar)
    if (showMuteSolo)
    {
        auto tiles = bounds.removeFromTop (tileRow);
        muteTile.setBounds (tiles.removeFromLeft (tiles.getWidth() / 2).reduced (2));
        soloTile.setBounds (tiles.reduced (2));
    }

    auto footer = bounds.removeFromBottom (footerHeight);
    playTile.setBounds (footer.removeFromLeft (footer.getWidth() / 3).reduced (2));
    stopTile.setBounds (footer.removeFromLeft (footer.getWidth() / 2).reduced (2));
    // rechtes Fußdrittel = Takt-Anzeige (paint)

    // Slot-Zellen füllen den Rest gleichmäßig
    const auto count = (int) cells.size();
    if (count > 0)
    {
        const auto cellHeight = juce::jmax (18, (bounds.getHeight() - cellGap * (count - 1))
                                                    / count);
        for (int i = 0; i < count; ++i)
        {
            cells[(size_t) i]->setBounds (bounds.removeFromTop (cellHeight));
            bounds.removeFromTop (cellGap);
        }
    }
}

void LooperTrackStrip::paint (juce::Graphics& g)
{
    // Header: "TRACK n" + LED
    auto header = headerArea();
    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (11.0f));
    g.drawText ("TRACK " + juce::String (trackNumber),
                header.reduced (4, 0), juce::Justification::centredLeft, false);

    const auto led = juce::Rectangle<float> (8.0f, 8.0f)
                         .withCentre ({ (float) header.getRight() - 10.0f,
                                        (float) header.getCentreY() });
    g.setColour (ledOn ? push::colours::ledGreen : push::colours::outline);
    g.fillEllipse (led);

    // Meter-Zeile (ersetzt den Fader): STEREO von der Mitte aus — L läuft
    // nach links, R nach rechts; RMS als Fläche, Peak als Linie pro Seite.
    // Post-fader-Daten sind bereits pan-gewichtet (Engine). Vertikal
    // wischen = Gain (Geste des früheren Faders); die Gain-Marker sitzen
    // symmetrisch am jeweiligen Zeilen-Ende des Regelwegs.
    auto meter = meterArea().toFloat().reduced (2.0f);
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (meter, 3.0f);

    const auto centreX = meter.getCentreX();
    const auto halfWidth = meter.getWidth() * 0.5f - 3.0f;
    const auto lane = meter.reduced (2.0f);

    const auto drawSide = [&] (float rms, float peak, bool leftSide)
    {
        const auto direction = leftSide ? -1.0f : 1.0f;
        if (rms > 0.001f)
        {
            const auto extent = halfWidth * juce::jlimit (0.0f, 1.0f, rms);
            const auto bar = leftSide
                ? juce::Rectangle<float> (centreX - extent, lane.getY(), extent, lane.getHeight())
                : juce::Rectangle<float> (centreX, lane.getY(), extent, lane.getHeight());
            g.setColour (push::colours::ledGreen.withAlpha (0.45f));
            g.fillRect (bar);
        }
        if (peak > 0.001f)
        {
            const auto peakX = centreX + direction * halfWidth
                                   * juce::jlimit (0.0f, 1.0f, peak);
            g.setColour (peak >= 0.999f ? push::colours::ledRed
                                        : push::colours::ledGreen);
            g.fillRect (juce::Rectangle<float> (peakX - 0.75f, lane.getY(),
                                                1.5f, lane.getHeight()));
        }
    };
    drawSide (meterLeft, peakLeft, true);
    drawSide (meterRight, peakRight, false);

    // Mittellinie + Gain-Marker (⌶ beidseitig, Abstand = Gain)
    g.setColour (push::colours::outline);
    g.fillRect (juce::Rectangle<float> (centreX - 0.5f, lane.getY(),
                                        1.0f, lane.getHeight()));
    const auto gainExtent = halfWidth * gain;
    g.setColour (push::colours::ledWhite);
    for (const auto side : { -1.0f, 1.0f })
        g.fillRect (juce::Rectangle<float> (centreX + side * gainExtent - 1.0f,
                                            lane.getY(), 2.0f, lane.getHeight()));

    // Takt-Anzeige (rechte Fußhälfte): Pie + "Takt / Länge"
    auto barBox = barArea().toFloat();
    if (barTotal > 0)
    {
        const auto pieSide = juce::jmin (16.0f, barBox.getHeight() - 4.0f);
        auto pie = juce::Rectangle<float> (pieSide, pieSide)
                       .withCentre ({ barBox.getX() + pieSide * 0.5f + 4.0f,
                                      barBox.getCentreY() });
        g.setColour (push::colours::outline);
        g.drawEllipse (pie, 1.2f);

        juce::Path slice;
        slice.addPieSegment (pie, 0.0f,
                             juce::MathConstants<float>::twoPi * barProgress, 0.0f);
        g.setColour (push::colours::ledGreen);
        g.fillPath (slice);

        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (11.0f));
        barBox.removeFromLeft (pieSide + 8.0f);
        g.drawText (juce::String (barCurrent) + " / " + juce::String (barTotal),
                    barBox, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (10.0f));
        g.drawText ("gestoppt", barBox, juce::Justification::centred, false);
    }
}

//==============================================================================
void LooperTrackStrip::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (meterArea().contains (position))
    {
        dragMode = DragMode::gain;
        dragStartValue = gain;
        dragStartPosition = position;
    }
    else if (headerArea().contains (position))
    {
        headerPressed = true;
        headerPressTime = juce::Time::getMillisecondCounter();
    }
}

void LooperTrackStrip::mouseDrag (const juce::MouseEvent& event)
{
    if (dragMode == DragMode::gain)
    {
        // VERTIKAL wischen (User-Entscheidung, Geste des alten Faders):
        // hoch = lauter — die Meter-Zeile bleibt der Gain-Regler
        const auto delta = (float) (dragStartPosition.y - event.getPosition().y);
        const auto next = juce::jlimit (0.0f, 1.0f,
                                        dragStartValue + delta / faderDragRange);
        if (! juce::exactlyEqual (next, gain))
        {
            gain = next;
            repaint (meterArea());
            if (onGainChanged != nullptr)
                onGainChanged (gain);
        }
    }
}

void LooperTrackStrip::mouseUp (const juce::MouseEvent& event)
{
    dragMode = DragMode::none;

    if (headerPressed)
    {
        headerPressed = false;
        const auto heldMs = juce::Time::getMillisecondCounter() - headerPressTime;
        if (headerArea().contains (event.getPosition()))
        {
            if (heldMs >= longPressMs && onHeaderLongPress != nullptr)
                onHeaderLongPress();
            else if (heldMs < longPressMs && onHeaderTapped != nullptr)
                onHeaderTapped();   // M7: Delete-Geste wertet den Tap aus
        }
    }
}

} // namespace conduit
