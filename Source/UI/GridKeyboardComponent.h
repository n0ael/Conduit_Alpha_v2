#pragma once

#include <map>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ChordMemory.h"
#include "Core/GridVoiceEngine.h"
#include "Core/PadGridLayout.h"
#include "Core/RingTouchModel.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

//==============================================================================
/**
    Touch-Fläche des Grid-Voice-Modells (M1 Teil 3, CLAUDE.md 14 ADR
    Grid-Page): isomorphes Pad-Raster (PadGridLayout), Multi-Touch über die
    Standard-Maus-/Touch-Callbacks — jeder JUCE-Touch-Source liefert ein
    eigenes event.source.

    Note + Pitch-Bend über X + Pressure über Y (M1 Teil 3) plus Ring-
    Mechanik als zweite Ausdrucksachse (M1b-2, "Sonne/Mond/Orbit"): ein
    zweiter Finger im Greifband eines liegenden primären Fingers (Sonne)
    wird dessen Ring-Finger (Mond), sein Abstand zur Sonne (Orbit-Radius)
    moduliert setSlide des primären Fingers (RingTouchModel).
    Bewusst weiterhin ohne Drone/Latch, Pinch, Doppeltipp, Drift-über-Rand,
    Rand-Ribbons, Release-All — eigene Meilensteine.

    Hält keinen eigenen Zustand außer der Per-Finger-Zuordnung (primär),
    der Session-Skala (setScale) und dem RingTouchModel; ruft die
    GridVoiceEngine& direkt (Message Thread, CLAUDE.md 4.2 ITouchMacro).
*/
class GridKeyboardComponent final : public juce::Component
{
public:
    explicit GridKeyboardComponent (grid::GridVoiceEngine& engineToUse,
                                     const grid::PadGridLayout::Config& layoutConfig = {});

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

    /** Session-Skala (Root-Tree scaleRoot/scaleType, Schema 6.2) — färbt die
        Pad-Grundfarben (Grundton/Skalenton/skalenfremd). Message Thread. */
    void setScale (int newRootNote, ScaleType newScaleType);

    /** Pad-Grundfarbe nach Session-Skala (Design-Mock Grid-Page v2):
        Grundton padRoot, Skalenton tile, skalenfremd padUnlit — pure
        function, testbar ohne Component. */
    [[nodiscard]] static juce::Colour padBaseColour (int midiNote, int rootNote,
                                                     ScaleType type) noexcept;

    //==========================================================================
    // Akkord-Speicher (Grid-Page v2, Feature 6): eine abgerufene
    // Konstellation liegt "latched" auf dem Grid — ohne physische Finger —
    // und wird wie Sonnen/Monde gerendert (inkl. Pad-Glow). Message Thread.

    /** Ruft eine gespeicherte Konstellation ab: beendet zuerst den evtl.
        liegenden Akkord (clearLatched), dann pro Sonne noteOn +
        Startwerte (Bend 0, Pressure neutral, Slide aus dem Mond-Offset).
        Sonnen außerhalb des Rasters werden nur visuell gelatched. */
    void latchConstellation (const std::vector<grid::StoredSun>& suns);

    /** Verschiebt die latched Konstellation starr um das Pixel-Delta —
        KEIN Clamping (Sonnen dürfen über den Rand, nur visuell geclippt).
        X-Bewegung = Pitch-Bend, Y = Ausdruck — exakt wie ein Finger-Drag;
        der Mond-Offset (Slide) bleibt starr. */
    void moveLatchedBy (float dxPx, float dyPx);

    /** Beendet den latched Akkord (noteOff, releaseVelocity 0) und
        entfernt die Konstellation. */
    void clearLatched();

    /** Aktuelle Konstellation = live Sonnen (RingTouchModel) PLUS latched —
        normalisiert (x über Breite, y über Höhe, ox/oy BEIDE über die
        Breite, ChordMemory-Konvention). */
    [[nodiscard]] std::vector<grid::StoredSun> constellationNormalized() const;

private:
    struct FingerState
    {
        float startNormX = 0.0f;
        float startNormY = 0.0f;
    };

    /** Latched Sonne (Akkord-Abruf): Pixel-Positionen wie die Live-Kreise,
        note = -1, wenn die Sonne außerhalb des Rasters lag (kein noteOn). */
    struct LatchedSun
    {
        juce::Point<float> centre;
        juce::Point<float> orbitOffset;
        bool hasOrbit = false;
        uint32_t fingerId = 0;
        float startNormX = 0.0f;
        float startNormY = 0.0f;
        int note = -1;
    };

    // Synthetische fingerIds der latched Sonnen — kollidiert nie mit
    // Touch-Ids (= sourceIndex + 1, kleine Werte).
    static constexpr uint32_t kLatchedFingerBase = 0x10000u;

    [[nodiscard]] juce::Point<float> normalisedPosition (const juce::MouseEvent& event) const noexcept;
    [[nodiscard]] static int fingerIdFor (const juce::MouseEvent& event) noexcept;

    grid::GridVoiceEngine& engine;
    grid::PadGridLayout    layout;
    grid::RingTouchModel   ring;

    int       scaleRootNote = 0;
    ScaleType sessionScale  = ScaleType::chromatic;

    std::map<int, FingerState> fingers;
    std::vector<LatchedSun> latched;   // Akkord-Speicher-Abruf (leer = keiner)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridKeyboardComponent)
};

} // namespace conduit
