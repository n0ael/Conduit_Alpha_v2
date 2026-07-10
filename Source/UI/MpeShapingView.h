#pragma once

#include <array>
#include <functional>
#include <map>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "Core/CurveEditInteraction.h"
#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/UiSettings.h"
#include "LockToggle.h"
#include "NoteCircleFadeTracker.h"
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

    Rein Anzeige -- keine Touch-Bearbeitung der Kurve (Punkte/Krümmung/
    Min-Max-Griffe kommen erst mit S2c-2). Ab editorThresholdWidth (Dev-Wert)
    klappt pro Sektion eine Detailspalte auf (Platzhalter-Text oben,
    Offset-Schloss + „Color"-Zeile unten). Zwei Dev-Slider unten
    (Schwellbreite, Fade-Zeit) sind nur im Dev-Modus sichtbar und
    persistieren über GridPanelSettings.

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
class MpeShapingView final : public juce::Component
{
public:
    MpeShapingView (grid::GridVoiceEngine& engineToUse, GridPanelSettings& panelSettingsToUse,
                    UiSettings& uiSettingsToUse);
    ~MpeShapingView() override;

    /** Feuert bei jeder Achsen-Farbänderung (Quick-Swatch-Tap oder
        ConduitColorPicker, live) — der Besitzer (GridPage) aktualisiert
        damit die ExpressionRibbon-Füllfarben. Persistenz (GridPanelSettings)
        übernimmt die View selbst. */
    std::function<void (grid::GridVoiceEngine::Axis, juce::Colour)> onAxisColourChanged;

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

    struct AxisSection
    {
        grid::GridVoiceEngine::Axis axis;
        juce::String label;
        juce::Colour colour;

        NoteCircleFadeTracker fadeTracker { 180 };   // Default, setFadeMs() aus der Persistenz im Ctor
        std::vector<grid::GridVoiceEngine::VoiceReadout> scratch {};   // reserve(kMaxVoices) einmalig

        // Schatten der ResponseCurve-Krümmung (Segment 0) -- ResponseCurve
        // hat keinen Getter dafür; diese View ist die einzige Stelle, die
        // setSegmentCurvature aufruft, daher bleibt der Schatten konsistent.
        float segmentCurvature = 0.0f;

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
        float startNormY      = 0.0f;   // normY bei mouseDown (Curvature-Basis)
        float curvatureAtDown = 0.0f;   // section.segmentCurvature bei mouseDown
    };

    void tick();
    void paintAxis (juce::Graphics& g, const AxisSection& section,
                    grid::CurveEditInteraction::Target activeTarget) const;
    void updateDevSliderVisibility (bool devModeEnabled);

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
    static constexpr int   kDevRowHeight       = 28;
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

    grid::GridVoiceEngine& engine;
    GridPanelSettings& panelSettings;
    UiSettings& uiSettings;

    std::array<AxisSection, 3> sections;

    std::map<int, EditGesture> gestures;   // Key = Touch-Source-Index (Multi-Touch)

    juce::Label  thresholdCaption { {}, "Schwellbreite" };
    juce::Slider thresholdSlider;
    juce::Label  fadeCaption { {}, "Fade-Zeit" };
    juce::Slider fadeSlider;

    // Schloss-Toggle je Achse (unteres Ende der Detailspalte) + graues
    // Label -- eigene Member statt Teil von AxisSection (Button ist nicht
    // kopier-/verschiebbar, siehe setupOffsetToggle()). PitchBend bekommt
    // noch kein Schloss -- Platz bleibt frei fürs künftige Range-Element.
    LockToggle  pressureOffsetToggle;
    juce::Label pressureOffsetLabel { {}, "Offset" };
    LockToggle  slideOffsetToggle;
    juce::Label slideOffsetLabel { {}, "Offset" };

    // „Color"-Zeile je Achse (unterster Punkt der Detailspalte, ALLE drei
    // Achsen) — Index parallel zu sections. Eigene Member statt Teil von
    // AxisSection (Components sind nicht kopier-/verschiebbar).
    std::array<AxisColourRow, 3> colourRows;

    // Offene Picker-CallOutBox (höchstens eine) — SafePointer, damit der
    // Destruktor eine noch offene Box schließen kann, ohne einem bereits
    // geschlossenen Fenster hinterherzuzeigen.
    juce::Component::SafePointer<juce::CallOutBox> activeColourPicker;

    bool devSlidersVisible = false;   // gecachter Dev-Modus-Zustand (tick())
    double lastTickMs = 0.0;

    juce::VBlankAttachment vblank { this, [this] (double) { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpeShapingView)
};

} // namespace conduit
