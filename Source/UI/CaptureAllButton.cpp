#include "CaptureAllButton.h"

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr float ringThickness = 3.0f;
    constexpr float fillThickness = 5.0f;

    const juce::Colour idleColour      { 0xff5a5f66 };
    const juce::Colour recordingColour { 0xffe6453c };
    const juce::Colour heldColour      { 0xffe6a23c };
    const juce::Colour exportColour    { 0xff4caf6e };
}

//==============================================================================
CaptureAllButton::CaptureAllButton()
    : juce::Button ("Capture All")
{
    setTooltip ("Capture All: alle aktiven Aufnahmen exportieren");
}

void CaptureAllButton::setStatus (const CaptureService::UiStatus& newStatus)
{
    const auto visibleChange = newStatus.anyRecording != status.anyRecording
                            || newStatus.anyHeld != status.anyHeld
                            || newStatus.exporting != status.exporting
                            || quantizeFill (newStatus.fillNorm) != quantizeFill (status.fillNorm);
    status = newStatus;

    if (visibleChange)
        repaint();
}

int CaptureAllButton::quantizeFill (float fillNorm) noexcept
{
    return juce::roundToInt (juce::jlimit (0.0f, 1.0f, fillNorm) * 48.0f);
}

//==============================================================================
void CaptureAllButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                    bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    const auto square = bounds.withSizeKeepingCentre (side, side);

    const auto stateColour = status.anyRecording ? recordingColour
                           : status.anyHeld      ? heldColour
                                                 : idleColour;

    // Grundring: aggregierter Status
    g.setColour (stateColour.withAlpha (0.45f));
    g.drawEllipse (square.reduced (ringThickness * 0.5f), ringThickness);

    // Dezente Füllstandsanzeige: Bogen ab 12 Uhr, Anteil = vollster Kanal
    if (status.fillNorm > 0.0f)
    {
        juce::Path arc;
        arc.addArc (square.getX() + fillThickness * 0.5f, square.getY() + fillThickness * 0.5f,
                    square.getWidth() - fillThickness, square.getHeight() - fillThickness,
                    0.0f, juce::MathConstants<float>::twoPi
                              * juce::jlimit (0.0f, 1.0f, status.fillNorm),
                    true);
        g.setColour (stateColour);
        g.strokePath (arc, juce::PathStrokeType (fillThickness * 0.5f));
    }

    // Innerer Export-Indikator
    if (status.exporting)
    {
        g.setColour (exportColour);
        g.drawEllipse (square.reduced (side * 0.28f), 2.0f);
    }

    // Zentrum: Touch-Feedback + Beschriftung
    const auto centre = square.reduced (side * 0.18f);
    if (shouldDrawButtonAsDown)
        g.setColour (juce::Colours::white.withAlpha (0.25f));
    else if (shouldDrawButtonAsHighlighted)
        g.setColour (juce::Colours::white.withAlpha (0.12f));
    else
        g.setColour (juce::Colours::transparentBlack);
    g.fillEllipse (centre);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (push::scaledFont (13.0f, true));
    g.drawText ("CAP", square, juce::Justification::centred);
}

} // namespace conduit
