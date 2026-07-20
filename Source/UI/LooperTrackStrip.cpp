#include "LooperTrackStrip.h"

namespace conduit
{

namespace
{
    constexpr int headerHeight = 24;
    constexpr int faderHeight  = 26;
    constexpr int panHeight    = 16;
    constexpr int tileRow      = 44;   // Touch-Target (CLAUDE.md 10)
    constexpr int footerHeight = 44;
    constexpr int cellGap      = 4;

    constexpr float faderDragRange = 150.0f;   // Pixel für vollen Gain-Weg
    constexpr juce::uint32 longPressMs = 600;
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

        // Badges rechts: Rate ("0.71×") und Reverse (◁)
        juce::String badge = state.rateBadge;
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
    sndTile.onClick = [this]
    {
        if (onSendTileTapped != nullptr)
            onSendTileTapped();
    };
    stopTile.onClick = [this]
    {
        if (onStop != nullptr)
            onStop();
    };

    addAndMakeVisible (muteTile);
    addAndMakeVisible (soloTile);
    addAndMakeVisible (sndTile);
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
    repaint (faderArea());
}

void LooperTrackStrip::setPan (float newPan)
{
    pan = juce::jlimit (-1.0f, 1.0f, newPan);
    repaint (panArea());
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

void LooperTrackStrip::setSendState (int sendMask, bool sendPre)
{
    sndTile.setActive (sendMask != 0);
    sndTile.setText (sendMask != 0 && sendPre ? "SND·P" : "SND");
}

void LooperTrackStrip::setMeter (float rmsLeft, float rmsRight, bool audible)
{
    const auto changed = std::abs (meterLeft - rmsLeft) > 0.004f
                      || std::abs (meterRight - rmsRight) > 0.004f
                      || ledOn != audible;
    meterLeft = rmsLeft;
    meterRight = rmsRight;
    ledOn = audible;
    if (changed)
    {
        repaint (headerArea());
        repaint (faderArea());
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

juce::Rectangle<int> LooperTrackStrip::faderArea() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (headerHeight);
    return bounds.removeFromTop (faderHeight);
}

juce::Rectangle<int> LooperTrackStrip::panArea() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (headerHeight + faderHeight);
    return bounds.removeFromTop (panHeight);
}

juce::Rectangle<int> LooperTrackStrip::barArea() const
{
    auto bounds = getLocalBounds();
    auto footer = bounds.removeFromBottom (footerHeight);
    footer.removeFromLeft (footer.getWidth() / 2);
    return footer;
}

void LooperTrackStrip::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (headerHeight + faderHeight + panHeight);

    // M/S/SND nebeneinander (Touch-Target 44px pro Kachel); darunter
    // M/S geteilt + SND in eigener Zeile, sehr schmal alles untereinander
    if (getWidth() >= 132)
    {
        auto tiles = bounds.removeFromTop (tileRow);
        muteTile.setBounds (tiles.removeFromLeft (tiles.getWidth() / 3).reduced (2));
        soloTile.setBounds (tiles.removeFromLeft (tiles.getWidth() / 2).reduced (2));
        sndTile.setBounds (tiles.reduced (2));
    }
    else if (getWidth() >= 88)
    {
        auto tiles = bounds.removeFromTop (tileRow * 2);
        auto upper = tiles.removeFromTop (tileRow);
        muteTile.setBounds (upper.removeFromLeft (upper.getWidth() / 2).reduced (2));
        soloTile.setBounds (upper.reduced (2));
        sndTile.setBounds (tiles.reduced (2));
    }
    else
    {
        auto tiles = bounds.removeFromTop (tileRow * 3);
        muteTile.setBounds (tiles.removeFromTop (tileRow).reduced (2));
        soloTile.setBounds (tiles.removeFromTop (tileRow).reduced (2));
        sndTile.setBounds (tiles.reduced (2));
    }

    auto footer = bounds.removeFromBottom (footerHeight);
    stopTile.setBounds (footer.removeFromLeft (footer.getWidth() / 2).reduced (2));
    // rechte Fußhälfte = Takt-Anzeige (paint)

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

    // Fader-Zeile: Post-Fader-Meter als Hintergrund, Marker = Gain
    auto fader = faderArea().toFloat().reduced (2.0f);
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (fader, 3.0f);

    const auto meterMax = juce::jlimit (0.0f, 1.0f, juce::jmax (meterLeft, meterRight));
    if (meterMax > 0.001f)
    {
        auto meterBar = fader.reduced (2.0f);
        meterBar = meterBar.removeFromLeft (meterBar.getWidth() * meterMax);
        g.setColour (push::colours::ledGreen.withAlpha (0.45f));
        g.fillRoundedRectangle (meterBar, 2.0f);
    }

    const auto markerX = fader.getX() + fader.getWidth() * gain;
    g.setColour (push::colours::ledWhite);
    g.fillRect (juce::Rectangle<float> (markerX - 1.0f, fader.getY(),
                                        2.0f, fader.getHeight()));

    juce::Path marker;
    marker.addTriangle (markerX - 4.0f, fader.getY(),
                        markerX + 4.0f, fader.getY(),
                        markerX, fader.getY() + 5.0f);
    g.fillPath (marker);

    // Pan-Zeile: Mittel-Kerbe + Marker
    auto panRow = panArea().toFloat().reduced (2.0f, 1.0f);
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (panRow, 3.0f);
    g.setColour (push::colours::outline);
    g.fillRect (juce::Rectangle<float> (1.0f, panRow.getHeight() - 4.0f)
                    .withPosition (panRow.getCentreX() - 0.5f, panRow.getY() + 2.0f));

    const auto panX = panRow.getCentreX() + pan * (panRow.getWidth() * 0.5f - 4.0f);
    g.setColour (std::abs (pan) > 0.01f ? push::colours::ledCyan
                                        : push::colours::textDim);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f)
                       .withCentre ({ panX, panRow.getCentreY() }));

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

    if (faderArea().contains (position))
    {
        dragMode = DragMode::fader;
        dragStartValue = gain;
        dragStartPosition = position;
    }
    else if (panArea().contains (position))
    {
        dragMode = DragMode::pan;
        dragStartValue = pan;
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
    if (dragMode == DragMode::fader)
    {
        // VERTIKAL wischen (User-Entscheidung): hoch = lauter
        const auto delta = (float) (dragStartPosition.y - event.getPosition().y);
        const auto next = juce::jlimit (0.0f, 1.0f,
                                        dragStartValue + delta / faderDragRange);
        if (! juce::exactlyEqual (next, gain))
        {
            gain = next;
            repaint (faderArea());
            if (onGainChanged != nullptr)
                onGainChanged (gain);
        }
    }
    else if (dragMode == DragMode::pan)
    {
        const auto width = juce::jmax (8.0f, (float) panArea().getWidth() - 8.0f);
        const auto delta = (float) (event.getPosition().x - dragStartPosition.x);
        const auto next = juce::jlimit (-1.0f, 1.0f,
                                        dragStartValue + delta / (width * 0.5f));
        if (! juce::exactlyEqual (next, pan))
        {
            pan = next;
            repaint (panArea());
            if (onPanChanged != nullptr)
                onPanChanged (pan);
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

void LooperTrackStrip::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (panArea().contains (event.getPosition()))
    {
        pan = 0.0f;
        repaint (panArea());
        if (onPanChanged != nullptr)
            onPanChanged (pan);
    }
}

} // namespace conduit
