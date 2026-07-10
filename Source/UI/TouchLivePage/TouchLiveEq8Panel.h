#pragma once

#include <array>
#include <map>

#include "TouchLiveBespokePanel.h"

namespace conduit
{

//==============================================================================
/**
    Bespoke EQ-Eight-UI (M5, docs/TouchLive.md §6b): Frequenzgang-Kurve
    mit acht Touch-Punkten statt Fader-Bänken — Darstellung an Lives
    EQ-Eight-Anzeige kalibriert (Messkampagne 10.07.2026, §10i/§10j).

    Parameter-Zuordnung über die parmeta-NAMEN der A-Kurve
    ("{n} Filter On A" · "{n} Filter Type A" · "{n} Frequency A" ·
    "{n} Gain A" · "{n} Q A"/"{n} Resonance A", n = 1…8) — nie über feste
    Indizes; Filtertyp-Semantik aus den items-Strings. Kein vollständiges
    Mapping → isUsable()==false, die DeviceView bleibt bei der Bank.

    Kurvenmathematik: analoge Prototypen (s-Domain) mit Lives
    Q-Semantik — pro Typ ein kalibriertes Q_eff-Gesetz (Bell/Shelf
    gain-abhängig = Adaptive Q; Cuts/Notch 1:1; 48er-Cuts als skalierte
    Butterworth-8-Kaskade). Kalibriert auf < 0.4 dB gegen Lives Anzeige;
    der "Adaptive Q"-Parameter der Gegenseite schaltet den Gain-Term.

    Gesten (User-Spezifikation 10.07.2026, Kernpfade touchDown/Move/Up):
    - Punkt ziehen = Frequenz (X) + Gain (Y bei Bell/Shelf; bei
      Cut/Notch steuert Y den Q wie in Live) — RELATIV mit
      Bewegungsschwelle (der Punkt springt NIE zum Finger);
      Doppeltipp = Band an/aus — alles lokal-optimistisch (§5.1).
      Kein Footer: Typ steht im Readout, alles läuft über Gesten.
    - Punkt berühren und ~1 s STILL halten = Typ-Selector öffnet
      (vertikale Symbolliste wie Lives Dropdown): hoch/runter wischen
      wählt, Loslassen übernimmt. Kernpfad triggerLongPress().
    - Punkt HALTEN + weitere Punkte antippen = Mehrfachauswahl;
      Punkt halten + freier zweiter Finger = Pinch ändert den Q des
      aktiven Bandes (Q-Geste NUR bei berührtem Punkt).
    - OHNE Punktberührung: 2 Finger = alle angewählten Bänder gemeinsam
      verschieben · 3 Finger = Output-Gain fein · 4 Finger = Scale.
      Lässt der haltende Finger los, sind Restfinger bis zum Abheben
      wirkungslos (keine Gesten-Überraschungen).
    Wertesemantik (verifiziert): Hz = 10·2200^norm, Q = 0.1·180^norm,
    Gain direkt dB.
*/
class TouchLiveEq8Panel final : public TouchLiveBespokePanel,
                                private juce::MultiTimer
{
public:
    explicit TouchLiveEq8Panel (TouchLiveClient& clientToUse,
                                LiveSpectrumTap* spectrumTapToUse = nullptr);
    ~TouchLiveEq8Panel() override;

    static constexpr int bandCount = 8;
    static constexpr float handleDiameter = 44.0f;      // "Zeigefinger"-Punkt
    static constexpr float selectedHandleDiameter = 54.0f;
    static constexpr float touchRadius = 34.0f;         // Trefferzone
    static constexpr float dragThreshold = 9.0f;        // px bis Drag greift
    static constexpr int longPressMs = 1000;            // Typ-Selector

    //==========================================================================
    // TouchLiveBespokePanel
    void setDevice (const juce::String& deviceKey, const juce::var& parmeta) override;
    void setValues (const juce::var& parvals) override;
    [[nodiscard]] bool isUsable() const override { return mappedBandCount > 0; }

    //==========================================================================
    // Testbare Kernpfade (die Maus-/Touch-Handler rufen genau diese;
    // touchIndex = MouseInputSource-Index, Maus ist 0)

    enum class Gesture { none, idle, bandDrag, pinchQ, moveSelection,
                         trimOutput, trimScale, typeSelect };

    [[nodiscard]] int bandAt (juce::Point<float> position) const;

    void touchDown (int touchIndex, juce::Point<float> position);
    void touchMove (int touchIndex, juce::Point<float> position);
    void touchUp (int touchIndex);

    /** [Tests/Timer] Öffnet den Typ-Selector, wenn der Primärfinger
        noch still auf seinem Punkt liegt (Long-Press-Bedingungen). */
    void triggerLongPress();

    void selectBand (int band);
    void toggleBandOn (int band);

    //==========================================================================
    [[nodiscard]] Gesture getGesture() const noexcept { return gesture; }
    [[nodiscard]] int getSelectedBand() const noexcept { return selectedBand; }
    [[nodiscard]] bool isBandSelected (int band) const;
    [[nodiscard]] int getMappedBandCount() const noexcept { return mappedBandCount; }
    [[nodiscard]] bool isBandOn (int band) const;
    [[nodiscard]] double getResonanceNorm (int band) const;
    [[nodiscard]] double getFrequencyNorm (int band) const;
    [[nodiscard]] double getGainDb (int band) const;
    [[nodiscard]] juce::Point<float> bandPosition (int band) const;   // Plot-Pixel
    [[nodiscard]] int frequencyIndexOf (int band) const;
    [[nodiscard]] int gainIndexOf (int band) const;
    [[nodiscard]] int resonanceIndexOf (int band) const;
    [[nodiscard]] int outputIndexOf() const noexcept { return outputIndex; }
    [[nodiscard]] int scaleIndexOf() const noexcept { return scaleIndex; }
    [[nodiscard]] bool isTypeSelectorOpen() const noexcept { return gesture == Gesture::typeSelect; }
    [[nodiscard]] int getTypeSelectorHover() const noexcept { return typeSelectorHover; }

    /** [Tests] Anzeige-Kurve in dB bei hz (inkl. Scale-Wirkung, §10j). */
    [[nodiscard]] double curveDbAt (double hz) const { return responseDbAt (hz); }

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    enum class Shape { lowCut12, lowCut48, lowShelf, bell, notch,
                       highShelf, highCut12, highCut48 };

    struct Band
    {
        int onIndex = -1, typeIndex = -1, frequencyIndex = -1,
            gainIndex = -1, resonanceIndex = -1;
        juce::StringArray typeItems;

        bool on = false;
        int typeValue = 3;
        double frequencyNorm = 0.5;        // 0..1 (Wire-Wert)
        double gainDb = 0.0;
        double resonanceNorm = 0.5;        // 0..1 (Wire-Wert)
        double gainMin = -15.0, gainMax = 15.0;

        [[nodiscard]] bool isMapped() const noexcept
        {
            return onIndex >= 0 && typeIndex >= 0 && frequencyIndex >= 0
                && gainIndex >= 0 && resonanceIndex >= 0;
        }
    };

    void sendParameter (int parameterIndex, float value, bool continuous);
    void rebuildCurve();
    void setResonanceNorm (Band& band, double newNorm);

    void dragActiveBandBy (juce::Point<float> delta);
    void beginFreeGesture();
    void timerCallback (int timerId) override;
    void commitTypeSelector();
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area);
    [[nodiscard]] juce::Rectangle<float> typeSelectorBounds() const;
    [[nodiscard]] juce::Point<float> touchCentroid() const;
    [[nodiscard]] int heldBandTouchIndex() const;   // −1 = kein Punkt gehalten

    [[nodiscard]] static Shape shapeForType (const juce::StringArray& typeItems,
                                             int typeValue);
    [[nodiscard]] Shape shapeOf (const Band& band) const;
    [[nodiscard]] bool shapeHasGain (Shape shape) const noexcept;
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    [[nodiscard]] float xForNorm (double norm) const;
    [[nodiscard]] double normForX (float x) const;
    [[nodiscard]] float yForDb (double db) const;
    [[nodiscard]] double dbForY (float y) const;

    /** Summen-Magnitude in dB (analoge Prototypen, Live-kalibriert). */
    [[nodiscard]] double responseDbAt (double hz) const;

    /** Magnitude EINES Typs in dB bei hz/f0 (auch für die Selector-Icons). */
    [[nodiscard]] static double shapeResponseDb (Shape shape, double hzRatio,
                                                 double q, double gainDb);

    /** Lives effektiver RBJ-Q fürs Display (§10j-Kalibrierung). */
    [[nodiscard]] double effectiveQ (Shape shape, double q, double gainDb) const;

    static constexpr int longPressTimerId = 1;
    static constexpr int spectrumTimerId = 2;

    TouchLiveClient& client;
    LiveSpectrumTap* spectrumTap = nullptr;   // nullptr in Tests
    juce::uint32 lastSpectrumRevision = 0;
    juce::String deviceKey;

    std::array<Band, bandCount> bands;
    int mappedBandCount = 0;
    int selectedBand = 0;
    std::array<bool, bandCount> bandSelected {};   // Mehrfachauswahl
    bool adaptiveQ = true;      // Parameter "Adaptive Q" der Gegenseite
    int adaptiveQIndex = -1;

    // Globale EQ8-Parameter (3-/4-Finger-Trim); Scale-Range in Live:
    // −200 % … +200 % (negativ = Kurve invertiert, §10j)
    int outputIndex = -1, scaleIndex = -1;
    double outputValue = 0.0, outputMin = -12.0, outputMax = 12.0;
    double scaleValue = 1.0, scaleMin = -2.0, scaleMax = 2.0;

    // Touch-/Gesten-Zustand (Kernpfade touchDown/Move/Up)
    struct TouchPoint
    {
        juce::Point<float> start, current;
        int bandHit = -1;
    };

    std::map<int, TouchPoint> touches;
    Gesture gesture = Gesture::none;
    int primaryTouchIndex = -1;          // hält ein Band (bandDrag/pinchQ)
    int pinchTouchIndex = -1;
    float pinchStartDistance = 0.0f;
    double pinchStartResonance = 0.5;
    juce::Point<float> gestureStartCentroid;
    double gestureStartValue = 0.0;      // Output/Scale beim Gesten-Start
    std::array<std::pair<double, double>, bandCount> selectionStart {};

    // Band-Drag: relativ mit Schwelle (Punkt springt nie zum Finger)
    bool dragMoved = false;
    double dragStartFrequencyNorm = 0.0, dragStartGainDb = 0.0;
    double dragStartResonanceNorm = 0.5;   // Y-Drag bei Cut/Notch = Q

    // Typ-Selector (Long-Press)
    int typeSelectorHover = -1;

    juce::Path curve;
    bool curveDirty = true;

    static constexpr double plotDbRange = 15.0;   // ±15 dB wie Lives Anzeige

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveEq8Panel)
};

} // namespace conduit
