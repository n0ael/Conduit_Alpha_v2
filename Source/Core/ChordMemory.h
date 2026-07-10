#pragma once

#include <array>
#include <vector>

namespace conduit::grid
{

//==============================================================================
/** Eine gespeicherte Sonne (+ optionaler Mond) eines Akkord-Slots
    (Grid-Page v2, Akkord-Speicher).

    x/y: Sonnen-Zentrum, normalisiert über die Flächen-BREITE bzw. -HÖHE
    [0,1]. ox/oy: Mond-Offset relativ zur Sonne — BEIDE über die
    Flächen-BREITE normalisiert, damit der Orbit-Kreis beim Rescale rund
    bleibt (Design-Mock: „beide über surfW"). */
struct StoredSun
{
    float x = 0.0f;
    float y = 0.0f;
    float ox = 0.0f;
    float oy = 0.0f;
    bool hasOrbit = false;
};

//==============================================================================
/** Akkord-Speicher der Grid-Page (8 LCD-Slots, Grid-Page v2): hält pro Slot
    eine Sonnen-/Mond-Konstellation in normalisierten Flächen-Koordinaten.
    UI-frei, Message Thread, Catch2-getestet (ChordMemoryTests).

    Belegte Slots werden NIE überschrieben — Überschreiben geht nur über
    clear() + store() (Design-Mock: Löschen im CC-Modus).

    TODO(design): Persistenz der Slots (Preset/Settings) kommt später —
    aktuell leben die Akkorde nur für die Session. */
class ChordMemory
{
public:
    static constexpr int kNumSlots = 8;

    /** Speichert eine Konstellation. false bei ungültigem Slot, leerer
        Liste oder bereits belegtem Slot. */
    bool store (int slotIndex, std::vector<StoredSun> suns);

    /** Leert den Slot (ungültige Indizes: kein Effekt). */
    void clear (int slotIndex);

    [[nodiscard]] bool isOccupied (int slotIndex) const;

    /** Inhalt des Slots — leere statische Liste bei ungültigem Index. */
    [[nodiscard]] const std::vector<StoredSun>& slot (int slotIndex) const;

    [[nodiscard]] bool anyOccupied() const;

private:
    [[nodiscard]] static bool isValidSlot (int slotIndex) noexcept
    {
        return slotIndex >= 0 && slotIndex < kNumSlots;
    }

    std::array<std::vector<StoredSun>, kNumSlots> slots;   // leer = unbelegt
};

} // namespace conduit::grid
