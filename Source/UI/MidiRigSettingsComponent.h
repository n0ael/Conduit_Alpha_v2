#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ControllerProfileLibrary.h"
#include "Core/MidiPortHub.h"
#include "Core/MidiProfileLibrary.h"

namespace conduit
{

//==============================================================================
/**
    Tab „MIDI" des Einstellungen-Fensters (ADR 006 M1b): Geräteliste der
    MIDI-Rig-Registry — pro Zeile Name (editierbar), Rolle (Klangerzeuger/
    Controller), In-/Out-Port-Dropdowns (aus dem MidiPortHub) und der
    Verbunden-Status beider Richtungen; dazu Hinzufügen/Entfernen. Die
    Ports öffnet/schließt der Hub selbst (Registry-Broadcast) — die UI
    schreibt nur die Registry.

    Zeilenhöhe 44 px (Touch-Target, CLAUDE.md §10.0), keine
    Schriftstauchung. Ein 1-Hz-Timer aktualisiert den Verbunden-Status
    (der Hub broadcastet nicht). Message Thread.

    Sektion „Profile" (M2, ADR E1/E1b): Lade-Report der Klangerzeuger-
    Profile (Factory-CSVs + Conduit/Devices) mit Warnungen — kein stilles
    Scheitern; „Neu laden"-Button, Klartext-Schnellpfad-Toggle und die
    CC-BY-SA-Attribution der midi.guide-Daten.

    Sektion „Controller-Profile" (M4, ADR E2): analoger Lade-Report der
    Controller-Profile (Factory Xone:K1 + Conduit/Controllers), KEIN
    Klartext-Toggle (kein Aequivalent auf der Controller-Seite). Pro
    Controller-Zeile zusaetzlich ein Profil-Dropdown (nur sichtbar bei
    Rolle „Controller") — schreibt `MidiRigSettings::setControllerProfileName`.

    M4b (User-Richtung 14.07.2026, zentrale Geraeteverwaltung): die Liste
    ist nach Kategorie gruppiert (Abschnitts-Header „Instrumente" /
    „Controller", die Kind-Combo pro Zeile entfaellt — die Kategorie
    bestimmt der Anlage-Picker); pro Zeile ein Kanal-Dropdown (1..16,
    `RigDevice::midiChannel` — Profil-Matching ist kanal-agnostisch);
    „+ Geraet" oeffnet einen CallOutBox-Picker mit den bekannten Profilen
    beider Bibliotheken plus „Eigenes …"-Eintraegen; Controller-Zeilen
    tragen einen „Grid"-Marker (setzt `gridControllerDeviceId` — welcher
    Controller steuert das Grid, ehemals inputCombo des Grid-Tabs).
*/
class MidiRigSettingsComponent final : public juce::Component,
                                       private juce::ChangeListener,
                                       private juce::Timer
{
public:
    MidiRigSettingsComponent (MidiRigSettings& settingsToUse, MidiPortHub& hubToUse,
                              MidiProfileLibrary& profileLibraryToUse,
                              ControllerProfileLibrary& controllerProfileLibraryToUse);
    ~MidiRigSettingsComponent() override;

    void resized() override;

private:
    //==========================================================================
    class DeviceRow final : public juce::Component
    {
    public:
        DeviceRow (MidiRigSettingsComponent& ownerToUse, const juce::Uuid& deviceIdToUse);

        void paint (juce::Graphics& g) override;
        void resized() override;

        void refreshStatus();

        [[nodiscard]] juce::Uuid id() const noexcept { return deviceId; }

    private:
        void populatePortCombo (juce::ComboBox& combo,
                                const juce::Array<juce::MidiDeviceInfo>& ports,
                                const juce::String& savedName);
        [[nodiscard]] juce::String selectedPortName (const juce::ComboBox& combo,
                                                     const juce::Array<juce::MidiDeviceInfo>& ports) const;

        MidiRigSettingsComponent& owner;
        juce::Uuid deviceId;

        void populateProfileCombo (const juce::String& savedProfileName);

        juce::Label nameLabel;
        juce::ComboBox channelBox;   // M4b: RigDevice::midiChannel (1..16)
        juce::ComboBox inBox;
        juce::ComboBox outBox;
        juce::ComboBox profileBox;      // M4: nur sichtbar bei kind == controller
        juce::ComboBox takeoverBox;     // M6: Pickup/Sprung (nur Controller)
        juce::TextButton gridButton { "Grid" };   // M4b: Grid-Controller-Marker
        juce::TextButton liveButton { "Live" };   // Bridge: Live-Remote-Marker (AlphaTrack)
        juce::TextButton removeButton { "X" };

        bool inConnected = false;
        bool outConnected = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceRow)
    };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void rebuildRows();
    void refreshProfileReport();
    void refreshControllerProfileReport();

    MidiRigSettings& settings;
    MidiPortHub& hub;
    MidiProfileLibrary& profileLibrary;
    ControllerProfileLibrary& controllerProfileLibrary;

    void openCreatePicker();
    void createDeviceFromPicker (RigDeviceKind kind, const juce::String& profileName);

    juce::Label header;
    juce::Label columnsLabel;
    juce::TextButton addButton { juce::String::fromUTF8 ("+ Ger\xc3\xa4t") };
    juce::Viewport viewport;
    juce::Component rowContainer;
    juce::Label instrumentsHeader;   // M4b: Abschnitts-Header der Gruppierung
    juce::Label controllersHeader;
    std::vector<std::unique_ptr<DeviceRow>> rows;

    // Sektion „Profile" (M2).
    juce::Label profileHeader;
    juce::ToggleButton legacyToggle { "Klartext-CC-Liste laden (HardwareDevices.txt)" };
    juce::TextEditor reportBox;   // read-only, mehrzeilig (Report je Quelle)
    juce::TextButton reloadButton { "Neu laden" };
    juce::Label attributionLabel;

    // Sektion „Controller-Profile" (M4) — analog, ohne Legacy-Toggle.
    juce::Label controllerProfileHeader;
    juce::TextEditor controllerReportBox;
    juce::TextButton controllerReloadButton { "Neu laden" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRigSettingsComponent)
};

} // namespace conduit
