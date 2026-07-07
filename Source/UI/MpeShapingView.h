#pragma once

#include <array>
#include <map>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "Core/CurveEditInteraction.h"
#include "Core/GridPanelSettings.h"
#include "Core/GridVoiceEngine.h"
#include "Core/UiSettings.h"
#include "NoteCircleFadeTracker.h"

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
    klappt pro Sektion eine Detailspalte auf (reiner Platzhalter-Text, noch
    nicht interaktiv). Zwei Dev-Slider unten (Schwellbreite, Fade-Zeit) sind
    nur im Dev-Modus sichtbar und persistieren über GridPanelSettings.

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

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
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

    static constexpr float kNoteCircleDiameter = 10.0f;
    static constexpr int   kCurveSamples       = 48;   // >= 48 Stützstellen
    static constexpr int   kHeaderRowHeight    = 18;
    static constexpr int   kDetailColumnWidth  = 120;
    static constexpr int   kDevRowHeight       = 28;
    static constexpr int   kSectionGap         = 6;    // Abstand zwischen den Achsen-Kacheln
    static constexpr float kTileCornerRadius   = 6.0f; // Looper-Kachel-Stil
    static constexpr float kMarkerWidth        = 6.0f; // Höhenmarke (combinedValue)
    static constexpr float kMarkerHeight       = 2.5f;

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

    bool devSlidersVisible = false;   // gecachter Dev-Modus-Zustand (tick())
    double lastTickMs = 0.0;

    juce::VBlankAttachment vblank { this, [this] (double) { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpeShapingView)
};

} // namespace conduit
