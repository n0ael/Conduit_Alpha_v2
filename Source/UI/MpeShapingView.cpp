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
MpeShapingView::MpeShapingView (grid::GridVoiceEngine& engineToUse, GridPanelSettings& panelSettingsToUse,
                                UiSettings& uiSettingsToUse)
    : engine (engineToUse), panelSettings (panelSettingsToUse), uiSettings (uiSettingsToUse),
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

    // Schloss-Toggle: nur Pressure/Slide (PitchBend bekommt noch keins,
    // Platz bleibt frei fürs künftige Range-Element). Akzentfarbe je Achse
    // = Kurvenfarbe (section.colour).
    setupOffsetToggle (pressureOffsetToggle, pressureOffsetLabel,
                      grid::GridVoiceEngine::Axis::Pressure, sections[0].colour);
    setupOffsetToggle (slideOffsetToggle, slideOffsetLabel,
                      grid::GridVoiceEngine::Axis::Slide, sections[1].colour);

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

    updateDevSliderVisibility (uiSettings.isDevModeEnabled());
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

    // LockToggle-Akzent folgt der Achsfarbe (setAccentColour repaintet selbst).
    if (section.axis == grid::GridVoiceEngine::Axis::Pressure)
        pressureOffsetToggle.setAccentColour (colour);
    else if (section.axis == grid::GridVoiceEngine::Axis::Slide)
        slideOffsetToggle.setAccentColour (colour);

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
        // liest section.detailBounds live). PitchBend: kein Toggle, Platz
        // bleibt frei.
        // TODO: PitchBend-Range-Element (¼…×8 / Halbtöne) -- späterer Schritt.
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
