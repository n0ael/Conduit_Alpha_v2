#pragma once

#include <functional>
#include <map>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/CcControlModel.h"
#include "Core/FigmaSnap.h"
#include "Core/GridPanelSettings.h"
#include "Core/GridPhysics.h"
#include "DragCursorHider.h"

namespace conduit
{

//==============================================================================
/**
    Overlay des CC-Baukastens exakt über dem Pad-Raster der Grid-Page
    (Grid-Page v2, Design-Mock): zeichnet und bedient die platzierten
    CC-Controls (CcControlModel) komplett im Layer — keine Kind-Components,
    Zellgeometrie aus cols/rows der PadGridLayout-Konfiguration
    (Zelle (c,r) → c·w/cols, r·h/rows; Control-Fläche = Zell-Union,
    reduced(1)).

    CC-Modus (Dock-Panel offen + Tab „CC", setCcMode): der Layer fängt ALLE
    Events über dem Raster ab — Drag mit gewähltem Werkzeug zieht ein neues
    Control auf (gestrichelte Platzierungs-Vorschau), Drag auf einem Control
    verschiebt grid-snapped (moveTo klemmt in die Grenzen), die ×-Zone oben
    rechts entfernt. Ohne Werkzeug + freie Fläche: Events werden geschluckt
    (keine Noten im CC-Modus, wie im Mock).

    MPE-Modus (Spielen): hitTest lässt freie Flächen zum darunterliegenden
    Keyboard durch (Pads UNTER Controls bleiben stumm), Controls werden
    bedient — Fader (vertikaler Drag, unten 0), Push (an solange gehalten),
    Toggle (Tap), XY (Drag, geklemmt). Multi-Touch: pro MouseInputSource
    das gegriffene Control (JUCE liefert Touch als per-Source-Mouse-Events;
    ein setAcceptsTouchEvents braucht juce::Component nicht). Message Thread.
*/
class CcControlLayer final : public juce::Component,
                             private juce::Timer
{
public:
    CcControlLayer (grid::CcControlModel& modelToUse, int colsToUse, int rowsToUse);
    ~CcControlLayer() override { cursorHider.end(); }

    /** CC-Modus (Bearbeiten) an/aus — bricht laufende Gesten ab und löst
        gehaltene Push-Controls. */
    void setCcMode (bool shouldEdit);
    [[nodiscard]] bool isCcMode() const noexcept { return ccMode; }

    //==========================================================================
    // Map-Modus (MIDI-Rig M5b, Ableton-Analogie): Dock-Tab „Map" aktiv →
    // alle Controls werden hervorgehoben (Adress-Badge über
    // mapBadgeTextFor), Antippen meldet onMapTapControl (der Besitzer armt
    // MIDI-Learn und markiert das Control via setMapArmedControl). Der
    // Layer schluckt ALLE Events (keine Noten, kein Spielen — wie CC-Modus).

    void setMapMode (bool shouldMap);
    [[nodiscard]] bool isMapMode() const noexcept { return mapMode; }

    /** Learn-scharfes Control (Akzent-Rahmen) — -1 = keins. */
    void setMapArmedControl (int controlId);

    /** Tap auf ein Control im Map-Modus. */
    std::function<void (int controlId)> onMapTapControl;

    /** Badge-Text eines Controls (gebundene Adresse, leer = ungebunden). */
    std::function<juce::String (int controlId)> mapBadgeTextFor;

    /** Macro-Modulations-Anzeige (MIDI-Rig M5c): Effektivwert einer
        Control-Achse (Achsen-Semantik wie feedMacros, axis 1 = Y
        invertiert) — nullopt = keine aktive Modulation. Gezeichnet als
        cyaner Zweit-Marker (Fader: Linie, XY: Ring). */
    std::function<std::optional<float> (int controlId, int axis)> modulationValueFor;

    void setActiveTool (grid::CcTool tool);
    [[nodiscard]] grid::CcTool getActiveTool() const noexcept { return activeTool; }

    /** Feuert bei jeder Wertänderung eines Controls (Fader/Push/Toggle/XY) —
        seit Block E laeuft hier der Macro-Fluss an (GridPage →
        MacroBindings → MidiCcTarget/AbletonParamTarget). */
    std::function<void (const grid::CcControl&)> onControlValueChanged;

    /** Long-Press auf einem Control im Play-Modus (Block E, ~450 ms ohne
        nennenswerte Bewegung) — der Besitzer oeffnet damit die
        Macro-Ansicht des Controls. */
    std::function<void (int controlId)> onLongPressControl;

    //==========================================================================
    // Fader/XY-Physics (Block J3, Masterplan): Fader- und XY-Werte folgen
    // dem Finger über die gemeinsame Feder (grid::SpringParams — Force/
    // Mass/Inertia aus dem Dev-Panel, live gepollt); Loslassen federt mit
    // Snap-to-Default optional auf den Default-Wert zurück. Zweifarbige
    // Anzeige: das ZIEL (Finger bzw. Default) cyan, der IST-Wert (gesendet)
    // weiß. Push/Toggle bleiben unangetastet (diskrete Controls).

    /** nullptr (Default/Tests) = Physics aus, Original-Verhalten. */
    void setPanelSettings (const GridPanelSettings* settingsToPoll) noexcept
    {
        panelSettings = settingsToPoll;
    }

    /** Snap-Ziel eines Control-Typs (Default-Werte der CcControl-Felder) —
        pure, testbar. Liefert {value, x, y}. */
    [[nodiscard]] static grid::CcControl physicsDefaultsFor (grid::CcTool type) noexcept;

    /** MPE-Modus: nur Control-Flächen sind Ziel — freie Flächen fallen zum
        Keyboard durch. CC-Modus: alles wird abgefangen. */
    bool hitTest (int x, int y) override;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

private:
    struct Cell { int c = 0; int r = 0; };

    [[nodiscard]] Cell cellAt (juce::Point<float> position) const noexcept;   // geklemmt ins Raster
    [[nodiscard]] juce::Rectangle<float> rectForCells (int c0, int r0, int c1, int r1) const noexcept;
    [[nodiscard]] juce::Rectangle<float> rectFor (const grid::CcControl& control) const noexcept;
    [[nodiscard]] int controlIdAt (juce::Point<float> position) const noexcept;   // oberstes zuerst
    [[nodiscard]] static juce::Rectangle<float> removeZoneFor (juce::Rectangle<float> controlRect) noexcept;

    void drawControl (juce::Graphics& g, const grid::CcControl& control) const;
    void notifyValueChanged (const grid::CcControl& control);

    void handleEditDown (const juce::MouseEvent& event);
    void handleEditDrag (const juce::MouseEvent& event);
    void handleEditUp   (const juce::MouseEvent& event);
    void handlePlayDown (const juce::MouseEvent& event);
    void handlePlayDrag (const juce::MouseEvent& event);
    void handlePlayUp   (const juce::MouseEvent& event);

    /** Wendet die Spiel-Geste auf das Control an (Fader-value, Push/Toggle-on,
        XY-x/y) und meldet onControlValueChanged. */
    void applyPlayGesture (grid::CcControl& control, juce::Point<float> position, bool isDown);

    grid::CcControlModel& model;
    const int cols;
    const int rows;

    bool ccMode = false;
    bool mapMode = false;              // M5b: Zuweisungs-Overlay
    int  mapArmedControlId = -1;       // Learn scharf für dieses Control
    grid::CcTool activeTool = grid::CcTool::none;

    // Bearbeiten (CC-Modus): EIN Edit-Vorgang zur Zeit — weitere Finger
    // werden während eines laufenden Platzier-/Verschiebe-Drags ignoriert
    // (konservativ; Multi-Touch-Edit ist nicht Teil des Mocks).
    // Verschieben ist seit Block F FREI (FigmaSnap statt Zell-Raster).
    bool placing = false;
    Cell placeStart, placeCurrent;
    int  movingId   = -1;
    juce::Point<float> moveGrabPx;          // Greif-Offset Zeiger → Rect-Ursprung
    grid::FigmaSnap::Result activeSnap;     // Guides waehrend des Verschiebens
    int  editFinger = -1;

    static constexpr float kSnapThresholdPx = 8.0f;

    // Spielen (MPE-Modus), Multi-Touch: MouseInputSource-Index → Control-Id.
    std::map<int, int> grabbedControls;

    // Long-Press-Kandidat (Block E): ein Finger, Timer-basiert, Bewegung
    // ueber der Toleranz bricht ab (Muster AxisColourRow + Drag-Abbruch).
    void cancelLongPress();
    void timerCallback() override;

    static constexpr int   kLongPressMs             = 450;
    static constexpr float kLongPressMoveTolerancePx = 8.0f;

    int longPressFinger    = -1;
    int longPressControlId = -1;
    juce::Point<float> longPressStart;

    //==========================================================================
    // Fader/XY-Physics (Block J3): pro Control ein Feder-Zustand je Achse.
    // Der Timer der Basisklasse gehört dem Long-Press — die Physik tickt
    // über einen eigenen VBlank-Puls (Rule ui-design: Animationen via
    // VBlankAttachment).

    struct PhysicsState
    {
        grid::SpringState value, x, y;
        float targetValue = 0.0f, targetX = 0.0f, targetY = 0.0f;
        bool  grabbed = false;
    };

    [[nodiscard]] bool physicsEnabled() const noexcept
    {
        return panelSettings != nullptr && panelSettings->isControlPhysicsEnabled();
    }
    /** Feder-Zustand des Controls holen/anlegen (mit Ist-Werten geseedet). */
    PhysicsState& physicsFor (const grid::CcControl& control);
    void physicsTick();

    const GridPanelSettings* panelSettings = nullptr;
    std::map<int, PhysicsState> physicsStates;   // Control-Id → Feder-Zustand
    double lastPhysicsTickMs = 0.0;

    ui::DragCursorHider cursorHider;   // Cursor weg beim Fader-/XY-Ziehen

    // Letzter Member: tickt erst nach vollständiger Konstruktion.
    juce::VBlankAttachment physicsVBlank { this, [this] { physicsTick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CcControlLayer)
};

} // namespace conduit
