#include "MpeShapingView.h"

#include "ConduitColorPicker.h"
#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    // Quick-Swatches der „Color"-Zeile (Design-Mock Grid-Page v2) — die
    // fünf LED-Akzent-Tokens, keine Roh-Hex.
    [[nodiscard]] const std::array<juce::Colour, 5>& quickSwatchColours()
    {
        static const std::array<juce::Colour, 5> swatches {
            push::colours::ledOrange, push::colours::ledCyan, push::colours::ledGreen,
            push::colours::ledRed,    push::colours::ledWhite,
        };
        return swatches;
    }
}

//==============================================================================
void MpeShapingView::AxisColourRow::setSelectedColour (juce::Colour colour)
{
    if (selected == colour)
        return;

    selected = colour;
    repaint();
}

void MpeShapingView::AxisColourRow::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    area.removeFromTop (kHeadingGapTop);

    const auto heading = area.removeFromTop (kHeadingHeight);
    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (10.0f));
    g.drawText ("Color", heading, juce::Justification::centredLeft);

    const auto& swatchColours = quickSwatchColours();
    for (int i = 0; i < (int) swatchColours.size(); ++i)
    {
        const auto cell   = swatchBounds (i).toFloat();
        const auto colour = swatchColours[(size_t) i];

        g.setColour (colour);
        g.fillRoundedRectangle (cell, 3.0f);

        g.setColour (colour == selected ? push::colours::ledWhite : push::colours::outline);
        g.drawRoundedRectangle (cell.reduced (0.75f), 3.0f, 1.5f);
    }
}

juce::Rectangle<int> MpeShapingView::AxisColourRow::swatchBounds (int index) const noexcept
{
    return { index * (kSwatchSize + kSwatchGap),
             kHeadingGapTop + kHeadingHeight + kHeadingGapBottom,
             kSwatchSize, kSwatchSize };
}

int MpeShapingView::AxisColourRow::swatchIndexAt (juce::Point<int> pos) const noexcept
{
    // Hit-Zone großzügig (Touch): x-Band des Swatches + halber Gap, volle
    // Zeilenhöhe — die 16-px-Optik bleibt Design-Vorgabe.
    for (int i = 0; i < (int) quickSwatchColours().size(); ++i)
    {
        const auto band = swatchBounds (i)
                              .withY (0).withHeight (getHeight())
                              .expanded (kSwatchGap / 2, 0);
        if (band.contains (pos))
            return i;
    }

    return -1;
}

void MpeShapingView::AxisColourRow::mouseDown (const juce::MouseEvent& event)
{
    pressPosition      = event.getPosition();
    longPressTriggered = false;
    startTimer (kLongPressMs);
}

void MpeShapingView::AxisColourRow::mouseUp (const juce::MouseEvent&)
{
    stopTimer();

    if (longPressTriggered)
    {
        longPressTriggered = false;
        return;   // Picker ist bereits offen — kein zusätzlicher Swatch-Tap
    }

    // Kurzer Tap: Swatch unter dem AUFSETZ-Punkt (Finger-Wackeln tolerant).
    const auto index = swatchIndexAt (pressPosition);
    if (index < 0)
        return;

    const auto colour = quickSwatchColours()[(size_t) index];
    setSelectedColour (colour);

    if (onColourPicked != nullptr)
        onColourPicked (colour);
}

void MpeShapingView::AxisColourRow::timerCallback()
{
    stopTimer();
    longPressTriggered = true;

    if (onLongPress != nullptr)
        onLongPress();
}

//==============================================================================
juce::String MpeShapingView::BendRangeSelector::labelForIndex (int index)
{
    switch (index)
    {
        case 0: return juce::String::fromUTF8 ("¼");
        case 1: return juce::String::fromUTF8 ("½");
        case 2: return "x1";
        case 3: return "x2";
        case 4: return "x4";
        case 5: return "x8";
        default: return {};
    }
}

juce::Rectangle<int> MpeShapingView::BendRangeSelector::cellBounds (int index) const noexcept
{
    auto area = getLocalBounds();
    area.removeFromTop (18);   // Ueberschrift "Bend Range"

    const auto cellW = area.getWidth() / kCols;
    const auto cellH = area.getHeight() / kRows;
    const auto row = index / kCols;
    const auto col = index % kCols;

    return { area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH };
}

int MpeShapingView::BendRangeSelector::cellIndexAt (juce::Point<int> pos) const noexcept
{
    for (int i = 0; i < (int) kMultipliers.size(); ++i)
        if (cellBounds (i).contains (pos))
            return i;

    return -1;
}

void MpeShapingView::BendRangeSelector::setSelectedIndex (int index) noexcept
{
    if (index < 0 || index >= (int) kMultipliers.size() || index == selectedIndex)
        return;

    selectedIndex = index;
    repaint();
}

void MpeShapingView::BendRangeSelector::setAccentColour (juce::Colour newColour) noexcept
{
    if (accentColour == newColour)
        return;

    accentColour = newColour;
    repaint();
}

void MpeShapingView::BendRangeSelector::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    const auto heading = area.removeFromTop (18);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (10.0f));
    g.drawText ("Bend Range", heading, juce::Justification::centredLeft);

    for (int i = 0; i < (int) kMultipliers.size(); ++i)
    {
        const auto cell = cellBounds (i).reduced (2).toFloat();
        const auto isSelected = i == selectedIndex;

        g.setColour (isSelected ? accentColour : push::colours::tile);
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (isSelected ? push::colours::ledWhite : push::colours::outline);
        g.drawRoundedRectangle (cell, 3.0f, 1.0f);

        g.setColour (isSelected ? push::colours::panel : push::colours::text);
        g.setFont (push::scaledFont (12.0f));
        g.drawFittedText (labelForIndex (i), cell.toNearestInt(), juce::Justification::centred, 1, 1.0f);
    }
}

void MpeShapingView::BendRangeSelector::mouseUp (const juce::MouseEvent& event)
{
    const auto index = cellIndexAt (event.getPosition());
    if (index < 0)
        return;

    setSelectedIndex (index);

    if (onMultiplierChanged != nullptr)
        onMultiplierChanged (kMultipliers[(size_t) index]);
}

//==============================================================================
MpeShapingView::MpeShapingView (grid::GridVoiceEngine& engineToUse, GridPanelSettings& panelSettingsToUse)
    : engine (engineToUse), panelSettings (panelSettingsToUse),
      sections {{
          AxisSection { grid::GridVoiceEngine::Axis::Pressure,  "Pressure",
                        panelSettingsToUse.getAxisColour (grid::GridVoiceEngine::Axis::Pressure) },
          AxisSection { grid::GridVoiceEngine::Axis::Slide,     "Slide",
                        panelSettingsToUse.getAxisColour (grid::GridVoiceEngine::Axis::Slide) },
          AxisSection { grid::GridVoiceEngine::Axis::PitchBend, "PitchBend",
                        panelSettingsToUse.getAxisColour (grid::GridVoiceEngine::Axis::PitchBend) },
      }}
{
    for (auto& section : sections)
        section.scratch.reserve ((size_t) grid::VoiceAllocator::kMaxVoices);

    for (auto& section : sections)
        section.fadeTracker.setFadeMs (panelSettings.getNoteCircleFadeMs());

    cachedThresholdWidth = panelSettings.getEditorThresholdWidth();
    cachedFadeMs         = panelSettings.getNoteCircleFadeMs();

    // Schloss-Toggle: nur Pressure/Slide (PitchBend bekommt noch keins,
    // der Platz gehört dem bendRangeSelector). Akzentfarbe je Achse =
    // Kurvenfarbe (section.colour).
    setupOffsetToggle (pressureOffsetToggle, pressureOffsetLabel,
                      grid::GridVoiceEngine::Axis::Pressure, sections[0].colour);
    setupOffsetToggle (slideOffsetToggle, slideOffsetLabel,
                      grid::GridVoiceEngine::Axis::Slide, sections[1].colour);

    // Block K: persistierte Zustände anzeigen — Sensitivity/Bend-Range aus
    // GridPanelSettings (die GridPage hat sie beim Start bereits aufs
    // Keyboard angewandt), Kurven-Krümmungs-Schatten aus den geladenen
    // ResponseCurves der Engine (die Session lädt VOR dem Bau dieser View).
    pressureSensField.setValue (panelSettings.getPressureSensitivity(), juce::dontSendNotification);
    slideSensField.setValue (panelSettings.getSlideSensitivity(), juce::dontSendNotification);
    bendRangeSelector.setSelectedIndex (panelSettings.getBendRangeIndex());

    for (auto& section : sections)
    {
        const auto& curve = engine.responseCurve (section.axis);
        for (size_t segment = 0; segment < section.segmentCurvature.size(); ++segment)
            section.segmentCurvature[segment] = curve.segmentCurvature ((int) segment);
    }

    // Sensitivity-Felder (Block A2): Config kommt aus dem In-Class-
    // Initialisierer (Header) -- hier nur Akzentfarbe + Verdrahtung.
    addChildComponent (pressureSensField);
    pressureSensField.setAccentColour (sections[0].colour);
    pressureSensField.onValueChanged = [this] (double v)
    {
        if (onSensitivityChanged != nullptr)
            onSensitivityChanged (grid::GridVoiceEngine::Axis::Pressure, v);
    };

    addChildComponent (slideSensField);
    slideSensField.setAccentColour (sections[1].colour);
    slideSensField.onValueChanged = [this] (double v)
    {
        if (onSensitivityChanged != nullptr)
            onSensitivityChanged (grid::GridVoiceEngine::Axis::Slide, v);
    };

    // PitchBend-Range-Multiplikator (Block A3).
    addChildComponent (bendRangeSelector);
    bendRangeSelector.setAccentColour (sections[2].colour);
    bendRangeSelector.onMultiplierChanged = [this] (float multiplier)
    {
        if (onPitchBendMultiplierChanged != nullptr)
            onPitchBendMultiplierChanged (multiplier);
    };

    // „Color"-Zeile je Achse (alle drei): Tap wählt sofort, Gedrückthalten
    // öffnet den ConduitColorPicker — beides läuft durch applyAxisColour.
    for (size_t i = 0; i < colourRows.size(); ++i)
    {
        auto& row = colourRows[i];
        addChildComponent (row);
        row.setSelectedColour (sections[i].colour);

        const auto index = (int) i;
        row.onColourPicked = [this, index] (juce::Colour colour) { applyAxisColour (index, colour); };
        row.onLongPress    = [this, index] { openColourPicker (index); };
    }
}

MpeShapingView::~MpeShapingView()
{
    // Offene Picker-CallOutBox schließen — ihr Callback hält zwar nur einen
    // SafePointer, aber eine verwaiste Box über einer verschwundenen View
    // wäre Zombie-UI.
    if (activeColourPicker != nullptr)
        activeColourPicker->dismiss();
}

//==============================================================================
void MpeShapingView::applyAxisColour (int sectionIndex, juce::Colour colour)
{
    if (sectionIndex < 0 || sectionIndex >= (int) sections.size())
        return;

    auto& section = sections[(size_t) sectionIndex];
    section.colour = colour;

    colourRows[(size_t) sectionIndex].setSelectedColour (colour);

    // LockToggle-/Sensitivity-/Range-Akzent folgt der Achsfarbe (die
    // setAccentColour-Aufrufe repainten selbst).
    if (section.axis == grid::GridVoiceEngine::Axis::Pressure)
    {
        pressureOffsetToggle.setAccentColour (colour);
        pressureSensField.setAccentColour (colour);
    }
    else if (section.axis == grid::GridVoiceEngine::Axis::Slide)
    {
        slideOffsetToggle.setAccentColour (colour);
        slideSensField.setAccentColour (colour);
    }
    else
    {
        bendRangeSelector.setAccentColour (colour);
    }

    panelSettings.setAxisColour (section.axis, colour);

    if (onAxisColourChanged != nullptr)
        onAxisColourChanged (section.axis, colour);

    repaint();
}

void MpeShapingView::openColourPicker (int sectionIndex)
{
    if (sectionIndex < 0 || sectionIndex >= (int) sections.size())
        return;

    auto picker = std::make_unique<ConduitColorPicker>();
    picker->setColour (sections[(size_t) sectionIndex].colour);

    // SafePointer: die CallOutBox kann die View überleben (Page-Wechsel) —
    // dann darf der Live-Callback nicht mehr feuern.
    juce::Component::SafePointer<MpeShapingView> safeThis (this);
    picker->onColourChanged = [safeThis, sectionIndex] (juce::Colour colour)
    {
        if (safeThis != nullptr)
            safeThis->applyAxisColour (sectionIndex, colour);
    };

    activeColourPicker = &juce::CallOutBox::launchAsynchronously (
        std::move (picker), colourRows[(size_t) sectionIndex].getScreenBounds(), nullptr);
}

void MpeShapingView::setupOffsetToggle (LockToggle& toggle, juce::Label& label,
                                        grid::GridVoiceEngine::Axis axis, juce::Colour accentColour)
{
    addChildComponent (toggle);
    addChildComponent (label);

    toggle.setAccentColour (accentColour);
    toggle.setTooltip ("Offset darf den Ausgang über die Kurven-Grenze hinausschieben");
    toggle.setActive (engine.offsetBeyondMax (axis));
    toggle.onClick = [this, &toggle, axis]
    {
        const auto shouldAllow = ! toggle.isActive();
        toggle.setActive (shouldAllow);
        engine.setOffsetBeyondMax (axis, shouldAllow);
    };

    label.setText ("Offset", juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, push::colours::textDim);
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

    // Live-Rückweg vom DevPanel (Block A4): GridPanelSettings ist kein
    // ChangeBroadcaster, daher Polling -- billig genug für zwei ints/Frame.
    const auto thresholdWidth = panelSettings.getEditorThresholdWidth();
    if (thresholdWidth != cachedThresholdWidth)
    {
        cachedThresholdWidth = thresholdWidth;
        resized();
    }

    const auto fadeMs = panelSettings.getNoteCircleFadeMs();
    if (fadeMs != cachedFadeMs)
    {
        cachedFadeMs = fadeMs;
        for (auto& section : sections)
            section.fadeTracker.setFadeMs (cachedFadeMs);
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

    // Zwei-Finger-Gesten (Block C): Mittelpunkt-Drag hebt den Griff hervor,
    // die stufenlose Drehung betont die Kurvenlinie (wie Kruemmungs-Wisch).
    for (const auto& entry : twoFingerBySection)
    {
        if (entry.first < 0 || entry.first >= (int) activeTargets.size())
            continue;

        if (entry.second.dragging)
            activeTargets[(size_t) entry.first] = grid::CurveEditInteraction::Target::MidPoint;
        else if (entry.second.rotating)
            activeTargets[(size_t) entry.first] = grid::CurveEditInteraction::Target::Curvature;
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
    const auto lo = juce::jmin (outMin, outMax);
    const auto hi = juce::jmax (outMin, outMax);

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
    // Geklemmt auf [lo,hi] statt fix [0,1] -- die Linie liegt an der
    // gesetzten Grenze flach an, statt weiter zu extrapolieren.
    juce::Path path;
    for (int i = 0; i <= kCurveSamples; ++i)
    {
        const auto x = (float) i / (float) kCurveSamples;
        const auto px = bounds.getX() + x * bounds.getWidth();
        const auto py = valueToTileY (curve.apply (x), lo, hi, bounds);

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

    const auto minPos = juce::Point<float> (bounds.getX(), valueToTileY (outMin, lo, hi, bounds));
    const auto maxPos = juce::Point<float> (bounds.getRight(), valueToTileY (outMax, lo, hi, bounds));

    const auto minRadius = minActive ? kEndpointRadiusActive : kEndpointRadius;
    g.setColour (minActive ? push::colours::ledWhite : section.colour);
    g.fillEllipse (juce::Rectangle<float> (minRadius * 2.0f, minRadius * 2.0f).withCentre (minPos));

    const auto maxRadius = maxActive ? kEndpointRadiusActive : kEndpointRadius;
    g.setColour (maxActive ? push::colours::ledWhite : section.colour);
    g.fillEllipse (juce::Rectangle<float> (maxRadius * 2.0f, maxRadius * 2.0f).withCentre (maxPos));

    // Mittelpunkt-Griff (Block C, 3-Punkt-Kurve): Position = Kontrollpunkt
    // in Feld-Koordinaten (Kurve laeuft exakt durch ihn hindurch). Hohler
    // Ring statt gefuellter Punkt -- unterscheidet ihn von den Endpunkten.
    if (curve.numPoints() == 3)
    {
        const auto& mid = curve.points()[1];
        const auto midValue = outMin + mid.y * (outMax - outMin);
        const auto midPos = juce::Point<float> (bounds.getX() + mid.x * bounds.getWidth(),
                                                valueToTileY (midValue, lo, hi, bounds));

        const auto midActive = activeTarget == grid::CurveEditInteraction::Target::MidPoint;
        const auto midRadius = midActive ? kEndpointRadiusActive : kEndpointRadius;

        g.setColour (midActive ? push::colours::ledWhite : section.colour);
        g.drawEllipse (juce::Rectangle<float> (midRadius * 2.0f, midRadius * 2.0f)
                           .withCentre (midPos), 2.0f);
    }

    // Live-Noten-Kreise: x = jlimit(0,1,rawValue) -- für Achsen mit
    // Rohwertbereich außerhalb [0,1] (PitchBend in Halbtönen) klemmt die
    // Position an den Rand; eine domänenkorrekte Skalierung folgt mit den
    // Achs-Optionen (S2c-2). y = apply(rawValue), geklemmt auf [lo,hi] --
    // ein weitergewischter Rohwert extrapoliert sonst über die gesetzte
    // Kurven-Grenze hinaus (Bugfix: Kreis blieb am ungeklemmten apply()
    // hängen statt am Min/Max-Endpunkt kleben zu bleiben).
    for (const auto& circle : section.fadeTracker.circles())
    {
        const auto x = juce::jlimit (0.0f, 1.0f, circle.rawValue);
        const auto px = bounds.getX() + x * bounds.getWidth();
        const auto py = valueToTileY (curve.apply (circle.rawValue), lo, hi, bounds);

        g.setColour (section.colour.withAlpha (circle.opacity));
        g.fillEllipse (juce::Rectangle<float> (kNoteCircleDiameter, kNoteCircleDiameter)
                           .withCentre ({ px, py }));

        // Zweite Höhenmarke am rechten Feldrand: finaler Ausgang inkl. Kurve+
        // Offset (combinedValue). Anders als Kurvenlinie/Endpunkte/Noten-
        // Kreise NICHT auf die Kurven-Grenze [lo,hi] geklemmt, sondern auf
        // die Achsen-Kapazität -- bei aktivem "Offset"-Schloss reicht der
        // echte, hörbare Ausgang weiter als die Kurve selbst zeigt, und die
        // Marke muss das sichtbar machen (sonst wäre sie bei aktivem Schloss
        // wieder auf die Kurven-Grenze eingefroren, obwohl der Ton weiterläuft).
        const auto markerY = valueToTileY (circle.combinedValue, kAxisCapacityMin, kAxisCapacityMax, bounds);
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

    const auto sectionHeight = bounds.getHeight() / (int) sections.size();
    const auto showDetail = getWidth() >= panelSettings.getEditorThresholdWidth();

    for (int i = 0; i < (int) sections.size(); ++i)
    {
        auto& section = sections[(size_t) i];

        // Abgesetzte Kachel: kleiner Gap oben/unten trennt die drei Achsen-
        // Felder sichtbar voneinander (Looper-Kachel-Stil).
        auto sectionBounds = bounds.removeFromTop (sectionHeight).reduced (0, kSectionGap / 2);
        section.tileBounds = sectionBounds;

        section.detailBounds = showDetail ? sectionBounds.removeFromRight (kDetailColumnWidth)
                                          : juce::Rectangle<int>{};
        section.curveBounds  = sectionBounds;

        // Sensitivity-Feld (Block A2): OBERSTER Detailspalten-Eintrag --
        // nur Pressure/Slide, vor dem Bottom-Stacking abgezogen, damit der
        // Platzhalter-Text (paintAxis) automatisch schrumpft.
        if (section.axis != grid::GridVoiceEngine::Axis::PitchBend)
        {
            const auto isPressure = section.axis == grid::GridVoiceEngine::Axis::Pressure;
            auto& sensField = isPressure ? pressureSensField : slideSensField;

            sensField.setVisible (showDetail);

            if (showDetail)
                sensField.setBounds (section.detailBounds.removeFromTop (NumberFieldBracket::kRowHeight)
                                         .reduced (4, 0));
        }

        // „Color"-Zeile: UNTERSTER Punkt der Detailspalte, alle drei Achsen
        // (bei PitchBend unter dem freien Platzhalter-Bereich). Zuerst von
        // unten abziehen, damit das Offset-Schloss darüber landet.
        auto& colourRow = colourRows[(size_t) i];
        colourRow.setVisible (showDetail);

        if (showDetail)
        {
            auto rowArea = section.detailBounds.removeFromBottom (
                AxisColourRow::kRowHeight + kColourRowBottomPad);
            rowArea.removeFromBottom (kColourRowBottomPad);
            colourRow.setBounds (rowArea.reduced (8, 0));
        }

        // Schloss-Toggle + Label: über der Color-Zeile (Detailspalte von
        // unten), Rest bleibt für den Platzhalter-Text oben (paintAxis
        // liest section.detailBounds live). PitchBend: kein Toggle -- der
        // Platz gehört stattdessen dem bendRangeSelector (Block A3).
        if (section.axis != grid::GridVoiceEngine::Axis::PitchBend)
        {
            const auto isPressure = section.axis == grid::GridVoiceEngine::Axis::Pressure;
            auto& toggle = isPressure ? pressureOffsetToggle : slideOffsetToggle;
            auto& label  = isPressure ? pressureOffsetLabel  : slideOffsetLabel;

            toggle.setVisible (showDetail);
            label.setVisible (showDetail);

            if (showDetail)
            {
                auto toggleRow = section.detailBounds.removeFromBottom (kOffsetToggleRowHeight).reduced (2);
                toggle.setBounds (toggleRow.removeFromLeft (LockToggle::kComponentSize));
                label.setBounds (toggleRow.reduced (4, 0));
            }
        }
        else
        {
            bendRangeSelector.setVisible (showDetail);

            if (showDetail)
                bendRangeSelector.setBounds (
                    section.detailBounds.removeFromBottom (BendRangeSelector::kRowHeight).reduced (2));
        }
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

float MpeShapingView::valueToTileY (float value, float lo, float hi,
                                    juce::Rectangle<float> bounds) noexcept
{
    return bounds.getBottom() - juce::jlimit (lo, hi, value) * bounds.getHeight();
}

int MpeShapingView::gestureCountInSection (int sectionIndex) const noexcept
{
    int count = 0;
    for (const auto& entry : gestures)
        if (entry.second.sectionIndex == sectionIndex)
            ++count;
    return count;
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
    const auto sourceIndex = event.source.getIndex();
    const auto existing = gestureCountInSection (sectionIndex);

    EditGesture gesture;
    gesture.sectionIndex = sectionIndex;
    gesture.startNormY   = normPos.y;
    gesture.lastFieldPos = normPos;

    if (existing == 0)
    {
        // Ein-Finger-Geste wie gehabt -- Trefferradius: 44 px (Touch-
        // Target-Regel) normiert auf die Feldhöhe; hitTest kennt seit
        // Block C auch den Mittelpunkt der 3-Punkt-Kurve.
        const auto hitRadiusNorm = kTouchTargetPx / juce::jmax (1.0f, fieldBounds.getHeight());

        gesture.target = grid::CurveEditInteraction::hitTest (normPos, curve.getOutputMin(),
                                                              curve.getOutputMax(), hitRadiusNorm, curve);
        gesture.segmentIndex = grid::CurveEditInteraction::curvatureSegmentAt (curve, normPos.x);
        gesture.curvatureAtDown = section.segmentCurvature[(size_t) gesture.segmentIndex];
    }
    else if (existing == 1 && twoFingerBySection.find (sectionIndex) == twoFingerBySection.end())
    {
        // Zweiter Finger auf derselben Sektion (Block C): Ein-Finger-
        // Bearbeitung beider Finger neutralisieren, Zwei-Finger-Geste
        // starten (Drehung = Toggle, Zentroid-Drag = Mittelpunkt).
        for (auto& entry : gestures)
        {
            if (entry.second.sectionIndex == sectionIndex)
            {
                TwoFingerGesture twoFinger;
                twoFinger.fingerA  = entry.first;
                twoFinger.startA   = entry.second.lastFieldPos;
                twoFinger.currentA = entry.second.lastFieldPos;
                twoFinger.fingerB  = sourceIndex;
                twoFinger.startB   = normPos;
                twoFinger.currentB = normPos;

                // Stufenlose Drehung setzt am Ist-Zustand an: bei einer
                // 3-Punkt-Kurve ist Segment 0 = +Bauchigkeit (Schatten);
                // bei einer 2-Punkt-Kurve merken wir uns deren Kruemmung
                // fuer die Rueckkehr in die Null-Lage.
                if (curve.numPoints() == 3)
                    twoFinger.shapeAtStart = section.segmentCurvature[0];
                else
                    twoFinger.restoreCurvature = section.segmentCurvature[0];

                twoFingerBySection[sectionIndex] = twoFinger;

                entry.second.target = grid::CurveEditInteraction::Target::None;
                break;
            }
        }
    }
    else if (existing == 2)
    {
        // Dritter Finger (Block C): 2 s halten = Reset auf 2-Punkt-Default.
        resetPendingSection = sectionIndex;
        startTimer (kResetHoldMs);
    }

    gestures[sourceIndex] = gesture;

    repaint();
}

void MpeShapingView::processTwoFingerGesture (int sectionIndex, TwoFingerGesture& twoFinger)
{
    auto& section = sections[(size_t) sectionIndex];
    auto& curve = engine.responseCurve (section.axis);

    const auto degrees = grid::CurveEditInteraction::rotationDegrees (
        twoFinger.startA, twoFinger.startB, twoFinger.currentA, twoFinger.currentB);

    if (! twoFinger.rotating && ! twoFinger.dragging)
    {
        // Klassifikation: was zuerst die Schwelle reisst, gewinnt fuer die
        // ganze Geste. Drehen nur bei weit aufgeklapptem Panel (Block C).
        const auto wideOpen = getWidth() >= panelSettings.getEditorThresholdWidth();

        if (std::abs (degrees) >= kRotateStartDegrees && wideOpen)
        {
            twoFinger.rotating = true;
        }
        else
        {
            const auto startCentroid   = (twoFinger.startA + twoFinger.startB) / 2.0f;
            const auto currentCentroid = (twoFinger.currentA + twoFinger.currentB) / 2.0f;

            if (currentCentroid.getDistanceFrom (startCentroid) >= kMidDragStartNorm
                && curve.numPoints() == 3)
                twoFinger.dragging = true;
        }
    }

    if (twoFinger.rotating)
    {
        // Stufenlos (User-Feedback 11.07.): der Drehwinkel steuert live die
        // gegensinnige Bauchigkeit, ausgehend vom Wert bei Gesten-Beginn --
        // zurueck zur Null-Lage laesst den Mittelpunkt wieder verschwinden.
        const auto amount = juce::jlimit (-1.0f, 1.0f,
            twoFinger.shapeAtStart + grid::CurveEditInteraction::degreesToShapeAmount (degrees));

        grid::CurveEditInteraction::applyShapeAmount (curve, amount, twoFinger.restoreCurvature);

        if (curve.numPoints() == 3)
            section.segmentCurvature = { amount, -amount };
        else
            section.segmentCurvature = { twoFinger.restoreCurvature, 0.0f };
    }
    else if (twoFinger.dragging)
    {
        const auto centroid = (twoFinger.currentA + twoFinger.currentB) / 2.0f;
        grid::CurveEditInteraction::applyMidPointDrag (curve, centroid,
                                                       curve.getOutputMin(), curve.getOutputMax());
    }
}

void MpeShapingView::mouseDrag (const juce::MouseEvent& event)
{
    const auto it = gestures.find (event.source.getIndex());
    if (it == gestures.end())
        return;

    auto& gesture = it->second;
    if (gesture.sectionIndex < 0)
        return;

    auto& section = sections[(size_t) gesture.sectionIndex];
    const auto fieldBounds = curveFieldBounds (section);
    if (fieldBounds.isEmpty())
        return;

    const auto normPos = normalisedPositionIn (fieldBounds, event.position);
    gesture.lastFieldPos = normPos;

    // Zwei-Finger-Geste dieser Sektion? Dann dort verarbeiten (Block C).
    const auto twoFingerIt = twoFingerBySection.find (gesture.sectionIndex);
    if (twoFingerIt != twoFingerBySection.end())
    {
        auto& twoFinger = twoFingerIt->second;
        const auto sourceIndex = event.source.getIndex();

        if (sourceIndex == twoFinger.fingerA || sourceIndex == twoFinger.fingerB)
        {
            (sourceIndex == twoFinger.fingerA ? twoFinger.currentA : twoFinger.currentB) = normPos;
            processTwoFingerGesture (gesture.sectionIndex, twoFinger);
            repaint();
            return;
        }
    }

    if (gesture.target == grid::CurveEditInteraction::Target::None)
        return;

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

        case grid::CurveEditInteraction::Target::MidPoint:
            // Ein-Finger-Drag auf dem Mittelpunkt-Griff (Block C) --
            // zusaetzlich zum Zwei-Finger-Drag, gleiches Ziel.
            grid::CurveEditInteraction::applyMidPointDrag (curve, normPos,
                                                           curve.getOutputMin(), curve.getOutputMax());
            break;

        case grid::CurveEditInteraction::Target::Curvature:
        {
            const auto segment = gesture.segmentIndex;
            const auto newCurvature = juce::jlimit (-1.0f, 1.0f,
                gesture.curvatureAtDown + grid::CurveEditInteraction::curvatureDelta (
                    gesture.startNormY, normPos.y, kCurvatureSensitivity));
            curve.setSegmentCurvature (segment, newCurvature);
            section.segmentCurvature[(size_t) segment] = newCurvature;
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
    const auto sourceIndex = event.source.getIndex();
    const auto it = gestures.find (sourceIndex);
    const auto sectionIndex = it != gestures.end() ? it->second.sectionIndex : -1;

    gestures.erase (sourceIndex);

    if (sectionIndex >= 0)
    {
        // Zwei-Finger-Geste aufloesen, sobald einer der beiden abhebt.
        const auto twoFingerIt = twoFingerBySection.find (sectionIndex);
        if (twoFingerIt != twoFingerBySection.end()
            && (twoFingerIt->second.fingerA == sourceIndex || twoFingerIt->second.fingerB == sourceIndex))
            twoFingerBySection.erase (twoFingerIt);

        // 3-Finger-Reset abbrechen, wenn die Sektion unter 3 Finger faellt.
        if (resetPendingSection == sectionIndex && gestureCountInSection (sectionIndex) < 3)
        {
            resetPendingSection = -1;
            stopTimer();
        }
    }

    repaint();
}

void MpeShapingView::timerCallback()
{
    stopTimer();

    const auto sectionIndex = resetPendingSection;
    resetPendingSection = -1;

    if (sectionIndex < 0 || sectionIndex >= (int) sections.size())
        return;

    if (gestureCountInSection (sectionIndex) < 3)
        return;   // Finger sind schon wieder weg -- kein Reset

    // 3 Finger, 2 s gehalten (Block C): Kurve auf 2-Punkt-Default zurueck.
    auto& section = sections[(size_t) sectionIndex];
    grid::CurveEditInteraction::resetToDefault (engine.responseCurve (section.axis));
    section.segmentCurvature = {};

    // Weiterziehen der liegenden Finger darf nichts mehr editieren.
    for (auto& entry : gestures)
        if (entry.second.sectionIndex == sectionIndex)
            entry.second.target = grid::CurveEditInteraction::Target::None;
    twoFingerBySection.erase (sectionIndex);

    repaint();
}

} // namespace conduit
