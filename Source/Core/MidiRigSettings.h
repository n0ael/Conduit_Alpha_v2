#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
enum class RigDeviceKind : int { soundGenerator = 0, controller };

/** Ein registriertes MIDI-Rig-Gerät (Klangerzeuger ODER Controller,
    ADR 006 E1/E2). `midiOutName`/`midiInName` sind die zuletzt bekannten
    MIDI-Portnamen (leer = diese Richtung ungenutzt) — das Matching gegen
    aktuell verfügbare Ports übernimmt `MidiPortHub` (exakt→Prefix, ADR E3). */
struct RigDevice
{
    juce::Uuid id;
    juce::String label;
    RigDeviceKind kind = RigDeviceKind::soundGenerator;
    juce::String midiOutName;
    juce::String midiInName;
};

//==============================================================================
/**
    Persistente Registry der MIDI-Rig-Geräte (ADR 006 E3) — App-Zustand,
    NIE Patch-Zustand (CLAUDE.md §6). STRUKTURIERT als ValueTree↔XML in
    einer EIGENEN PropertiesFile (Conduit/MidiRig.settings, Muster
    LooperSettings): eine dynamische Geräteliste (nicht fixes Array,
    Anzahl user-bestimmt), jedes Gerät trägt eine stabile `juce::Uuid`
    für Referenzen aus späteren Meilensteinen (Profile-Zuordnung,
    Macro-Ziele — Muster CcControlModel-Ids, Grid Block K).

    Threading: alle Methoden Message Thread; UI-Benachrichtigung über
    juce::ChangeBroadcaster (async).
*/
class MidiRigSettings : public juce::ChangeBroadcaster
{
public:
    /** Eigene Datei neben Conduit.settings (Muster LooperSettings). */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit MidiRigSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~MidiRigSettings() override;

    [[nodiscard]] int getNumDevices() const noexcept { return devices.size(); }
    [[nodiscard]] RigDevice getDevice (int index) const;

    /** -1, wenn kein Gerät mit dieser Id existiert. */
    [[nodiscard]] int indexOfId (const juce::Uuid& id) const noexcept;

    /** Legt ein neues Gerät an, gibt dessen (neu vergebene) Id zurück. */
    juce::Uuid addDevice (const juce::String& label, RigDeviceKind kind);
    void removeDevice (const juce::Uuid& id);

    void setLabel (const juce::Uuid& id, const juce::String& label);
    void setKind (const juce::Uuid& id, RigDeviceKind kind);
    void setMidiOutName (const juce::Uuid& id, const juce::String& portName);
    void setMidiInName (const juce::Uuid& id, const juce::String& portName);

    //==========================================================================
    // Grid-Rollen (M1b): die zwei aus den GridPanelSettings migrierten
    // Geräte — Controller (Block G) und Grid-Ausgang (MPE-Out + Noten-Echo-In,
    // Block H4). Null-Uuid = Rolle unbesetzt (Gerät gelöscht/nie migriert);
    // die Grid-UI legt bei Bedarf ein neues Gerät an und setzt die Rolle neu.

    [[nodiscard]] juce::Uuid getGridControllerDeviceId() const noexcept { return gridControllerDeviceId; }
    void setGridControllerDeviceId (const juce::Uuid& id);

    [[nodiscard]] juce::Uuid getGridOutputDeviceId() const noexcept { return gridOutputDeviceId; }
    void setGridOutputDeviceId (const juce::Uuid& id);

    /** Einmal-Migration der GridPanelSettings-Gerätenamen (M1b): ohne
        gesetztes Migrations-Flag UND mit mindestens einem nicht-leeren
        Namen entstehen zwei RigDevices („Controller" aus controlIn,
        „Grid-Ausgang" aus gridOut + echoIn als Eingang), die Rollen-Ids
        werden gesetzt und das Flag verhindert den Zweitlauf. Die
        Quell-Strings in den GridPanelSettings bleiben unangetastet. */
    void migrateFromGridPanel (const juce::String& controlInName,
                               const juce::String& gridOutName,
                               const juce::String& echoInName);

    [[nodiscard]] bool hasMigratedFromGridPanel() const noexcept { return migratedFromGridPanel; }

    //==========================================================================
    /** [Message Thread] Ausstehende Änderungen sofort auf Platte schreiben. */
    void flush();

private:
    void loadFromFile();
    void writeAndNotify();

    juce::ApplicationProperties applicationProperties;
    juce::Array<RigDevice> devices;

    juce::Uuid gridControllerDeviceId = juce::Uuid::null();
    juce::Uuid gridOutputDeviceId     = juce::Uuid::null();
    bool migratedFromGridPanel = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRigSettings)
};

} // namespace conduit
