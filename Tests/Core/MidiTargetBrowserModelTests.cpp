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

//==============================================================================
// M9c (ADR 007): Preset-Zweig "HW Presets"

namespace
{
    struct PresetRig : Rig
    {
        juce::ScopedJuceInitialiser_GUI juceRuntime;

        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ConduitBrowserPresetTests")
                                .getChildFile (juce::Uuid().toString());
        juce::File settingsFolder = folder.getChildFile ("settings");
        juce::File presetFolder = folder.getChildFile ("presets");

        conduit::MidiRigSettings settings;
        conduit::HardwarePresetLibrary presetLibrary { presetFolder };
        juce::Uuid mopho;

        PresetRig()
            : settings ([this]
                        {
                            juce::PropertiesFile::Options o;
                            o.applicationName = "BrowserPresetTests";
                            o.filenameSuffix = ".settings";
                            settingsFolder.createDirectory();
                            o.folderName = settingsFolder.getFullPathName();
                            return o;
                        }())
        {
            mopho = settings.addDevice ("Mopho", conduit::RigDeviceKind::soundGenerator);
            settings.addDevice ("AlphaTrack", conduit::RigDeviceKind::controller);

            conduit::HardwarePresetLibrary::DevicePresets presets;
            presets.deviceIdByte = 0x25;
            presets.programsPerBank = 3;
            presets.names = { "Init", "Fat Bass", "Pad", "Lead", "Keys", "Pluck" };
            presetLibrary.setPresets (mopho, std::move (presets));

            model.setPresetSources (&presetLibrary, &settings);
        }

        ~PresetRig() { folder.deleteRecursively(); }
    };
}

TEST_CASE ("BrowserModel HW-Presets: Navigation Root -> Geraet -> Bank -> Preset", "[midirig][sysex]")
{
    PresetRig rig;

    // Oben angepinnt (erste Zeile), nur Klangerzeuger erscheinen darunter.
    auto rows = rig.model.currentRows();
    REQUIRE (! rows.empty());
    CHECK (rows[0].kind == MidiTargetBrowserModel::Kind::presetRoot);
    CHECK (rows[0].label == "HW Presets");

    rig.model.enter (0);
    rows = rig.model.currentRows();
    REQUIRE (rows.size() == 1);   // nur der Mopho (AlphaTrack ist Controller)
    CHECK (rows[0].kind == MidiTargetBrowserModel::Kind::presetDevice);
    CHECK (rows[0].label == "Mopho");

    rig.model.enter (0);
    rows = rig.model.currentRows();
    REQUIRE (rows.size() == 3);   // Scan-Aktion + 2 Baenke
    CHECK (rows[0].kind == MidiTargetBrowserModel::Kind::action);
    CHECK (rows[0].deviceId == rig.mopho);
    CHECK (rows[1].label == "Bank 1");
    CHECK (rows[2].label == "Bank 2");

    // Aktion/Preset sind nicht navigierbar (enter = no-op).
    rig.model.enter (0);
    CHECK (rig.model.currentRows().size() == 3);

    rig.model.enter (2);   // Bank 2
    rows = rig.model.currentRows();
    REQUIRE (rows.size() == 3);
    CHECK (rows[0].kind == MidiTargetBrowserModel::Kind::preset);
    CHECK (rows[0].label == "1: Lead");
    CHECK (rows[0].deviceId == rig.mopho);
    CHECK (rows[0].bank == 1);
    CHECK (rows[0].program == 0);
    CHECK (rows[2].displayName == "Pluck");

    CHECK (rig.model.breadcrumbText().contains ("Bank 2"));
    CHECK (rig.model.goBack());
}

TEST_CASE ("BrowserModel HW-Presets: Filter durchsucht Preset-Namen, Scan-Status ersetzt das Aktions-Label", "[midirig][sysex]")
{
    PresetRig rig;

    rig.model.enter (0);   // HW Presets
    rig.model.setFilter ("bass");
    auto rows = rig.model.currentRows();
    REQUIRE (rows.size() == 1);
    CHECK (rows[0].kind == MidiTargetBrowserModel::Kind::preset);
    CHECK (rows[0].program == 1);
    CHECK (rows[0].label.contains ("Fat Bass"));

    // Scan-Status-Hook: Aktions-Zeile zeigt den Fortschritt.
    rig.model.setFilter ({});
    rig.model.scanStatusFor = [] (const juce::Uuid&) { return juce::String ("Scanne... 42/384"); };
    rig.model.enter (0);   // Mopho
    rows = rig.model.currentRows();
    REQUIRE (! rows.empty());
    CHECK (rows[0].label == "Scanne... 42/384");
}

TEST_CASE ("BrowserModel HW-Presets: ohne Quellen kein Zweig, ohne Cache nur die Scan-Aktion", "[midirig][sysex]")
{
    Rig plain;
    const auto rows = plain.model.currentRows();
    for (const auto& row : rows)
        CHECK (row.kind != MidiTargetBrowserModel::Kind::presetRoot);

    PresetRig rig;
    rig.presetLibrary.clearPresets (rig.mopho);
    rig.model.enter (0);   // HW Presets
    rig.model.enter (0);   // Mopho
    const auto deviceRows = rig.model.currentRows();
    REQUIRE (deviceRows.size() == 1);
    CHECK (deviceRows[0].kind == MidiTargetBrowserModel::Kind::action);
    CHECK (deviceRows[0].label.contains ("scannen"));
}
