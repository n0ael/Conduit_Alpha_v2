#include "CurveEditor.h"

#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr float handleRadius   = 6.0f;
    constexpr float endpointRadius = 7.0f;
    constexpr float handleHitZone  = 16.0f;
    constexpr int   tabRowHeight   = 22;   // Fader/Link-Umschalter
    constexpr int   rangeRowHeight = 22;   // Min/Max-Felder (User-Regelbereich)
    constexpr int   linkRowHeight  = 22;   // Control-Link: Quelle + Amount
    constexpr int   footerHeight   = 24;   // linear-Reset

    constexpr int noLinkItemId    = 1;     // ComboBox: "kein Link"
    constexpr int firstSourceItem = 2;

    // Mindestabstand der Range-Endpunkte (Anteil der Hard-Range)
    constexpr double minRangeGapFraction = 0.01;
} // namespace

//==============================================================================
CurveEditor::CurveEditor (const juce::String& initialCurve, double userMin, double userMax,
                          double hardMinToUse, double hardMaxToUse,
                          const juce::StringArray& linkSources,
                          const juce::String& currentLinkSource, double currentLinkAmount,
                          const juce::String& initialLinkCurve)
    : currentMin (userMin), currentMax (userMax),
      hardMin (hardMinToUse), hardMax (hardMaxToUse),
      sources (linkSources)
{
    if (const auto parsed = ChassisSchema::parseCurve (initialCurve))
        faderCurve = { *parsed, false };

    if (const auto parsed = ChassisSchema::parseCurve (initialLinkCurve))
        linkCurve = { *parsed, false };

    // Tab-Umschalter: Link nur wählbar, wenn eine Quelle gesetzt ist
    faderTabButton.onClick = [this] { setActiveTab (Tab::fader); };
    linkTabButton.onClick  = [this] { setActiveTab (Tab::link); };
    addAndMakeVisible (faderTabButton);
    addAndMakeVisible (linkTabButton);

    // Min/Max des User-Regelbereichs — Textfelder zusätzlich zu den
    // draggbaren Range-Endpunkten in der Grafik
    for (auto* edit : { &minEdit, &maxEdit })
    {
        edit->setFont (juce::Font (juce::FontOptions (11.0f)));
        edit->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        edit->setJustificationType (juce::Justification::centred);
        edit->setEditable (true, true, false);
        edit->onTextChange = [this] { commitRange(); };
        addAndMakeVisible (*edit);
    }

    refreshRangeFields();

    // Control-Link (4.6, modulintern): Quelle + bipolarer Amount
    linkSourceBox.addItem ("kein Link", noLinkItemId);

    for (int i = 0; i < sources.size(); ++i)
        linkSourceBox.addItem (sources[i], firstSourceItem + i);

    const auto sourceIndex = sources.indexOf (currentLinkSource);
    linkSourceBox.setSelectedId (sourceIndex >= 0 ? firstSourceItem + sourceIndex : noLinkItemId,
                                 juce::dontSendNotification);
    linkSourceBox.onChange = [this]
    {
        commitLink();
        updateTabButtons();   // Link-Tab folgt der Quellen-Auswahl

        if (linkSourceBox.getSelectedId() == noLinkItemId && activeTab == Tab::link)
            setActiveTab (Tab::fader);
    };
    addAndMakeVisible (linkSourceBox);

    linkAmountSlider.setRange (-1.0, 1.0, 0.0);
    linkAmountSlider.setValue (juce::jlimit (-1.0, 1.0, currentLinkAmount),
                               juce::dontSendNotification);
    linkAmountSlider.setDoubleClickReturnValue (true, 0.0);
    linkAmountSlider.onValueChange = [this] { commitLink(); };
    addAndMakeVisible (linkAmountSlider);

    resetButton.onClick = [this] { resetToLinear(); };
    addAndMakeVisible (resetButton);

    updateTabButtons();
    setSize (preferredSize,
             preferredSize + tabRowHeight + rangeRowHeight + linkRowHeight + footerHeight);
}

//==============================================================================
CurveEditor::CurveState& CurveEditor::activeCurve() noexcept
{
    return activeTab == Tab::link ? linkCurve : faderCurve;
}

const CurveEditor::CurveState& CurveEditor::activeCurve() const noexcept
{
    return activeTab == Tab::link ? linkCurve : faderCurve;
}

ChassisSchema::BezierCurve CurveEditor::getCurve() const noexcept
{
    return activeCurve().curve;
}

void CurveEditor::setActiveTab (Tab tab)
{
    if (tab == Tab::link && linkSourceBox.getSelectedId() == noLinkItemId)
        return;   // ohne Quelle gibt es keine Link-Kurve

    activeTab = tab;
    updateTabButtons();
    repaint();
}

void CurveEditor::updateTabButtons()
{
    const auto linkActive = linkSourceBox.getSelectedId() != noLinkItemId;
    linkTabButton.setEnabled (linkActive);

    faderTabButton.setColour (juce::TextButton::textColourOffId,
                              activeTab == Tab::fader ? push::colours::ledOrange
                                                      : push::colours::textDim);
    linkTabButton.setColour (juce::TextButton::textColourOffId,
                             activeTab == Tab::link ? push::colours::ledOrange
                                                    : push::colours::textDim);
}

//==============================================================================
void CurveEditor::refreshRangeFields()
{
    minEdit.setText (juce::String (currentMin, 3), juce::dontSendNotification);
    maxEdit.setText (juce::String (currentMax, 3), juce::dontSendNotification);
}

void CurveEditor::commitRange()
{
    const auto newMin = minEdit.getText().getDoubleValue();
    const auto newMax = maxEdit.getText().getDoubleValue();

    // Abgelehnt (außerhalb Hard-Range / min >= max) → Felder restaurieren
    if (onRangeChanged != nullptr && onRangeChanged (newMin, newMax))
    {
        currentMin = newMin;
        currentMax = newMax;
    }

    refreshRangeFields();
    repaint();
}

void CurveEditor::dragEndpointToValue (bool maxEndpoint, double value)
{
    const auto gap = (hardMax - hardMin) * minRangeGapFraction;

    const auto newMin = maxEndpoint ? currentMin
                                    : juce::jlimit (hardMin, currentMax - gap, value);
    const auto newMax = maxEndpoint ? juce::jlimit (currentMin + gap, hardMax, value)
                                    : currentMax;

    if (onRangeChanged != nullptr && onRangeChanged (newMin, newMax))
    {
        currentMin = newMin;
        currentMax = newMax;
    }

    refreshRangeFields();
    repaint();
}

//==============================================================================
void CurveEditor::commitLink()
{
    if (onLinkChanged == nullptr)
        return;

    const auto selected = linkSourceBox.getSelectedId();
    const auto source = selected >= firstSourceItem ? sources[selected - firstSourceItem]
                                                    : juce::String();

    onLinkChanged (source, linkAmountSlider.getValue());
}

void CurveEditor::notifyCurveChange()
{
    const auto& state = activeCurve();
    const auto text = state.isLinear ? juce::String()
                                     : ChassisSchema::curveToString (state.curve);

    if (activeTab == Tab::link)
    {
        if (onLinkCurveChanged != nullptr)
            onLinkCurveChanged (text);
    }
    else if (onCurveChanged != nullptr)
    {
        onCurveChanged (text);
    }
}

void CurveEditor::setHandle (int handleIndex, float x, float y)
{
    auto& state = activeCurve();
    auto& hx = handleIndex == 0 ? state.curve.x1 : state.curve.x2;
    auto& hy = handleIndex == 0 ? state.curve.y1 : state.curve.y2;

    hx = juce::jlimit (0.0f, 1.0f, x);   // [0,1] erzwingt Monotonie (Schema-Doku)
    hy = juce::jlimit (0.0f, 1.0f, y);
    state.isLinear = false;

    repaint();
    notifyCurveChange();
}

void CurveEditor::resetToLinear()
{
    auto& state = activeCurve();
    state.curve = { 0.25f, 0.25f, 0.75f, 0.75f };
    state.isLinear = true;
    repaint();
    notifyCurveChange();
}

//==============================================================================
juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds()
        .withTrimmedTop (tabRowHeight)
        .withTrimmedBottom (rangeRowHeight + linkRowHeight + footerHeight)
        .toFloat().reduced (10.0f);
}

float CurveEditor::yForValue (double value) const
{
    const auto area = plotArea();
    const auto span = hardMax - hardMin;
    const auto norm = span > 0.0 ? (value - hardMin) / span : 0.0;
    return area.getBottom() - static_cast<float> (norm) * area.getHeight();
}

double CurveEditor::valueForY (float y) const
{
    const auto area = plotArea();
    const auto norm = area.getHeight() > 0.0f ? (area.getBottom() - y) / area.getHeight() : 0.0f;
    return hardMin + static_cast<double> (juce::jlimit (0.0f, 1.0f, norm)) * (hardMax - hardMin);
}

juce::Point<float> CurveEditor::handlePosition (int handleIndex) const
{
    const auto area = plotArea();

    // Range-Endpunkte (nur Fader-Tab): (0, userMin) und (1, userMax)
    if (handleIndex == 2)
        return { area.getX(), yForValue (currentMin) };

    if (handleIndex == 3)
        return { area.getRight(), yForValue (currentMax) };

    const auto& state = activeCurve();
    const auto hx = handleIndex == 0 ? state.curve.x1 : state.curve.x2;
    const auto hy = handleIndex == 0 ? state.curve.y1 : state.curve.y2;

    if (activeTab == Tab::link)
        return { area.getX() + hx * area.getWidth(),
                 area.getBottom() - hy * area.getHeight() };

    // Fader-Tab: Kontrollpunkt lebt im User-Range-Fenster
    return { area.getX() + hx * area.getWidth(),
             yForValue (currentMin + static_cast<double> (hy) * (currentMax - currentMin)) };
}

//==============================================================================
void CurveEditor::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    const auto& state = activeCurve();

    g.setColour (push::colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (juce::Colour (0xff1b1e22));
    g.fillRect (area);

    const auto curveColour = state.isLinear ? push::colours::textDim
                                            : push::colours::ledOrange;

    if (activeTab == Tab::link)
    {
        // Link-Ansicht: reine 0..1-Response (Quelle → Modulationsform)
        g.setColour (push::colours::outline);
        g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

        juce::Path path;
        path.startNewSubPath (area.getX(), area.getBottom()
                                               - ChassisSchema::evaluateCurve (state.curve, 0.0f) * area.getHeight());

        for (int i = 1; i <= 48; ++i)
        {
            const auto p = static_cast<float> (i) / 48.0f;
            path.lineTo (area.getX() + p * area.getWidth(),
                         area.getBottom() - ChassisSchema::evaluateCurve (state.curve, p) * area.getHeight());
        }

        g.setColour (curveColour);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        for (int handle = 0; handle < 2; ++handle)
        {
            const auto centre = handlePosition (handle);
            g.setColour (push::colours::ledWhite);
            g.fillEllipse (centre.x - handleRadius, centre.y - handleRadius,
                           handleRadius * 2.0f, handleRadius * 2.0f);
        }

        return;
    }

    // Fader-Ansicht: HARD-Range-Fenster, Kurve läuft von userMin nach userMax
    g.setColour (push::colours::outline.withAlpha (0.6f));
    g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

    // User-Fenster andeuten (Bereich außerhalb bleibt dunkler)
    g.setColour (push::colours::outline.withAlpha (0.35f));
    g.drawHorizontalLine (juce::roundToInt (yForValue (currentMin)), area.getX(), area.getRight());
    g.drawHorizontalLine (juce::roundToInt (yForValue (currentMax)), area.getX(), area.getRight());

    juce::Path path;
    const auto valueAt = [this, &state] (float p)
    {
        return currentMin + static_cast<double> (ChassisSchema::evaluateCurve (state.curve, p))
                                * (currentMax - currentMin);
    };

    path.startNewSubPath (area.getX(), yForValue (valueAt (0.0f)));

    for (int i = 1; i <= 48; ++i)
    {
        const auto p = static_cast<float> (i) / 48.0f;
        path.lineTo (area.getX() + p * area.getWidth(), yForValue (valueAt (p)));
    }

    g.setColour (curveColour);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Verbindungslinien + Kontrollpunkte
    g.setColour (push::colours::textDim.withAlpha (0.5f));
    g.drawLine ({ handlePosition (2), handlePosition (0) }, 1.0f);
    g.drawLine ({ handlePosition (3), handlePosition (1) }, 1.0f);

    for (int handle = 0; handle < 2; ++handle)
    {
        const auto centre = handlePosition (handle);
        g.setColour (push::colours::ledWhite);
        g.fillEllipse (centre.x - handleRadius, centre.y - handleRadius,
                       handleRadius * 2.0f, handleRadius * 2.0f);
    }

    // Range-Endpunkte (draggbar): min unten links, max oben rechts
    for (int handle = 2; handle <= 3; ++handle)
    {
        const auto centre = handlePosition (handle);
        g.setColour (push::colours::ledOrange);
        g.fillEllipse (centre.x - endpointRadius, centre.y - endpointRadius,
                       endpointRadius * 2.0f, endpointRadius * 2.0f);
        g.setColour (juce::Colour (0xff1b1e22));
        g.fillEllipse (centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
    }
}

void CurveEditor::resized()
{
    auto bounds = getLocalBounds();

    auto tabRow = bounds.removeFromTop (tabRowHeight).reduced (10, 1);
    faderTabButton.setBounds (tabRow.removeFromLeft (tabRow.getWidth() / 2).reduced (1, 0));
    linkTabButton.setBounds (tabRow.reduced (1, 0));

    resetButton.setBounds (bounds.removeFromBottom (footerHeight).reduced (10, 2));

    auto linkRow = bounds.removeFromBottom (linkRowHeight).reduced (10, 1);
    linkSourceBox.setBounds (linkRow.removeFromLeft (linkRow.getWidth() * 3 / 5));
    linkAmountSlider.setBounds (linkRow.reduced (2, 0));

    auto rangeRow = bounds.removeFromBottom (rangeRowHeight).reduced (10, 1);
    minEdit.setBounds (rangeRow.removeFromLeft (rangeRow.getWidth() / 2).reduced (2, 0));
    maxEdit.setBounds (rangeRow.reduced (2, 0));
}

//==============================================================================
void CurveEditor::mouseDown (const juce::MouseEvent& event)
{
    draggedHandle = -1;

    // Range-Endpunkte nur im Fader-Tab; Endpunkte gewinnen bei Überlappung
    const auto lastHandle = activeTab == Tab::fader ? 3 : 1;

    for (int handle = lastHandle; handle >= 0; --handle)
        if (handlePosition (handle).getDistanceFrom (event.position) <= handleHitZone)
        {
            draggedHandle = handle;
            break;
        }

    if (draggedHandle >= 0)
        mouseDrag (event);
}

void CurveEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedHandle < 0)
        return;

    const auto area = plotArea();

    // Range-Endpunkte: vertikal ziehen setzt userMin/userMax (Fader-Tab)
    if (draggedHandle >= 2)
    {
        dragEndpointToValue (draggedHandle == 3, valueForY (event.position.y));
        return;
    }

    const auto x = (event.position.x - area.getX()) / area.getWidth();

    if (activeTab == Tab::link)
    {
        setHandle (draggedHandle, x,
                   (area.getBottom() - event.position.y) / area.getHeight());
        return;
    }

    // Fader-Tab: Kontrollpunkt-y im User-Range-Fenster normalisieren
    const auto span = currentMax - currentMin;
    const auto value = valueForY (event.position.y);
    const auto yNorm = span > 0.0 ? (value - currentMin) / span : 0.0;

    setHandle (draggedHandle, x, static_cast<float> (yNorm));
}

} // namespace conduit
