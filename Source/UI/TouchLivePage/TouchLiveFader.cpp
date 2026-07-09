#include "TouchLiveFader.h"

#include "TouchLive/LiveFaderScale.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr float pointerColumn = 10.0f;  // Dreiecks-Zeiger links
    constexpr float tickColumn    = 6.0f;   // Skalen-Striche
    constexpr float labelColumn   = 20.0f;  // dB-Zahlen rechts
    constexpr float capHeight     = 24.0f;

    constexpr int scaleMarksDb[] = { 0, -12, -24, -36, -48, -60 };

    // Feinmodus (Shift): Wisch-Distanz wirkt nur zu einem Viertel
    constexpr float fineDragFactor = 0.25f;
}

//==============================================================================
TouchLiveFader::TouchLiveFader()
{
    slew.onUpdate = [this] (float animated)
    {
        displayedValue = animated;
        repaint();
    };
}

//==============================================================================
void TouchLiveFader::setRemoteValue (float newValue)
{
    if (dragging)
        return;  // Finger gewinnt (5.1) — Echo-Suppression filtert ohnehin

    slew.animateTo (juce::jlimit (0.0f, 1.0f, newValue), slewMs);
}

void TouchLiveFader::setValueSilent (float newValue)
{
    slew.snapTo (juce::jlimit (0.0f, 1.0f, newValue));
}

//==============================================================================
void TouchLiveFader::beginGesture()
{
    dragging = true;
    gestureStartValue = displayedValue;
}

void TouchLiveFader::dragGestureTo (float totalDeltaY, bool fine)
{
    if (! dragging)
        return;

    const auto track = trackArea();

    if (track.getHeight() < 1.0f)
        return;

    // Relativ (kein Cap-Sprung): Wisch-Distanz → dB entlang der Skala
    const auto dbPerPixel = (topDb - bottomDb) / (double) track.getHeight();
    const auto startDb = touchlive::faderscale::dbFromValue (gestureStartValue);
    const auto delta   = (double) (fine ? totalDeltaY * fineDragFactor : totalDeltaY);
    const auto newDb   = juce::jlimit (bottomDb, topDb, startDb - delta * dbPerPixel);

    setUserValue ((float) touchlive::faderscale::valueFromDb (newDb));
}

void TouchLiveFader::endGesture()
{
    dragging = false;
}

void TouchLiveFader::resetToUnity()
{
    setUserValue ((float) touchlive::faderscale::unityValue);
}

void TouchLiveFader::setUserValue (float newValue)
{
    displayedValue = juce::jlimit (0.0f, 1.0f, newValue);
    slew.snapTo (displayedValue);   // Geste = lokal-optimistisch, kein Slew

    if (onUserValue != nullptr)
        onUserValue (displayedValue);
}

//==============================================================================
void TouchLiveFader::mouseDown (const juce::MouseEvent&)
{
    beginGesture();
}

void TouchLiveFader::mouseDrag (const juce::MouseEvent& event)
{
    dragGestureTo ((float) event.getDistanceFromDragStartY(), event.mods.isShiftDown());
}

void TouchLiveFader::mouseUp (const juce::MouseEvent&)
{
    endGesture();
}

void TouchLiveFader::mouseDoubleClick (const juce::MouseEvent&)
{
    resetToUnity();
}

//==============================================================================
juce::Rectangle<float> TouchLiveFader::trackArea() const
{
    auto area = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
    area.removeFromLeft (pointerColumn + tickColumn);
    area.removeFromRight (labelColumn);
    return area;
}

float TouchLiveFader::normFromValue (double value) const
{
    // dB-linear (SVG-Vorlage: gleichmäßige Label-Teilung), 0 = oben
    const auto db = touchlive::faderscale::dbFromValue (value);
    return (float) ((topDb - db) / (topDb - bottomDb));
}

void TouchLiveFader::paint (juce::Graphics& g)
{
    const auto track = trackArea();

    if (track.isEmpty())
        return;

    // Fader-Track: schwarz auf der Kachel (SVG-Vorlage)
    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (push::colours::outline.withAlpha (0.6f));
    g.drawRoundedRectangle (track, 3.0f, 1.0f);

    // (M2: Meter-Füllung IM Track — grün→gelb mit Peak-Hold — landet hier)

    // Skalen-Striche links + dB-Zahlen rechts; Label-Dichte folgt der Höhe
    const auto tickX  = track.getX() - tickColumn;
    const auto labelX = track.getRight() + 2.0f;

    const auto spacing = track.getHeight() * 12.0f / (float) (topDb - bottomDb);
    const auto labelStep = spacing >= 16.0f ? 1 : 2;

    g.setFont (push::scaledFont (9.0f));

    int markIndex = 0;

    for (const auto db : scaleMarksDb)
    {
        const auto tickY = track.getY()
                         + (float) ((topDb - db) / (topDb - bottomDb)) * track.getHeight();

        g.setColour (push::colours::textDim.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (tickX, tickY - 0.5f, tickColumn - 2.0f, 1.0f));

        if (markIndex % labelStep == 0)
        {
            g.setColour (push::colours::textDim);
            g.drawText (juce::String (std::abs (db)),
                        juce::Rectangle<float> (labelX, tickY - 6.0f, labelColumn - 3.0f, 12.0f),
                        juce::Justification::centredLeft);
        }

        ++markIndex;
    }

    // Cap + Dreiecks-Zeiger auf Wert-Höhe
    const auto capCentreY = track.getY() + normFromValue (displayedValue) * track.getHeight();
    const auto capY = juce::jlimit (track.getY(), track.getBottom() - capHeight,
                                    capCentreY - capHeight * 0.5f);

    const juce::Rectangle<float> cap (track.getX() + 1.0f, capY,
                                      track.getWidth() - 2.0f, capHeight);
    g.setColour (juce::Colour (0xff2e2e2e));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (push::colours::text.withAlpha (0.85f));
    g.fillRect (juce::Rectangle<float> (cap.getX() + 2.0f, cap.getCentreY() - 0.5f,
                                        cap.getWidth() - 4.0f, 1.0f));

    juce::Path pointer;
    pointer.addTriangle (1.0f, capCentreY - 5.0f,
                         1.0f, capCentreY + 5.0f,
                         pointerColumn - 1.0f, capCentreY);
    g.setColour (push::colours::text);
    g.fillPath (pointer);

    // dB-Readout während der Geste (oben, über dem Track)
    if (dragging)
    {
        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (10.0f));
        g.drawText (touchlive::faderscale::dbText (displayedValue),
                    getLocalBounds().toFloat().withHeight (14.0f),
                    juce::Justification::centred);
    }
}

} // namespace conduit
