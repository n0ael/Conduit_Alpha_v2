#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "UI/AnimatedValue.h"

namespace conduit
{

//==============================================================================
/**
    Remote-Volume-Fader der TouchLive-Page in Conduit-Push-Optik
    (User-Entscheidung 09.07.2026: eigene Fader-Skizzen vorerst geparkt,
    Page im Conduit-Look — Skin-Idee „Conduit / Ableton mirrored" notiert,
    Dossier §11): schmale Rinne mit Füllung von unten + Griffstein mit
    Mittellinie (Formsprache PushLookAndFeel::drawLinearSlider), rechts
    dB-Beschriftung 0/12/24/36/48/60 mit Tick-Strichen — die Label-Dichte
    passt sich der Höhe an. Das Stereo-Meter kommt als eigene Spalte mit
    dem Meter-Pfad (M2, Muster GainFaderMeter).

    Positionsskala ist dB-linear (+6 oben … −72 unten — wie die
    FX-Chassis-Gain-Fader); Wert ↔ dB über touchlive::faderscale.

    Feel-Regeln (docs/TouchLive.md §5.1):
    - Drag ist RELATIV (fein per Wisch-Distanz, kein Cap-Sprung beim
      Antippen), Shift = Feinmodus. Doppeltipp = 0 dB.
    - Während der Geste folgt der Fader NUR dem Finger (lokal-optimistisch);
      onUserValue feuert pro Bewegung — der Besitzer thinned/sendet.
    - Fremd-Feedback (setRemoteValue) läuft über ein kurzes Slew (~30 ms,
      AnimatedValue) statt hart zu springen; während der Geste ignoriert.

    beginGesture()/dragGestureTo()/endGesture()/resetToUnity() sind die
    testbaren Kernpfade der Maus-Handler (Muster HoldTile).
*/
class TouchLiveFader final : public juce::Component
{
public:
    TouchLiveFader();

    static constexpr double topDb    = 6.0;    // Skalen-Kopf
    static constexpr double bottomDb = -72.0;  // Skalen-Fuß (−inf-Stub)
    static constexpr int slewMs = 30;

    /** Jede User-Änderung (Drag-Bewegung, Doppeltipp) — Wert ist Lives
        roher Volume-Parameter 0..1. */
    std::function<void (float)> onUserValue;

    /** Modell → UI: slewt zum Ziel; während einer Geste ignoriert
        (Echo-Suppression übernimmt der Client). */
    void setRemoteValue (float newValue);

    /** Initial-/Testwert ohne Animation. */
    void setValueSilent (float newValue);

    [[nodiscard]] float getDisplayedValue() const noexcept { return displayedValue; }
    [[nodiscard]] bool isDragging() const noexcept { return dragging; }

    //==========================================================================
    // Testbare Gesten-Kernpfade (Maus-Handler rufen genau diese)

    void beginGesture();
    void dragGestureTo (float totalDeltaY, bool fine);
    void endGesture();
    void resetToUnity();   // Doppeltipp → 0 dB

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

    /** Fader-Spalte (ohne dB-Label-Spalte) — Drag-Höhe und Layout-Checks. */
    [[nodiscard]] juce::Rectangle<float> trackArea() const;

private:
    //==========================================================================
    [[nodiscard]] float normFromValue (double value) const;
    void setUserValue (float newValue);

    AnimatedValue slew { *this };

    float displayedValue = 0.85f;   // Unity
    float gestureStartValue = 0.85f;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveFader)
};

} // namespace conduit
