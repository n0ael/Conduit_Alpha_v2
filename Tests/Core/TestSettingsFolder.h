#pragma once

#include <juce_core/juce_core.h>

namespace conduit::test
{

/** Frisches Temp-Verzeichnis für die Settings-Persistenz eines
    EngineProcessor-Tests — VOR dem Processor deklarieren und dessen
    settingsFolder-Konstruktor übergeben:

        conduit::test::ScopedSettingsFolder settingsFolder;
        conduit::EngineProcessor engine { settingsFolder.folder };

    Damit schreiben Transport-/Capture-/Meter-/ChannelNames-/UI-/OscSend-/
    ModuleUiDefaults-Settings NIE in die echten AppData-Dateien des Users
    (Vorfall 07/2026: ein Testlauf persistierte metronome=1 in die echte
    Transport.settings). Der Dtor räumt das Verzeichnis weg — er läuft nach
    dem Processor-Dtor (Deklarationsreihenfolge), die saveIfNeeded()-Writes
    der Settings landen also noch im Ordner und verschwinden mit ihm. */
struct ScopedSettingsFolder
{
    ScopedSettingsFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitEngineTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~ScopedSettingsFolder()
    {
        folder.deleteRecursively();
    }

    juce::File folder;

    JUCE_DECLARE_NON_COPYABLE (ScopedSettingsFolder)
};

} // namespace conduit::test
