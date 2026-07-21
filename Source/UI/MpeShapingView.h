#pragma once

#include <array>
#include <functional>
#include <map>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "Core/CurveEditInteraction.h"
#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "DragCursorHider.h"
#include "LockToggle.h"
#include "NoteCircleFadeTracker.h"
#include "NumberFieldBracket.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    MPE-Shaping-Editor (S2c) -- Inhalt des „MPE"-Tabs im EditorDockPanel der
    Grid-Page: drei gestapelte Achsen-Sektionen (Pressure/Slide/PitchBend),
    je die ResponseCurve der Achse als Kurve gezeichnet (gesampelt über
    GridVoiceEngine::responseCurve(axis).apply(x), x aus [0,1]) mit Min/Max-
    Anzeige, plus Live-Noten-Kreise (readActiveVoices) die beim Spielen auf
    der Kurve wandern und nach dem Loslassen ausfaden
    (NoteCircleFadeTracker).

    Die Kurve ist per Touch bearbeitbar (S2c-2a, CurveEditInteraction):
    Endpunkt-Griffe (Min/Max) ziehen, Krümmungs-Wisch im Schwarzbereich,
    Endpunkt schlägt Krümmung bei Kollision. Ab editorThresholdWidth (Dev-
    Wert, DevPanel) klappt pro Sektion eine Detailspalte auf: Sensitivity-
    Feld (Pressure/Slide, Block A2) bzw. PitchBend-Range-Multiplikator
    (Block A3) oben, Platzhalter-Text darunter, Offset-Schloss (Pressure/
    Slide) + „Color"-Zeile unten. Die frühere Schwellbreite-/Fade-Zeit-
    Bedienung sitzt seit Block A4 im floating Dev-Window (DevPanel) --
    diese View pollt die beiden GridPanelSettings-Werte in tick() nur noch
    passiv, um live zu reagieren.

    Achsen-Farben (Grid-Page v2): section.colour kommt aus GridPanelSettings
    (persistent); die „Color"-Zeile je Achse wählt per Quick-Swatch-Tap
    sofort bzw. öffnet per Gedrückthalten den ConduitColorPicker
    (CallOutBox, live). onAxisColourChanged meldet Änderungen an die
    GridPage (Ribbon-Füllfarben).

    Frame-Update über EIN juce::VBlankAttachment: liest pro Achse
    readActiveVoices in einen vorreservierten Vektor, füttert den
    NoteCircleFadeTracker, prüft den Dev-Modus-Cache und repaint()t. Kein
    readActiveVoices/Allokation im paint(). Message Thread.
*/
class MpeShapingView final : public juce::Component,
                             private juce::Timer
{
public:
    MpeShapingView (grid::GridVoiceEngine& engineToUse, GridPanelSettings& panelSettingsToUse);
    ~MpeShapingView() override;

    /** Feuert bei jeder Achsen-Farbänderung (Quick-Swatch-Tap oder
        ConduitColorPicker, live) — der Besitzer (GridPage) aktualisiert
        damit die ExpressionRibbon-Füllfarben. Persistenz (GridPanelSettings)
        übernimmt die View selbst. */
    std::function<void (grid::GridVoiceEngine::Axis, juce::Colour)> onAxisColourChanged;

    /** Feuert live beim Swipe der Sensitivity-Felder (Block A2) -- Pressure
        oder Slide, nie PitchBend. Der Besitzer (GridPage) reicht den Wert an
        GridKeyboardComponent::setPressureSensitivity/setSlideSensitivity
        durch. Laufzeit-only, keine Persistenz (Block K). */
    std::function<void (grid::GridVoiceEngine::Axis, double)> onSensitivityChanged;

    /** Feuert bei Auswahl eines PitchBend-Range-Multiplikators (Block A3,
        ¼…×8). Der Besitzer reicht den Wert an
        GridKeyboardComponent::setPitchBendMultiplier durch. */
    std::function<void (float)> onPitchBendMultiplierChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    /** „Color"-Zeile einer Achsen-Sektion (unterster Punkt der Detailspalte):
        Überschrift „Color" + 5 Quick-Swatches (16×16 px). Kurzer Tap wählt
        die Farbe sofort (onColourPicked), Gedrückthalten ~450 ms öffnet den
        ConduitColorPicker (onLongPress — der Besitzer launcht die
        CallOutBox). Hit-Zone je Swatch: volle Zeilenhöhe (Touch,
        CLAUDE.md 10 — die 16-px-Optik ist Design-Vorgabe). */
    class AxisColourRow final : public juce::Component, private juce::Timer
    {
    public:
        AxisColourRow() = default;

        void setSelectedColour (juce::Colour colour);
        [[nodiscard]] juce::Colour getSelectedColour() const noexcept { return selected; }

        std::function<void (juce::Colour)> onColourPicked;   // kurzer Tap
        std::function<void()> onLongPress;                   // Picker öffnen

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& event) override;
        void mouseUp   (const juce::MouseEvent& event) override;

        static constexpr int kHeadingGapTop    = 8;
        static constexpr int kHeadingHeight    = 12;
        static constexpr int kHeadingGapBottom = 4;
        static constexpr int kSwatchSize       = 16;
        static constexpr int kSwatchGap        = 5;
        static constexpr int kLongPressMs      = 450;
        static constexpr int kRowHeight        = kHeadingGapTop + kHeadingHeight
                                                     + kHeadingGapBottom + kSwatchSize;

    private:
        void timerCallback() override;

        [[nodiscard]] juce::Rectangle<int> swatchBounds (int index) const noexcept;
        [[nodiscard]] int swatchIndexAt (juce::Point<int> pos) const noexcept;

        juce::Colour selected;
        bool longPressTriggered = false;
        juce::Point<int> pressPosition;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AxisColourRow)
    };

    //==========================================================================
    /** PitchBend-Range-Multiplikator (Block A3, an der ehemaligen TODO-Stelle
        der PitchBend-Detailspalte): 2×3 Mini-Kacheln in der 120-px-Spalte,
        zeilenweise ¼ ½ / ×1 ×2 / ×4 ×8. Zellen unterschreiten die 44-px-
        Touch-Regel bewusst -- derselbe präzedenzierte Kompromiss wie die
        16-px-Swatches der AxisColourRow (Design-Vorgabe der 120-px-Spalte). */
    class BendRangeSelector final : public juce::Component
    {
    public:
        BendRangeSelector() = default;

        void setSelectedIndex (int index) noexcept;
        [[nodiscard]] int getSelectedIndex() const noexcept { return selectedIndex; }
        void setAccentColour (juce::Colour newColour) noexcept;

        std::function<void (float)> onMultiplierChanged;

        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& event) override;

        static constexpr std::array<float, 6> kMultipliers { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
        static constexpr int kCols = 2;
        static constexpr int kRows = 3;
        static constexpr int kRowHeight = 110;   // Ueberschrift + 3 Zeilen Kacheln

        [[nodiscard]] static juce::String labelForIndex (int index);

    private:
        [[nodiscard]] juce::Rectangle<int> cellBounds (int index) const noexcept;
        [[nodiscard]] int cellIndexAt (juce::Point<int> pos) const noexcept;

        int selectedIndex = 2;   // Default ×1
        juce::Colour accentColour = push::colours::ledCyan;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BendRangeSelector)
    };

    struct AxisSection
    {
        grid::GridVoiceEngine::Axis axis;
        juce::String label;
        juce::Colour colour;

        NoteCircleFadeTracker fadeTracker { 180 };   // Default, setFadeMs() aus der Persistenz im Ctor
        std::vector<grid::GridVoiceEngine::VoiceReadout> scratch {};   // reserve(kMaxVoices) einmalig

        // Schatten der ResponseCurve-Krümmungen (Segment 0 + 1, Block C) --
        // ResponseCurve hat keinen Getter dafür; diese View ist die einzige
        // Stelle, die setSegmentCurvature aufruft, daher bleibt der
        // Schatten konsistent. Bei der 2-Punkt-Kurve zählt nur Index 0.
        std::array<float, 2> segmentCurvature {};

        juce::Rectangle<int> tileBounds {};      // ganze Kachel (Kurvenfeld+Detailspalte), Rounded-Rect
        juce::Rectangle<int> curveBounds {};     // gesetzt in resized(), gelesen in paint()
        juce::Rectangle<int> detailBounds {};    // leer, wenn unter der Schwellbreite
    };

    /** Laufende Touch-Bearbeitung einer Kurve (Multi-Touch: eine Geste pro
        Touch-Source-Index). */
    struct EditGesture
    {
        int   sectionIndex    = -1;
        grid::CurveEditInteraction::Target target = grid::CurveEditInteraction::Target::None;
        int   segmentIndex    = 0;      // Krümmungs-Segment (Block C: links/rechts vom Mittelpunkt)
        float startNormY      = 0.0f;   // normY bei mouseDown (Curvature-Basis)
        float curvatureAtDown = 0.0f;   // section.segmentCurvature[segmentIndex] bei mouseDown
        juce::Point<float> lastFieldPos {};   // letzte Feld-Position (Zwei-Finger-Eskalation)
    };

    /** Zwei-Finger-Geste auf EINER Sektion (Block C, stufenlos --
        User-Feedback 11.07.): Drehung steuert LIVE die gegensinnige
        Bauchigkeit (zurück zur Null-Lage = Mittelpunkt verschwindet),
        Zentroid-Drag verschiebt den Mittelpunkt. Klassifikation: was
        zuerst die Schwelle reisst, gewinnt für die ganze Geste. Key der
        Map = sectionIndex (höchstens eine pro Sektion). */
    struct TwoFingerGesture
    {
        int fingerA = -1, fingerB = -1;   // Touch-Source-Indizes
        juce::Point<float> startA, startB, currentA, currentB;
        bool  rotating         = false;   // Dreh-Geste klassifiziert (live)
        bool  dragging         = false;   // Mittelpunkt-Drag klassifiziert
        float shapeAtStart     = 0.0f;    // Bauchigkeit bei Gesten-Beginn (weiterdrehen ohne Sprung)
        float restoreCurvature = 0.0f;    // 2-Punkt-Krümmung für die Rückkehr in die Null-Lage
    };

    void tick();
    void timerCallback() override;   // 3-Finger-2s-Reset (Block C)
    void paintAxis (juce::Graphics& g, const AxisSection& section,
                    grid::CurveEditInteraction::Target activeTarget) const;

    /** Anzahl aktiver Editier-Gesten (Touches) auf einer Sektion. */
    [[nodiscard]] int gestureCountInSection (int sectionIndex) const noexcept;

    /** Zwei-Finger-Verarbeitung (Block C): Drehung → Toggle 2→3 Punkte +
        Grundform (nur bei weit aufgeklapptem Panel), Zentroid-Drag →
        Mittelpunkt verschieben. */
    void processTwoFingerGesture (int sectionIndex, TwoFingerGesture& twoFinger);

    /** Verdrahtet ein "Offset"-Schloss-Toggle + graues Label für eine Achse
        (Pressure/Slide -- PitchBend bekommt noch keins, siehe resized()):
        Akzentfarbe, initialer Zustand aus der Engine, Klick schaltet
        engine.setOffsetBeyondMax(axis, ...) und die Optik. */
    void setupOffsetToggle (LockToggle& toggle, juce::Label& label,
                            grid::GridVoiceEngine::Axis axis, juce::Colour accentColour);

    /** Zentrale Farb-Anwendung einer Achse: section.colour, Quick-Swatch-
        Auswahl, LockToggle-Akzent, Persistenz (GridPanelSettings) und
        onAxisColourChanged (GridPage → Ribbons) — sofort, live. */
    void applyAxisColour (int sectionIndex, juce::Colour colour);

    /** Öffnet den ConduitColorPicker als CallOutBox über der Color-Zeile
        der Sektion, initialisiert mit der aktuellen Achsfarbe; Änderungen
        wirken live via applyAxisColour. */
    void openColourPicker (int sectionIndex);

    /** Index der Achsen-Sektion, deren curveBounds pos enthält, sonst -1. */
    [[nodiscard]] int sectionIndexAt (juce::Point<float> pos) const noexcept;

    /** Das eigentliche Kurvenfeld einer Sektion (curveBounds abzüglich
        Header-Zeile und Rand) -- dieselbe Geometrie wie beim Zeichnen, damit
        Hit-Testing und Rendering nie auseinanderlaufen. */
    [[nodiscard]] juce::Rectangle<float> curveFieldBounds (const AxisSection& section) const noexcept;

    /** Pixel-Position -> normierte Feld-Position (x: 0=links..1=rechts,
        y: 0=unten..1=oben -- CurveEditInteraction-Konvention). */
    [[nodiscard]] static juce::Point<float> normalisedPositionIn (juce::Rectangle<float> fieldBounds,
                                                                  juce::Point<float> pos) noexcept;

    /** Ein Achsen-Ausgangswert (Kurvenlinie/Endpunkt-Griffe/Noten-Kreise/
        Höhenmarke) -> Kachel-y-Pixel, geklemmt auf [lo,hi] (die sortierten
        Kurven-Ausgangsgrenzen). Gemeinsame Abbildung, damit die vier
        Zeichen-Stellen nie auseinanderlaufen (Konsistenz-Fix). */
    [[nodiscard]] static float valueToTileY (float value, float lo, float hi,
                                             juce::Rectangle<float> bounds) noexcept;

    static constexpr float kNoteCircleDiameter = 10.0f;
    static constexpr int   kCurveSamples       = 48;   // >= 48 Stützstellen
    static constexpr int   kHeaderRowHeight    = 18;
    static constexpr int   kDetailColumnWidth  = 120;
    static constexpr int   kSectionGap         = 6;    // Abstand zwischen den Achsen-Kacheln
    static constexpr float kTileCornerRadius   = 6.0f; // Looper-Kachel-Stil
    static constexpr float kMarkerWidth        = 6.0f; // Höhenmarke (combinedValue)
    static constexpr float kMarkerHeight       = 2.5f;
    static constexpr int   kOffsetToggleRowHeight = LockToggle::kComponentSize + 4; // Schloss + Rand
    static constexpr int   kColourRowBottomPad    = 4;   // Rand unter der Color-Zeile

    // Achsen-Kapazität (ExpressionAxis::Config::outMin/outMax) aller drei
    // Grid-Achsen ist seit S2c-1 immer [0,1] -- ExpressionAxis/GridVoiceEngine
    // geben das nicht nach außen (Scope dieser Nachbesserung ist ausschließlich
    // MpeShapingView), daher hier gespiegelt: die combined-Höhenmarke klemmt
    // darauf, NICHT auf die (ggf. engere) Kurven-Grenze wie Linie/Kreise.
    static constexpr float kAxisCapacityMin = 0.0f;
    static constexpr float kAxisCapacityMax = 1.0f;

    // Touch-Bearbeitung der Kurve (S2c-2a)
    static constexpr float kTouchTargetPx        = 44.0f;  // CLAUDE.md 10 Touch-Target-Regel
    static constexpr float kCurvatureSensitivity = 1.5f;   // Krümmungs-Wisch-Empfindlichkeit
    static constexpr float kEndpointRadius       = 4.0f;   // Endpunkt-Griff, Ruhezustand
    static constexpr float kEndpointRadiusActive = 6.0f;   // ... während des Ziehens

    // Mehrpunkt-Gesten (Block C)
    static constexpr float kRotateStartDegrees = 10.0f;    // Drehwinkel bis Dreh-Geste klassifiziert
    static constexpr float kMidDragStartNorm   = 0.04f;    // Zentroid-Weg bis Drag klassifiziert
    static constexpr int   kResetHoldMs        = 2000;     // 3 Finger halten → Reset

    grid::GridVoiceEngine& engine;
    GridPanelSettings& panelSettings;

    ui::DragCursorHider cursorHider;   // Cursor weg beim Kurven-Ziehen (Maus)

    std::array<AxisSection, 3> sections;

    std::map<int, EditGesture> gestures;   // Key = Touch-Source-Index (Multi-Touch)

    // Zwei-Finger-Gesten (Block C), Key = sectionIndex; plus die Sektion,
    // deren 3-Finger-Reset gerade laeuft (-1 = keiner, juce::Timer).
    std::map<int, TwoFingerGesture> twoFingerBySection;
    int resetPendingSection = -1;

    // Schloss-Toggle je Achse (unteres Ende der Detailspalte) + graues
    // Label -- eigene Member statt Teil von AxisSection (Button ist nicht
    // kopier-/verschiebbar, siehe setupOffsetToggle()). PitchBend bekommt
    // noch kein Schloss -- der Platz wird stattdessen vom bendRangeSelector
    // belegt (Block A3).
    LockToggle  pressureOffsetToggle;
    juce::Label pressureOffsetLabel { {}, "Offset" };
    LockToggle  slideOffsetToggle;
    juce::Label slideOffsetLabel { {}, "Offset" };

    // Sensitivity-Felder (Block A2, OBERSTER Detailspalten-Eintrag) --
    // Pressure/Slide, nie PitchBend. Eigene Member statt Teil von AxisSection
    // (Components sind nicht kopier-/verschiebbar). Config direkt als
    // In-Class-Initialisierer -- NumberFieldBracket ist wie jedes
    // JUCE_DECLARE_NON_COPYABLE-Widget weder kopier- noch zuweisbar.
    NumberFieldBracket pressureSensField { NumberFieldBracket::Config { 0.0, 100.0, 50.0, 1.0, 0, 0.5, "Sens" } };
    NumberFieldBracket slideSensField    { NumberFieldBracket::Config { 0.0, 100.0, 50.0, 1.0, 0, 0.5, "Sens" } };

    // PitchBend-Range-Multiplikator (Block A3) -- an der ehemaligen TODO-
    // Stelle in der PitchBend-Detailspalte.
    BendRangeSelector bendRangeSelector;

    // „Color"-Zeile je Achse (unterster Punkt der Detailspalte, ALLE drei
    // Achsen) — Index parallel zu sections. Eigene Member statt Teil von
    // AxisSection (Components sind nicht kopier-/verschiebbar).
    std::array<AxisColourRow, 3> colourRows;

    // Offene Picker-CallOutBox (höchstens eine) — SafePointer, damit der
    // Destruktor eine noch offene Box schließen kann, ohne einem bereits
    // geschlossenen Fenster hinterherzuzeigen.
    juce::Component::SafePointer<juce::CallOutBox> activeColourPicker;

    // Live-Rückweg vom DevPanel (Block A4): GridPanelSettings ist kein
    // ChangeBroadcaster -- tick() pollt beide Werte pro VBlank-Frame und
    // reagiert nur bei tatsächlicher Änderung (Layout-Neuberechnung bzw.
    // Fade-Tracker-Update sind zu teuer für jeden Frame ungeprüft).
    int cachedThresholdWidth = 0;
    int cachedFadeMs         = 0;

    double lastTickMs = 0.0;

    juce::VBlankAttachment vblank { this, [this] (double) { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpeShapingView)
};

} // namespace conduit
