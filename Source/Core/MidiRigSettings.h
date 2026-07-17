#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
enum class RigDeviceKind : int { soundGenerator = 0, controller };

/** MIDI-Rig M6: Takeover-Verhalten eines Controllers -- `pickup` = Soft-
    Takeover (Werte greifen erst bei Naehe/Kreuzung, Pickup-LEDs zeigen den
    Wartezustand), `jump` = Werte greifen sofort (Motorfader-/Ribbon-Geraete,
    M7-Vorgriff). */
enum class TakeoverMode : int { pickup = 0, jump };

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

    /** MIDI-Rig M4b: der MIDI-Kanal, auf dem dieses Geraet sendet/empfaengt
        (1..16) -- Controller-Profile matchen kanal-agnostisch, Feedback wird
        auf DIESEM Kanal gesendet (User-Entscheidung 14.07.2026: Kanal ist
        Geraete-Eigenschaft, nicht Profil-Eigenschaft). */
    int midiChannel = 1;

    /** MIDI-Rig M4 (ADR 006 E2): nur bei kind == controller sinnvoll --
        Name eines geladenen `ControllerProfile` (ControllerProfileLibrary),
        explizit über einen Picker gesetzt statt über `label` gematcht
        (überlebt Umbenennung des Geräts). Leer = kein Profil zugewiesen. */
    juce::String controllerProfileName;

    /** MIDI-Rig M6: nur bei kind == controller sinnvoll -- Default pickup
        (bisheriges Verhalten, XML-Attribut fehlend = pickup). */
    TakeoverMode takeoverMode = TakeoverMode::pickup;
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

    /** M4: welches ControllerProfile (ControllerProfileLibrary::find)
        dieses Geraet nutzt -- nur bei kind == controller sinnvoll. */
    void setControllerProfileName (const juce::Uuid& id, const juce::String& profileName);

    /** M4b: MIDI-Kanal des Geraets (1..16, geklemmt). */
    void setMidiChannel (const juce::Uuid& id, int channel);

    /** M6: Takeover-Verhalten (Pickup/Sprung) -- nur bei kind == controller
        sinnvoll. */
    void setTakeoverMode (const juce::Uuid& id, TakeoverMode mode);

    //==========================================================================
    // Grid-Rollen (M1b): die zwei aus den GridPanelSettings migrierten
    // Geräte — Controller (Block G) und Grid-Ausgang (MPE-Out + Noten-Echo-In,
    // Block H4). Null-Uuid = Rolle unbesetzt (Gerät gelöscht/nie migriert);
    // die Grid-UI legt bei Bedarf ein neues Gerät an und setzt die Rolle neu.

    [[nodiscard]] juce::Uuid getGridControllerDeviceId() const noexcept { return gridControllerDeviceId; }
    void setGridControllerDeviceId (const juce::Uuid& id);

    [[nodiscard]] juce::Uuid getGridOutputDeviceId() const noexcept { return gridOutputDeviceId; }
    void setGridOutputDeviceId (const juce::Uuid& id);

    /** Live-Remote-Bridge (07/2026): das Geraet, das als Ableton-Live-
        Fernbedienung dient (AlphaTrack). Null-Uuid = Rolle unbesetzt.
        KONFLIKTREGEL: traegt DASSELBE Geraet zusaetzlich die Grid-
        Controller-Rolle, bleibt die Bridge inaktiv (sonst konsumieren
        Grid-Bindungen UND Bridge denselben Fader und zwei Motor-Router
        senden gegeneinander) -- durchgesetzt in LiveRemoteBridge. */
    [[nodiscard]] juce::Uuid getLiveRemoteDeviceId() const noexcept { return liveRemoteDeviceId; }
    void setLiveRemoteDeviceId (const juce::Uuid& id);

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
    /** Pfad der eigenen Settings-Datei (Muster GridPanelSettings::
        sessionFile) — Basis für Nachbar-Ablagen wie Conduit/Devices/.
        Nicht-const: getUserSettings() erzeugt die PropertiesFile lazy. */
    [[nodiscard]] juce::File settingsFile();

    /** E1b-Schalter: Klartext-CC-Schnellpfad (HardwareDevices.txt +
        Faktor-Klartext-DB) laden — Default an. */
    [[nodiscard]] bool isLegacyCcListEnabled() const noexcept { return useLegacyCcList; }
    void setLegacyCcListEnabled (bool enabled);

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
    juce::Uuid liveRemoteDeviceId     = juce::Uuid::null();
    bool migratedFromGridPanel = false;
    bool useLegacyCcList = true;   // E1b-Schnellpfad, Default an

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRigSettings)
};

} // namespace conduit
