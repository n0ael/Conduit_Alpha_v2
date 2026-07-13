#include <catch2/catch_test_macros.hpp>

#include "Core/HardwareCcDatabase.h"
#include "Core/MidiProfileLibrary.h"
#include "Core/MidiTargetBrowserModel.h"

using conduit::MidiProfileLibrary;
using conduit::MidiTargetBrowserModel;

namespace
{
    // Faktor-Daten (BinaryData) reichen fuer diese Tests -- kein User-Ordner
    // noetig. Bewusst gegen ECHTE Profile getestet (Muster
    // MidiProfileLibraryTests.cpp): Analog Heat = NRPN ohne Sections,
    // Digitakt (CSV-Profil) = NRPN MIT Section "Track parameters",
    // "Elektron Digitakt" (Klartext-DB-Geraet, andere Quelle!) = CC-only.
    struct Rig
    {
        conduit::grid::HardwareCcDatabase hardwareDb;
        MidiProfileLibrary profileLibrary;   // Faktor-CSVs, kein User-Ordner
        MidiTargetBrowserModel model { hardwareDb, profileLibrary };

        Rig() { hardwareDb.load(); }
    };

    int findRowIndex (const std::vector<MidiTargetBrowserModel::Row>& rows, const juce::String& label)
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].label == label)
                return i;
        return -1;
    }
}

//==============================================================================
TEST_CASE ("MidiTargetBrowserModel: oberste Ebene mischt Klartext-Geraete und CSV-Hersteller", "[midirig]")
{
    Rig rig;
    const auto rows = rig.model.currentRows();

    REQUIRE (rig.model.isAtTop());
    REQUIRE_FALSE (rows.empty());

    // Klartext-DB-Geraet "Elektron Digitakt" ungruppiert direkt auf der
    // obersten Ebene (kein Hersteller-Feld in der Klartext-DB).
    const auto legacyIndex = findRowIndex (rows, "Elektron Digitakt");
    REQUIRE (legacyIndex >= 0);
    CHECK (rows[(size_t) legacyIndex].kind == MidiTargetBrowserModel::Kind::device);

    // CSV-Hersteller "Elektron" erscheint EINMAL (Analog Heat + Digitakt
    // gruppiert), nicht pro Geraet.
    int elektronCount = 0;
    for (const auto& row : rows)
        if (row.label == "Elektron")
            ++elektronCount;
    REQUIRE (elektronCount == 1);
    CHECK (rows[(size_t) findRowIndex (rows, "Elektron")].kind == MidiTargetBrowserModel::Kind::manufacturer);

    // Alphabetisch sortiert.
    for (size_t i = 1; i < rows.size(); ++i)
        CHECK (rows[i - 1].label.compareIgnoreCase (rows[i].label) <= 0);
}

TEST_CASE ("MidiTargetBrowserModel: Klartext-Geraet fuehrt direkt zu Parametern (CC)", "[midirig]")
{
    Rig rig;
    const auto top = rig.model.currentRows();
    const auto legacyIndex = findRowIndex (top, "Elektron Digitakt");
    REQUIRE (legacyIndex >= 0);

    rig.model.enter (legacyIndex);
    REQUIRE_FALSE (rig.model.isAtTop());
    CHECK (rig.model.breadcrumbText() == "Elektron Digitakt");

    const auto params = rig.model.currentRows();
    REQUIRE_FALSE (params.empty());

    const auto volumeIndex = findRowIndex (params, "Amp Volume (CC 7)");
    REQUIRE (volumeIndex >= 0);
    const auto& row = params[(size_t) volumeIndex];
    CHECK (row.kind == MidiTargetBrowserModel::Kind::parameter);
    CHECK (row.isCc);
    CHECK (row.number == 7);
    CHECK (row.displayName == "Elektron Digitakt: Amp Volume");

    REQUIRE (rig.model.goBack());
    CHECK (rig.model.isAtTop());
    REQUIRE_FALSE (rig.model.goBack());   // schon oben -- kein Effekt
}

TEST_CASE ("MidiTargetBrowserModel: Hersteller -> Geraet -> (keine Section) -> Parameter, NRPN", "[midirig]")
{
    Rig rig;
    const auto top = rig.model.currentRows();
    rig.model.enter (findRowIndex (top, "Elektron"));

    const auto devices = rig.model.currentRows();
    const auto heatIndex = findRowIndex (devices, "Analog Heat");
    REQUIRE (heatIndex >= 0);
    CHECK (devices[(size_t) heatIndex].kind == MidiTargetBrowserModel::Kind::device);

    rig.model.enter (heatIndex);
    CHECK (rig.model.breadcrumbText() == "Elektron " + juce::String::fromUTF8 ("\xe2\x96\xb8") + " Analog Heat");

    // Analog Heat hat KEINE Sections -- direkt Parameter, keine section-Zeilen.
    const auto params = rig.model.currentRows();
    for (const auto& row : params)
        CHECK (row.kind == MidiTargetBrowserModel::Kind::parameter);

    const auto driveIndex = findRowIndex (params, "Drive (NRPN 12)");
    REQUIRE (driveIndex >= 0);
    const auto& drive = params[(size_t) driveIndex];
    CHECK_FALSE (drive.isCc);
    CHECK (drive.number == 12);
    CHECK (drive.minValue == 0);
    CHECK (drive.maxValue == 16383);
    CHECK (drive.displayName == "Analog Heat: Drive");
}

TEST_CASE ("MidiTargetBrowserModel: Geraet MIT Section-Ebene (Digitakt-Profil)", "[midirig]")
{
    Rig rig;
    rig.model.enter (findRowIndex (rig.model.currentRows(), "Elektron"));
    const auto devices = rig.model.currentRows();
    const auto digitaktIndex = findRowIndex (devices, "Digitakt");
    REQUIRE (digitaktIndex >= 0);

    rig.model.enter (digitaktIndex);
    const auto sectionRows = rig.model.currentRows();
    REQUIRE_FALSE (sectionRows.empty());

    const auto sectionIndex = findRowIndex (sectionRows, "Track parameters");
    REQUIRE (sectionIndex >= 0);
    CHECK (sectionRows[(size_t) sectionIndex].kind == MidiTargetBrowserModel::Kind::section);

    rig.model.enter (sectionIndex);
    const auto params = rig.model.currentRows();
    REQUIRE_FALSE (params.empty());
    for (const auto& row : params)
        CHECK (row.kind == MidiTargetBrowserModel::Kind::parameter);

    CHECK (rig.model.breadcrumbText()
           == "Elektron " + juce::String::fromUTF8 ("\xe2\x96\xb8") + " Digitakt "
                  + juce::String::fromUTF8 ("\xe2\x96\xb8") + " Track parameters");
}

TEST_CASE ("MidiTargetBrowserModel: enter() auf Parameter-Zeile ist ein No-Op", "[midirig]")
{
    Rig rig;
    rig.model.enter (findRowIndex (rig.model.currentRows(), "Elektron Digitakt"));
    const auto params = rig.model.currentRows();
    const auto paramIndex = findRowIndex (params, "Amp Volume (CC 7)");
    REQUIRE (paramIndex >= 0);

    rig.model.enter (paramIndex);   // parameter -- nicht navigierbar
    CHECK (rig.model.currentRows().size() == params.size());   // unveraendert
}

TEST_CASE ("MidiTargetBrowserModel: setFilter durchsucht rekursiv unter der aktuellen Ebene", "[midirig]")
{
    Rig rig;

    // Von der obersten Ebene aus: findet "Drive" tief unter Elektron -> Analog Heat.
    rig.model.setFilter ("Drive");
    const auto hits = rig.model.currentRows();
    REQUIRE_FALSE (hits.empty());

    bool foundDrive = false;
    for (const auto& row : hits)
    {
        CHECK (row.kind == MidiTargetBrowserModel::Kind::parameter);
        CHECK (row.label.containsIgnoreCase ("Drive"));
        if (row.displayName == "Analog Heat: Drive")
            foundDrive = true;
    }
    REQUIRE (foundDrive);

    // Leerer Filter setzt auf die normale (ungefilterte) Ansicht zurueck.
    rig.model.setFilter ({});
    CHECK (rig.model.currentRows().size() > hits.size());

    // Kein Treffer -> leere Liste, kein Crash.
    rig.model.setFilter ("xyz_kein_treffer_garantiert");
    CHECK (rig.model.currentRows().empty());

    // Navigation setzt den Filter zurueck.
    rig.model.setFilter ("Drive");
    rig.model.enter (0);
    CHECK (rig.model.filter().isEmpty());
}
