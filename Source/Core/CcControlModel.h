#pragma once

#include <vector>

namespace conduit::grid
{

//==============================================================================
/** Werkzeuge des CC-Baukastens der Grid-Page (Grid-Page v2, Design-Mock):
    none = kein Werkzeug gewählt (CC-Modus schluckt dann nur Events). */
enum class CcTool { none, fader, push, toggle, xy };

//==============================================================================
/**
    Ein platziertes CC-Control auf dem Pad-Raster: belegt das Zell-Rechteck
    [c0..c1] × [r0..r1] (inklusive, normalisiert c0 <= c1, r0 <= r1) und
    hält seinen reinen UI-Zustand (Fader-value, Push/Toggle-on, XY-x/y).
*/
struct CcControl
{
    int id = 0;
    CcTool type = CcTool::none;
    int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
    float value = 0.75f;    // Fader: 0 = unten, 1 = oben
    bool  on    = false;    // Push (solange gehalten) / Toggle
    float x = 0.5f, y = 0.5f;   // XY-Pad, je [0,1] (y: 0 = oben)

    // Freie Anordnung (Block F, DIY-Tab): normalisierter Rect [0,1] ueber
    // der Layer-Flaeche. rw <= 0 = noch nicht frei bewegt, Position/Groesse
    // kommen aus den Zellen oben (Aufziehen bleibt Raster-basiert, nur das
    // VERSCHIEBEN ist frei -- FigmaSnap). Der Layer initialisiert den Rect
    // beim ersten Verschieben aus der aktuellen Zell-Geometrie.
    float rx = 0.0f, ry = 0.0f, rw = 0.0f, rh = 0.0f;
};

//==============================================================================
/**
    Datenhaltung + Geometrie-Logik der platzierten CC-Controls — UI-frei und
    Catch2-getestet (Tests/Core/CcControlModelTests.cpp). Der CcControlLayer
    (Source/UI) zeichnet und bedient die Controls, das Modell kennt nur
    Zellen. Message Thread (UI-Zustand, kein Audio-Pfad).
*/
class CcControlModel
{
public:
    /** Platziert ein Control über dem Zell-Rechteck (Ecken werden
        min/max-normalisiert) und liefert seine id (> 0, laufend). */
    int addControl (CcTool type, int c0, int r0, int c1, int r1);

    /** Entfernt das Control mit dieser id (kein Effekt bei unbekannter id). */
    void remove (int id) noexcept;

    /** Entfernt ALLE Controls und setzt die Id-Vergabe zurück — sauberer
        Zustand beim Moduswechsel der System-Controls (Grid-Page-Layout-Modi,
        User 10.07.2026). */
    void clear() noexcept;

    /** Stellt ein persistiertes Control MIT seiner gespeicherten Id wieder
        her (Block K — die Macro-/MIDI-In-Bindings referenzieren die Id;
        addControl wuerde nach remove()-Luecken andere Ids vergeben).
        Die Id-Vergabe zaehlt danach oberhalb der hoechsten Id weiter. */
    void restore (const CcControl& control);

    /** Verschiebt das Control mit erhaltener Größe an die neue Ursprungszelle,
        geklemmt in die Rastergrenzen [0,cols) × [0,rows). true, wenn die id
        existiert (auch wenn die geklemmte Position unverändert bleibt). */
    bool moveTo (int id, int c0, int r0, int cols, int rows) noexcept;

    [[nodiscard]] CcControl* find (int id) noexcept;

    [[nodiscard]] const std::vector<CcControl>& controls() const noexcept { return items; }

    /** Id des OBERSTEN Controls an dieser Zelle (zuletzt platziert liegt
        oben), -1 wenn die Zelle frei ist. */
    [[nodiscard]] int controlAt (int cellC, int cellR) const noexcept;

    /** true, wenn das Zell-Rechteck des Controls diese Zelle einschließt. */
    [[nodiscard]] static bool coversCell (const CcControl& control,
                                          int cellC, int cellR) noexcept;

private:
    std::vector<CcControl> items;
    int nextId = 1;
};

//==============================================================================
/** Feste System-Bestückung des XY+Fader-Modus der Grid-Page (User
    10.07.2026) über einem 8×2-Zellraster (die oberen zwei Pad-Reihen):
    EIN XY-Pad über den Zellen (0,0)-(1,1) plus 6 vertikale Fader je
    (c,0)-(c,1) für c = 2..7 — alle 16 Zellen abgedeckt. Fügt nur hinzu,
    der Aufrufer leert das Modell vorher (clear()).
    TODO(design): Anzahl/Anordnung + CC-Zuweisung final vom User. */
void buildXyFaderLayout (CcControlModel& model);

} // namespace conduit::grid
