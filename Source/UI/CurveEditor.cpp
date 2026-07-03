#include "CurveEditor.h"

#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr float handleRadius  = 6.0f;
    constexpr float handleHitZone = 16.0f;
    constexpr int   rangeRowHeight = 22;   // Min/Max-Felder (User-Regelbereich)
    constexpr int   footerHeight   = 24;   // linear-Reset
}

//==============================================================================
CurveEditor::CurveEditor (const juce::String& initialCurve, double userMin, double userMax)
    : currentMin (userMin), currentMax (userMax)
{
    if (const auto parsed = ChassisSchema::parseCurve (initialCurve))
    {
        curve = *parsed;
        isLinear = false;
    }
    else
    {
        // Anzeige startet auf der Diagonale — erst ein Drag setzt die Kurve
        curve = { 0.25f, 0.25f, 0.75f, 0.75f };
        isLinear = true;
    }

    // Min/Max des User-Regelbereichs — integriert ins Kurven-Tool (4.6)
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

    resetButton.onClick = [this] { resetToLinear(); };
    addAndMakeVisible (resetButton);

    setSize (preferredSize, preferredSize + rangeRowHeight + footerHeight);
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
}

//==============================================================================
juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().withTrimmedBottom (rangeRowHeight + footerHeight)
                           .toFloat().reduced (10.0f);
}

juce::Point<float> CurveEditor::handlePosition (int handleIndex) const
{
    const auto area = plotArea();
    const auto x = handleIndex == 0 ? curve.x1 : curve.x2;
    const auto y = handleIndex == 0 ? curve.y1 : curve.y2;

    // y wächst nach oben (Wert), Bildschirm-y nach unten
    return { area.getX() + x * area.getWidth(),
             area.getBottom() - y * area.getHeight() };
}

void CurveEditor::notifyChange()
{
    if (onCurveChanged != nullptr)
        onCurveChanged (isLinear ? juce::String() : ChassisSchema::curveToString (curve));
}

void CurveEditor::setHandle (int handleIndex, float x, float y)
{
    auto& hx = handleIndex == 0 ? curve.x1 : curve.x2;
    auto& hy = handleIndex == 0 ? curve.y1 : curve.y2;

    hx = juce::jlimit (0.0f, 1.0f, x);   // [0,1] erzwingt Monotonie (Schema-Doku)
    hy = juce::jlimit (0.0f, 1.0f, y);
    isLinear = false;

    repaint();
    notifyChange();
}

void CurveEditor::resetToLinear()
{
    curve = { 0.25f, 0.25f, 0.75f, 0.75f };
    isLinear = true;
    repaint();
    notifyChange();
}

//==============================================================================
void CurveEditor::paint (juce::Graphics& g)
{
    const auto area = plotArea();

    g.setColour (push::colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (juce::Colour (0xff1b1e22));
    g.fillRect (area);

    // Diagonale als Referenz (linear)
    g.setColour (push::colours::outline);
    g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

    // Bezier-Pfad: Position (x) → Wert (y), gesampelt über evaluateCurve
    juce::Path path;
    path.startNewSubPath (area.getX(), area.getBottom()
                                           - ChassisSchema::evaluateCurve (curve, 0.0f) * area.getHeight());

    for (int i = 1; i <= 48; ++i)
    {
        const auto p = static_cast<float> (i) / 48.0f;
        path.lineTo (area.getX() + p * area.getWidth(),
                     area.getBottom() - ChassisSchema::evaluateCurve (curve, p) * area.getHeight());
    }

    g.setColour (isLinear ? push::colours::textDim : push::colours::ledOrange);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Kontrollpunkte + Verbindungslinien zu den Endpunkten
    g.setColour (push::colours::textDim.withAlpha (0.5f));
    g.drawLine ({ { area.getX(), area.getBottom() }, handlePosition (0) }, 1.0f);
    g.drawLine ({ { area.getRight(), area.getY() }, handlePosition (1) }, 1.0f);

    for (int handle = 0; handle < 2; ++handle)
    {
        const auto centre = handlePosition (handle);
        g.setColour (push::colours::ledWhite);
        g.fillEllipse (centre.x - handleRadius, centre.y - handleRadius,
                       handleRadius * 2.0f, handleRadius * 2.0f);
    }
}

void CurveEditor::resized()
{
    auto bounds = getLocalBounds();
    resetButton.setBounds (bounds.removeFromBottom (footerHeight).reduced (10, 2));

    auto rangeRow = bounds.removeFromBottom (rangeRowHeight).reduced (10, 1);
    minEdit.setBounds (rangeRow.removeFromLeft (rangeRow.getWidth() / 2).reduced (2, 0));
    maxEdit.setBounds (rangeRow.reduced (2, 0));
}

//==============================================================================
void CurveEditor::mouseDown (const juce::MouseEvent& event)
{
    draggedHandle = -1;

    for (int handle = 0; handle < 2; ++handle)
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
    setHandle (draggedHandle,
               (event.position.x - area.getX()) / area.getWidth(),
               (area.getBottom() - event.position.y) / area.getHeight());
}

} // namespace conduit
