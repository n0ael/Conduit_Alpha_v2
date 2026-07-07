#include "MpeShapingView.h"

#include "PushLookAndFeel.h"

namespace conduit
{

MpeShapingView::MpeShapingView (grid::GridVoiceEngine& engineToUse, GridPanelSettings& panelSettingsToUse,
                                UiSettings& uiSettingsToUse)
    : engine (engineToUse), panelSettings (panelSettingsToUse), uiSettings (uiSettingsToUse),
      sections {{
          AxisSection { grid::GridVoiceEngine::Axis::Pressure,  "Pressure",  push::colours::ledOrange },
          AxisSection { grid::GridVoiceEngine::Axis::Slide,     "Slide",     push::colours::ledCyan },
          AxisSection { grid::GridVoiceEngine::Axis::PitchBend, "PitchBend", push::colours::ledGreen },
      }}
{
    for (auto& section : sections)
        section.scratch.reserve ((size_t) grid::VoiceAllocator::kMaxVoices);

    addChildComponent (thresholdCaption);
    addChildComponent (thresholdSlider);
    addChildComponent (fadeCaption);
    addChildComponent (fadeSlider);

    thresholdCaption.setJustificationType (juce::Justification::centredLeft);
    thresholdCaption.setColour (juce::Label::textColourId, push::colours::textDim);
    fadeCaption.setJustificationType (juce::Justification::centredLeft);
    fadeCaption.setColour (juce::Label::textColourId, push::colours::textDim);

    thresholdSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, kDevRowHeight - 4);
    thresholdSlider.setRange ((double) GridPanelSettings::minThresholdWidth,
                             (double) GridPanelSettings::maxThresholdWidth, 1.0);
    thresholdSlider.setValue (panelSettings.getEditorThresholdWidth(), juce::dontSendNotification);
    // Live: die Schwellbreite wirkt sofort aufs Layout; persistiert wird erst
    // beim Loslassen (Muster EditorDockPanel::onWidthCommitted).
    thresholdSlider.onValueChange = [this] { resized(); };
    thresholdSlider.onDragEnd = [this]
    { panelSettings.setEditorThresholdWidth ((int) thresholdSlider.getValue()); };

    fadeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    fadeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, kDevRowHeight - 4);
    fadeSlider.setRange ((double) GridPanelSettings::minNoteCircleFadeMs,
                        (double) GridPanelSettings::maxNoteCircleFadeMs, 1.0);
    fadeSlider.setValue (panelSettings.getNoteCircleFadeMs(), juce::dontSendNotification);
    // Live: wirkt ab dem nächsten Frame auf alle drei Fade-Tracker;
    // persistiert wird erst beim Loslassen.
    fadeSlider.onValueChange = [this]
    {
        for (auto& section : sections)
            section.fadeTracker.setFadeMs ((int) fadeSlider.getValue());
    };
    fadeSlider.onDragEnd = [this]
    { panelSettings.setNoteCircleFadeMs ((int) fadeSlider.getValue()); };

    for (auto& section : sections)
        section.fadeTracker.setFadeMs (panelSettings.getNoteCircleFadeMs());

    updateDevSliderVisibility (uiSettings.isDevModeEnabled());
}

void MpeShapingView::updateDevSliderVisibility (bool devModeEnabled)
{
    devSlidersVisible = devModeEnabled;

    thresholdCaption.setVisible (devSlidersVisible);
    thresholdSlider.setVisible (devSlidersVisible);
    fadeCaption.setVisible (devSlidersVisible);
    fadeSlider.setVisible (devSlidersVisible);
}

void MpeShapingView::tick()
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto deltaMs = lastTickMs > 0.0 ? (nowMs - lastTickMs) : 0.0;
    lastTickMs = nowMs;

    for (auto& section : sections)
    {
        engine.readActiveVoices (section.axis, section.scratch);
        section.fadeTracker.update (section.scratch, deltaMs);
    }

    const auto devModeEnabled = uiSettings.isDevModeEnabled();
    if (devModeEnabled != devSlidersVisible)
    {
        updateDevSliderVisibility (devModeEnabled);
        resized();
    }

    repaint();
}

void MpeShapingView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    // Pro Sektion: welches Ziel wird gerade (von irgendeinem Touch) bearbeitet?
    // (Multi-Touch: mehrere Gesten können gleichzeitig verschiedene Sektionen treffen.)
    std::array<grid::CurveEditInteraction::Target, 3> activeTargets;
    activeTargets.fill (grid::CurveEditInteraction::Target::None);

    for (const auto& entry : gestures)
    {
        const auto& gesture = entry.second;
        if (gesture.sectionIndex >= 0 && gesture.sectionIndex < (int) activeTargets.size())
            activeTargets[(size_t) gesture.sectionIndex] = gesture.target;
    }

    for (size_t i = 0; i < sections.size(); ++i)
        paintAxis (g, sections[i], activeTargets[i]);
}

void MpeShapingView::paintAxis (juce::Graphics& g, const AxisSection& section,
                                grid::CurveEditInteraction::Target activeTarget) const
{
    if (section.tileBounds.isEmpty())
        return;

    // Abgesetzte Kachel (Looper-Stil): dunkle Fläche + dezente Kontur, runde Ecken
    const auto tile = section.tileBounds.toFloat();
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (tile, kTileCornerRadius);
    g.setColour (push::colours::outline);
    g.drawRoundedRectangle (tile, kTileCornerRadius, 1.0f);

    if (section.curveBounds.isEmpty())
        return;

    const auto isPitchBend = section.axis == grid::GridVoiceEngine::Axis::PitchBend;

    auto area = section.curveBounds;
    const auto headerArea = area.removeFromTop (kHeaderRowHeight);

    const auto& curve  = engine.responseCurve (section.axis);
    const auto outMin  = curve.getOutputMin();
    const auto outMax  = curve.getOutputMax();

    g.setColour (section.colour);
    g.setFont (push::scaledFont (12.0f, true));
    g.drawText (section.label, headerArea.reduced (4, 0), juce::Justification::centredLeft);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (11.0f));

    if (isPitchBend)
    {
        const auto range = grid::GridVoiceEngine::getPitchBendRangeSemitones();
        g.drawText ("Max +" + juce::String (range, 0) + "  Min -" + juce::String (range, 0),
                   headerArea.reduced (4, 0), juce::Justification::centredRight);
    }
    else
    {
        g.drawText ("Max " + juce::String (outMax, 1) + "  Min " + juce::String (outMin, 1),
                   headerArea.reduced (4, 0), juce::Justification::centredRight);
    }

    const auto bounds = area.reduced (6).toFloat();
    if (bounds.isEmpty())
        return;

    // Diagonale Referenz (Identität)
    g.setColour (push::colours::outline);
    g.drawLine (bounds.getX(), bounds.getBottom(), bounds.getRight(), bounds.getY());

    // PitchBend: dünne Mittellinie bei y=0.5 (0 Halbtöne) -- mit der
    // Default-Kurve (Identität) sitzt eine ungebogene Note automatisch hier,
    // da rawValue/combinedValue bereits normalisiert sind (0.5 = Mitte).
    if (isPitchBend)
    {
        const auto midY = bounds.getBottom() - 0.5f * bounds.getHeight();
        g.setColour (push::colours::textDim);
        g.drawLine (bounds.getX(), midY, bounds.getRight(), midY, 1.0f);
    }

    // Kurve: kCurveSamples+1 Stützstellen über x in [0,1] -- apply(x) ist
    // bereits der ABSOLUTE Ausgangswert (auf [outMin,outMax] skaliert) und
    // wird direkt als y-Position verwendet. NICHT erneut auf die aktuelle
    // Spanne renormalisieren -- sonst füllt die Kurve nach jedem Repaint
    // wieder die ganze Box aus und ein Min/Max-Drag bleibt unsichtbar
    // (Bugfix: Endpunkte ließen sich anfassen, aber nicht sichtbar ziehen).
    juce::Path path;
    for (int i = 0; i <= kCurveSamples; ++i)
    {
        const auto x = (float) i / (float) kCurveSamples;
        const auto px = bounds.getX() + x * bounds.getWidth();
        const auto py = bounds.getBottom() - juce::jlimit (0.0f, 1.0f, curve.apply (x)) * bounds.getHeight();

        if (i == 0)
            path.startNewSubPath (px, py);
        else
            path.lineTo (px, py);
    }

    // Krümmungs-Wisch aktiv: Kurvenlinie kurz betonen (dicker, heller)
    const auto curvatureActive = activeTarget == grid::CurveEditInteraction::Target::Curvature;
    g.setColour (curvatureActive ? section.colour.brighter (0.4f) : section.colour);
    g.strokePath (path, juce::PathStrokeType (curvatureActive ? 3.0f : 1.5f));

    // Endpunkt-Griffe (S2c-2a): Höhe = tatsächlicher outMin-/outMax-Wert
    // (NICHT mehr fix an den Feld-Ecken) -- sonst bleibt ein Endpunkt-Drag
    // sichtbar wirkungslos, obwohl setOutputRange längst greift. Aktiv
    // gezogener Endpunkt wird größer/heller hervorgehoben.
    const auto minActive = activeTarget == grid::CurveEditInteraction::Target::MinEndpoint;
    const auto maxActive = activeTarget == grid::CurveEditInteraction::Target::MaxEndpoint;

    const auto minPy = bounds.getBottom() - juce::jlimit (0.0f, 1.0f, outMin) * bounds.getHeight();
    const auto maxPy = bounds.getBottom() - juce::jlimit (0.0f, 1.0f, outMax) * bounds.getHeight();
    const auto minPos = juce::Point<float> (bounds.getX(), minPy);
    const auto maxPos = juce::Point<float> (bounds.getRight(), maxPy);

    const auto minRadius = minActive ? kEndpointRadiusActive : kEndpointRadius;
    g.setColour (minActive ? push::colours::ledWhite : section.colour);
    g.fillEllipse (juce::Rectangle<float> (minRadius * 2.0f, minRadius * 2.0f).withCentre (minPos));

    const auto maxRadius = maxActive ? kEndpointRadiusActive : kEndpointRadius;
    g.setColour (maxActive ? push::colours::ledWhite : section.colour);
    g.fillEllipse (juce::Rectangle<float> (maxRadius * 2.0f, maxRadius * 2.0f).withCentre (maxPos));

    // Live-Noten-Kreise: x = jlimit(0,1,rawValue) -- für Achsen mit
    // Rohwertbereich außerhalb [0,1] (PitchBend in Halbtönen) klemmt die
    // Position an den Rand; eine domänenkorrekte Skalierung folgt mit den
    // Achs-Optionen (S2c-2). y = apply(rawValue) direkt (absoluter
    // Ausgangswert, nicht auf die aktuelle Spanne renormalisiert).
    for (const auto& circle : section.fadeTracker.circles())
    {
        const auto x = juce::jlimit (0.0f, 1.0f, circle.rawValue);
        const auto px = bounds.getX() + x * bounds.getWidth();
        const auto py = bounds.getBottom()
                           - juce::jlimit (0.0f, 1.0f, curve.apply (circle.rawValue)) * bounds.getHeight();

        g.setColour (section.colour.withAlpha (circle.opacity));
        g.fillEllipse (juce::Rectangle<float> (kNoteCircleDiameter, kNoteCircleDiameter)
                           .withCentre ({ px, py }));

        // Zweite Höhenmarke am rechten Feldrand: finaler Ausgang inkl. Kurve+
        // Offset (combinedValue, schon absolut) -- wandert live am Offset-
        // Slider, schwächer/kleiner als der Kreis.
        const auto markerY = bounds.getBottom()
                                 - juce::jlimit (0.0f, 1.0f, circle.combinedValue) * bounds.getHeight();
        const auto markerRect = juce::Rectangle<float> (kMarkerWidth, kMarkerHeight)
                                    .withCentre ({ bounds.getRight() - kMarkerWidth * 0.5f - 1.0f, markerY });

        g.setColour (section.colour.withAlpha (circle.opacity * 0.7f));
        g.fillRoundedRectangle (markerRect, 1.0f);
    }

    // Detailspalte: reiner Platzhalter-Text, noch nicht interaktiv (S2c-2)
    if (! section.detailBounds.isEmpty())
    {
        g.setColour (push::colours::outline);
        g.drawVerticalLine (section.detailBounds.getX(), (float) section.detailBounds.getY(),
                           (float) section.detailBounds.getBottom());

        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (11.0f));
        g.drawFittedText ("Smooth\nRise/Fall\n\nMode\nAbs/Rel/Ons\n\nDefault/\nCentered",
                          section.detailBounds.reduced (8), juce::Justification::topLeft, 8);
    }
}

void MpeShapingView::resized()
{
    auto bounds = getLocalBounds();

    if (devSlidersVisible)
    {
        auto devRow = bounds.removeFromBottom (kDevRowHeight * 2 + 4);

        auto thresholdRow = devRow.removeFromTop (kDevRowHeight);
        thresholdCaption.setBounds (thresholdRow.removeFromLeft (100));
        thresholdSlider.setBounds (thresholdRow.reduced (4, 2));

        devRow.removeFromTop (4);
        fadeCaption.setBounds (devRow.removeFromLeft (100));
        fadeSlider.setBounds (devRow.reduced (4, 2));
    }

    const auto sectionHeight = bounds.getHeight() / (int) sections.size();
    const auto showDetail = getWidth() >= panelSettings.getEditorThresholdWidth();

    for (auto& section : sections)
    {
        // Abgesetzte Kachel: kleiner Gap oben/unten trennt die drei Achsen-
        // Felder sichtbar voneinander (Looper-Kachel-Stil).
        auto sectionBounds = bounds.removeFromTop (sectionHeight).reduced (0, kSectionGap / 2);
        section.tileBounds = sectionBounds;

        section.detailBounds = showDetail ? sectionBounds.removeFromRight (kDetailColumnWidth)
                                          : juce::Rectangle<int>{};
        section.curveBounds  = sectionBounds;
    }
}

int MpeShapingView::sectionIndexAt (juce::Point<float> pos) const noexcept
{
    for (int i = 0; i < (int) sections.size(); ++i)
        if (sections[(size_t) i].curveBounds.contains (pos.toInt()))
            return i;

    return -1;
}

juce::Rectangle<float> MpeShapingView::curveFieldBounds (const AxisSection& section) const noexcept
{
    auto area = section.curveBounds;
    area.removeFromTop (kHeaderRowHeight);
    return area.reduced (6).toFloat();
}

juce::Point<float> MpeShapingView::normalisedPositionIn (juce::Rectangle<float> fieldBounds,
                                                         juce::Point<float> pos) noexcept
{
    if (fieldBounds.getWidth() <= 0.0f || fieldBounds.getHeight() <= 0.0f)
        return {};

    const auto normX = (pos.x - fieldBounds.getX()) / fieldBounds.getWidth();
    const auto normY = (fieldBounds.getBottom() - pos.y) / fieldBounds.getHeight();   // 0=unten, 1=oben
    return { normX, normY };
}

void MpeShapingView::mouseDown (const juce::MouseEvent& event)
{
    const auto sectionIndex = sectionIndexAt (event.position);
    if (sectionIndex < 0)
        return;

    auto& section = sections[(size_t) sectionIndex];
    const auto fieldBounds = curveFieldBounds (section);
    if (fieldBounds.isEmpty())
        return;

    const auto normPos = normalisedPositionIn (fieldBounds, event.position);
    const auto& curve = engine.responseCurve (section.axis);

    // Trefferradius: 44 px (Touch-Target-Regel) normiert auf die Feldhöhe.
    const auto hitRadiusNorm = kTouchTargetPx / juce::jmax (1.0f, fieldBounds.getHeight());

    const auto target = grid::CurveEditInteraction::hitTest (normPos, curve.getOutputMin(),
                                                             curve.getOutputMax(), hitRadiusNorm);

    EditGesture gesture;
    gesture.sectionIndex    = sectionIndex;
    gesture.target          = target;
    gesture.startNormY      = normPos.y;
    gesture.curvatureAtDown = section.segmentCurvature;

    gestures[event.source.getIndex()] = gesture;

    repaint();
}

void MpeShapingView::mouseDrag (const juce::MouseEvent& event)
{
    const auto it = gestures.find (event.source.getIndex());
    if (it == gestures.end())
        return;

    const auto& gesture = it->second;
    if (gesture.target == grid::CurveEditInteraction::Target::None || gesture.sectionIndex < 0)
        return;

    auto& section = sections[(size_t) gesture.sectionIndex];
    const auto fieldBounds = curveFieldBounds (section);
    if (fieldBounds.isEmpty())
        return;

    const auto normPos = normalisedPositionIn (fieldBounds, event.position);
    auto& curve = engine.responseCurve (section.axis);

    switch (gesture.target)
    {
        case grid::CurveEditInteraction::Target::MinEndpoint:
            curve.setOutputRange (grid::CurveEditInteraction::endpointValueFromY (normPos.y),
                                  curve.getOutputMax());
            break;

        case grid::CurveEditInteraction::Target::MaxEndpoint:
            curve.setOutputRange (curve.getOutputMin(),
                                  grid::CurveEditInteraction::endpointValueFromY (normPos.y));
            break;

        case grid::CurveEditInteraction::Target::Curvature:
        {
            const auto newCurvature = juce::jlimit (-1.0f, 1.0f,
                gesture.curvatureAtDown + grid::CurveEditInteraction::curvatureDelta (
                    gesture.startNormY, normPos.y, kCurvatureSensitivity));
            curve.setSegmentCurvature (0, newCurvature);
            section.segmentCurvature = newCurvature;
            break;
        }

        case grid::CurveEditInteraction::Target::None:
        default:
            break;
    }

    repaint();
}

void MpeShapingView::mouseUp (const juce::MouseEvent& event)
{
    gestures.erase (event.source.getIndex());
    repaint();
}

} // namespace conduit
