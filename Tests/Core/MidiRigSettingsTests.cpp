#include <catch2/catch_test_macros.hpp>

#include "Core/MidiRigSettings.h"

using conduit::MidiRigSettings;
using conduit::RigDeviceKind;

namespace
{

struct TempSettings
{
    TempSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitMidiRigSettingsTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options result;
        result.applicationName = "MidiRigSettingsTests";
        result.filenameSuffix  = ".settings";
        result.folderName      = folder.getFullPathName();
        return result;
    }

    juce::File folder;
};

} // namespace

//==============================================================================
TEST_CASE ("MidiRigSettings: leere Registry ohne gespeicherten Zustand", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };

    REQUIRE (settings.getNumDevices() == 0);
    REQUIRE (settings.indexOfId (juce::Uuid()) == -1);
}

TEST_CASE ("MidiRigSettings: addDevice/removeDevice", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };

    const auto id = settings.addDevice ("Digitakt", RigDeviceKind::soundGenerator);
    REQUIRE (settings.getNumDevices() == 1);

    const auto index = settings.indexOfId (id);
    REQUIRE (index == 0);

    const auto device = settings.getDevice (index);
    REQUIRE (device.id == id);
    REQUIRE (device.label == "Digitakt");
    REQUIRE (device.kind == RigDeviceKind::soundGenerator);
    REQUIRE (device.midiOutName.isEmpty());
    REQUIRE (device.midiInName.isEmpty());

    settings.removeDevice (id);
    REQUIRE (settings.getNumDevices() == 0);
    REQUIRE (settings.indexOfId (id) == -1);
}

TEST_CASE ("MidiRigSettings: Setter ändern nur bei echter Differenz", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };

    const auto id = settings.addDevice ("LCXL", RigDeviceKind::controller);

    settings.setLabel (id, "Launch Control XL");
    settings.setKind (id, RigDeviceKind::controller);   // unverändert, kein Crash
    settings.setMidiOutName (id, "LCXL Out");
    settings.setMidiInName (id, "LCXL In");

    const auto device = settings.getDevice (settings.indexOfId (id));
    REQUIRE (device.label == "Launch Control XL");
    REQUIRE (device.kind == RigDeviceKind::controller);
    REQUIRE (device.midiOutName == "LCXL Out");
    REQUIRE (device.midiInName == "LCXL In");

    // Setter auf unbekannte Id sind No-ops, kein Crash
    settings.setLabel (juce::Uuid(), "Ghost");
}

TEST_CASE ("MidiRigSettings: Migration der GridPanel-Namen erzeugt genau zwei Rollen-Geraete", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };

    REQUIRE_FALSE (settings.hasMigratedFromGridPanel());
    settings.migrateFromGridPanel ("LCXL In", "Conduit Out", "Conduit DAW");

    REQUIRE (settings.hasMigratedFromGridPanel());
    REQUIRE (settings.getNumDevices() == 2);

    const auto controllerIndex = settings.indexOfId (settings.getGridControllerDeviceId());
    REQUIRE (controllerIndex >= 0);
    const auto controller = settings.getDevice (controllerIndex);
    REQUIRE (controller.label == "Controller");
    REQUIRE (controller.kind == RigDeviceKind::controller);
    REQUIRE (controller.midiInName == "LCXL In");
    REQUIRE (controller.midiOutName.isEmpty());

    const auto outputIndex = settings.indexOfId (settings.getGridOutputDeviceId());
    REQUIRE (outputIndex >= 0);
    const auto output = settings.getDevice (outputIndex);
    REQUIRE (output.label == "Grid-Ausgang");
    REQUIRE (output.kind == RigDeviceKind::soundGenerator);
    REQUIRE (output.midiOutName == "Conduit Out");
    REQUIRE (output.midiInName == "Conduit DAW");

    // Flag verhindert den Zweitlauf — keine Duplikate.
    settings.migrateFromGridPanel ("LCXL In", "Conduit Out", "Conduit DAW");
    REQUIRE (settings.getNumDevices() == 2);
}

TEST_CASE ("MidiRigSettings: Migration ohne Namen ist ein No-op und setzt kein Flag", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };

    settings.migrateFromGridPanel ({}, {}, {});
    REQUIRE_FALSE (settings.hasMigratedFromGridPanel());
    REQUIRE (settings.getNumDevices() == 0);
}

TEST_CASE ("MidiRigSettings: Migrations-Flag und Rollen-Ids ueberleben den Reload", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    juce::Uuid controllerId, outputId;
    {
        MidiRigSettings settings { temp.options() };
        settings.migrateFromGridPanel ("LCXL In", "Conduit Out", {});
        controllerId = settings.getGridControllerDeviceId();
        outputId     = settings.getGridOutputDeviceId();
        settings.flush();
    }
    {
        MidiRigSettings reloaded { temp.options() };
        REQUIRE (reloaded.hasMigratedFromGridPanel());
        REQUIRE (reloaded.getGridControllerDeviceId() == controllerId);
        REQUIRE (reloaded.getGridOutputDeviceId() == outputId);
        REQUIRE (reloaded.getNumDevices() == 2);
    }
}

TEST_CASE ("MidiRigSettings: Roundtrip über Neuinstanz", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;

    juce::Uuid soundGenId, controllerId;

    {
        MidiRigSettings settings { temp.options() };
        soundGenId   = settings.addDevice ("Digitakt", RigDeviceKind::soundGenerator);
        controllerId = settings.addDevice ("LCXL", RigDeviceKind::controller);

        settings.setMidiOutName (soundGenId, "Elektron Digitakt");
        settings.setMidiOutName (controllerId, "LCXL Out");
        settings.setMidiInName (controllerId, "LCXL In");
        settings.flush();
    }

    {
        MidiRigSettings reloaded { temp.options() };
        REQUIRE (reloaded.getNumDevices() == 2);

        const auto genIndex = reloaded.indexOfId (soundGenId);
        REQUIRE (genIndex >= 0);
        const auto gen = reloaded.getDevice (genIndex);
        REQUIRE (gen.label == "Digitakt");
        REQUIRE (gen.kind == RigDeviceKind::soundGenerator);
        REQUIRE (gen.midiOutName == "Elektron Digitakt");
        REQUIRE (gen.midiInName.isEmpty());

        const auto ctrlIndex = reloaded.indexOfId (controllerId);
        REQUIRE (ctrlIndex >= 0);
        const auto ctrl = reloaded.getDevice (ctrlIndex);
        REQUIRE (ctrl.label == "LCXL");
        REQUIRE (ctrl.kind == RigDeviceKind::controller);
        REQUIRE (ctrl.midiOutName == "LCXL Out");
        REQUIRE (ctrl.midiInName == "LCXL In");
    }
}
