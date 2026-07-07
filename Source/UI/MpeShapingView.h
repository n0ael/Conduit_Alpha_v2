#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

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

private:
    struct AxisSection
    {
        grid::GridVoiceEngine::Axis axis;
        juce::String label;
        juce::Colour colour;

        NoteCircleFadeTracker fadeTracker { 180 };   // Default, setFadeMs() aus der Persistenz im Ctor
        std::vector<grid::GridVoiceEngine::VoiceReadout> scratch {};   // reserve(kMaxVoices) einmalig

        juce::Rectangle<int> curveBounds {};    // gesetzt in resized(), gelesen in paint()
        juce::Rectangle<int> detailBounds {};   // leer, wenn unter der Schwellbreite
    };

    void tick();
    void paintAxis (juce::Graphics& g, const AxisSection& section) const;
    void updateDevSliderVisibility (bool devModeEnabled);

    static constexpr float kNoteCircleDiameter = 10.0f;
    static constexpr int   kCurveSamples       = 48;   // >= 48 Stützstellen
    static constexpr int   kHeaderRowHeight    = 18;
    static constexpr int   kDetailColumnWidth  = 120;
    static constexpr int   kDevRowHeight       = 28;

    grid::GridVoiceEngine& engine;
    GridPanelSettings& panelSettings;
    UiSettings& uiSettings;

    std::array<AxisSection, 3> sections;

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
