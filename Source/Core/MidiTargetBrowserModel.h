#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "HardwareCcDatabase.h"
#include "HardwarePresetLibrary.h"
#include "MidiProfileLibrary.h"
#include "MidiRigSettings.h"

namespace conduit
{

//==============================================================================
/**
    Headless Drill-down-Modell fuer den Hardware-Ziel-Picker (MIDI-Rig M3,
    ADR 006 — Analogie Ableton-Parameter-Browser Track -> Device ->
    Parameter). Quellen: `HardwareCcDatabase` (Klartext-Schnellpfad E1b,
    Geraete OHNE Hersteller-Feld, erscheinen ungruppiert auf der obersten
    Ebene) + `MidiProfileLibrary` (CSV-Profile, gruppiert nach
    `manufacturer`; die `section`-Spalte wird zu einer eigenen Ebene, wenn
    ein Geraet ueberhaupt Sections hat -- sonst direkt Parameter-Liste).

    Navigation ist ein einfacher Pfad-Stack (`enter`/`goBack`); `setFilter`
    durchsucht rekursiv ALLE Parameter unterhalb der aktuellen Ebene
    (Substring, case-insensitiv) und liefert eine flache Treffer-Liste --
    `enter()` ignoriert dabei den Filter und arbeitet immer auf der
    ungefilterten Ebene (Filter wird beim Navigieren zurueckgesetzt).
    Pure, UI-frei, Catch2-testbar. Message Thread (wie die Quell-Objekte).
*/
class MidiTargetBrowserModel
{
public:
    enum class Kind { manufacturer, device, section, parameter,
                      presetRoot, presetDevice, presetBank, preset, action };

    struct Row
    {
        Kind kind = Kind::device;
        juce::String label;
        int indent = 0;

        // Nur bei kind == parameter belegt (Auswahl-Payload):
        bool isCc = false;          // true = CC-Ziel, false = NRPN-Ziel
        int number = -1;            // CC-Nummer bzw. NRPN-Adresse (msb*128+lsb)
        int minValue = 0;
        int maxValue = 127;         // nur fuer NRPN relevant
        juce::String displayName;   // "Geraet: Param" -- fuer MidiNrpnTarget-Anzeigename

        // Nur im Preset-Zweig belegt (M9c, ADR 007):
        juce::Uuid deviceId;        // RigDevice (presetDevice/preset/action)
        int bank = -1;              // preset: Bank-Index 0..2
        int program = -1;           // preset: Programm 0..127
    };

    MidiTargetBrowserModel (const grid::HardwareCcDatabase& hardwareDbToUse,
                            const MidiProfileLibrary& profileLibraryToUse);

    /** M9c: Preset-Zweig „HW Presets" aktivieren (Geraete = RigDevices
        kind == soundGenerator; Namen aus der Preset-Library). Optional —
        ohne Aufruf verhaelt sich das Modell wie vor M9. */
    void setPresetSources (const HardwarePresetLibrary* presetLibraryToUse,
                           const MidiRigSettings* rigSettingsToUse);

    /** M9c: liefert einen Status-Text fuer die Scan-Aktions-Zeile eines
        Geraets (leer = kein Scan aktiv, Zeile zeigt „Presets scannen…"). */
    std::function<juce::String (const juce::Uuid&)> scanStatusFor;

    /** Zeilen der aktuellen Ebene -- bei aktivem Filter eine flache,
        rekursiv gesammelte Trefferliste aus Parameter-Zeilen (kein
        `enter()` darauf sinnvoll). */
    [[nodiscard]] std::vector<Row> currentRows() const;

    /** In eine navigierbare Zeile (manufacturer/device/section) der
        UNGEFILTERTEN aktuellen Ebene hineingehen; Index bezieht sich auf
        `currentRows()` OHNE aktiven Filter. Setzt den Filter zurueck.
        Kein Effekt bei ungueltigem Index oder kind == parameter. */
    void enter (int rowIndex);

    /** Eine Ebene zurueck; false wenn bereits auf der obersten Ebene.
        Setzt den Filter zurueck. */
    bool goBack();

    [[nodiscard]] bool isAtTop() const noexcept { return path.empty(); }

    /** "Elektron \xe2\x96\xb8 Analog Heat \xe2\x96\xb8 Filter" -- leer auf der obersten Ebene. */
    [[nodiscard]] juce::String breadcrumbText() const;

    /** Substring-Filter (case-insensitiv) auf Parameter-Label/Anzeigename,
        wirkt auf `currentRows()` bis zum naechsten `enter()`/`goBack()`/
        `setFilter({})`. */
    void setFilter (const juce::String& text);
    [[nodiscard]] const juce::String& filter() const noexcept { return filterText; }

private:
    struct PathEntry
    {
        Kind kind = Kind::device;
        juce::String label;
        juce::String stringKey;      // manufacturer- bzw. section-Name
        int intKey = -1;             // legacy device index bzw. profile index
        bool isLegacyDevice = false; // nur bei kind == device
    };

    /** Eine Zeile der aktuellen Ebene MIT ihrer Navigations-Identitaet --
        ein Paar, damit `currentRows()`/`enter()` garantiert dieselbe
        Reihenfolge/Zuordnung sehen (nie zwei getrennte Aufbau-Pfade, die
        auseinanderlaufen koennten). */
    struct LevelEntry { Row row; PathEntry entry; };

    [[nodiscard]] std::vector<LevelEntry> buildCurrentLevel() const;
    [[nodiscard]] std::vector<Row> parameterRowsForLegacyDevice (int deviceIndex) const;
    [[nodiscard]] std::vector<Row> parameterRowsForProfileSection (int profileIndex,
                                                                    const juce::String& section,
                                                                    bool matchEmptySection) const;
    [[nodiscard]] std::vector<juce::String> distinctSectionsForProfile (int profileIndex) const;
    [[nodiscard]] std::vector<Row> allParameterRowsForProfile (int profileIndex) const;
    [[nodiscard]] std::vector<Row> allParameterRowsForManufacturer (const juce::String& manufacturer) const;
    [[nodiscard]] std::vector<Row> allParameterRowsUnderCurrentPath() const;
    [[nodiscard]] static std::vector<Row> filterRows (std::vector<Row> rows, const juce::String& filterText);

    // Preset-Zweig (M9c) — nur aktiv, wenn beide Quellen gesetzt sind.
    [[nodiscard]] bool presetsEnabled() const noexcept
    {
        return presetLibrary != nullptr && rigSettings != nullptr;
    }
    [[nodiscard]] std::vector<LevelEntry> buildPresetLevel (const PathEntry& top) const;
    [[nodiscard]] std::vector<Row> presetRowsForDeviceBank (const juce::Uuid& deviceId,
                                                            int bank) const;
    [[nodiscard]] std::vector<Row> allPresetRowsUnderCurrentPath() const;

    const grid::HardwareCcDatabase& hardwareDb;
    const MidiProfileLibrary& profileLibrary;
    const HardwarePresetLibrary* presetLibrary = nullptr;
    const MidiRigSettings* rigSettings = nullptr;

    std::vector<PathEntry> path;
    juce::String filterText;
};

} // namespace conduit
