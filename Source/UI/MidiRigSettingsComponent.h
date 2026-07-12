#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/MidiPortHub.h"

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
*/
class MidiRigSettingsComponent final : public juce::Component,
                                       private juce::ChangeListener,
                                       private juce::Timer
{
public:
    MidiRigSettingsComponent (MidiRigSettings& settingsToUse, MidiPortHub& hubToUse);
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

        juce::Label nameLabel;
        juce::ComboBox kindBox;
        juce::ComboBox inBox;
        juce::ComboBox outBox;
        juce::TextButton removeButton { "X" };

        bool inConnected = false;
        bool outConnected = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceRow)
    };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void rebuildRows();

    MidiRigSettings& settings;
    MidiPortHub& hub;

    juce::Label header;
    juce::Label columnsLabel;
    juce::TextButton addButton { juce::String::fromUTF8 ("+ Ger\xc3\xa4t") };
    juce::Viewport viewport;
    juce::Component rowContainer;
    std::vector<std::unique_ptr<DeviceRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRigSettingsComponent)
};

} // namespace conduit
