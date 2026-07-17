#include "MidiRigSettingsComponent.h"

namespace conduit
{

namespace
{
    constexpr int kRowHeight    = 44;   // Touch-Target (CLAUDE.md §10.0)
    constexpr int kRowGap       = 4;
    constexpr int kGroupGap     = 22;   // Abschnitts-Header-Hoehe (M4b)
    constexpr int kPadding      = 18;
    constexpr int kStatusWidth  = 40;   // zwei LED-Punkte (In/Out)

    constexpr int kNoPortId     = 1;
}

//==============================================================================
MidiRigSettingsComponent::DeviceRow::DeviceRow (MidiRigSettingsComponent& ownerToUse,
                                                const juce::Uuid& deviceIdToUse)
    : owner (ownerToUse), deviceId (deviceIdToUse)
{
    const auto index = owner.settings.indexOfId (deviceId);
    const auto device = index >= 0 ? owner.settings.getDevice (index) : RigDevice{};

    nameLabel.setText (device.label, juce::dontSendNotification);
    nameLabel.setEditable (false, true);   // Doppelklick = umbenennen
    nameLabel.setMinimumHorizontalScale (1.0f);   // NIE stauchen (User-Regel)
    nameLabel.onTextChange = [this]
    { owner.settings.setLabel (deviceId, nameLabel.getText()); };
    addAndMakeVisible (nameLabel);

    // M4b: Kanal-Dropdown statt Kind-Combo (die Kategorie bestimmt der
    // Anlage-Picker; Kanal ist Geraete-Eigenschaft, Profil-Matching
    // kanal-agnostisch).
    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem ("Ch " + juce::String (ch), ch);
    channelBox.setSelectedId (juce::jlimit (1, 16, device.midiChannel), juce::dontSendNotification);
    channelBox.onChange = [this]
    { owner.settings.setMidiChannel (deviceId, channelBox.getSelectedId()); };
    addAndMakeVisible (channelBox);

    // M4b: Grid-Marker -- welcher Controller steuert das Grid (ehemals
    // inputCombo des Grid-Settings-Tabs). Nur bei Controller-Zeilen sichtbar;
    // der Registry-Broadcast baut die Zeilen neu, der Zustand ist also
    // immer frisch aus dem Ctor.
    gridButton.setClickingTogglesState (false);
    gridButton.setToggleState (owner.settings.getGridControllerDeviceId() == deviceId,
                               juce::dontSendNotification);
    gridButton.onClick = [this]
    { owner.settings.setGridControllerDeviceId (deviceId); };
    addChildComponent (gridButton);

    // Live-Remote-Bridge: welches Geraet ist die Ableton-Fernbedienung
    // (AlphaTrack). Anders als der Grid-Marker abschaltbar (erneuter Klick
    // gibt die Rolle frei) -- die Bridge haelt sonst Abos aufs Geraet.
    // KONFLIKT Grid+Live auf demselben Geraet laesst die Bridge inaktiv
    // (LiveRemoteBridge, doppelte Fader-Konsumenten).
    liveButton.setClickingTogglesState (false);
    liveButton.setToggleState (owner.settings.getLiveRemoteDeviceId() == deviceId,
                               juce::dontSendNotification);
    liveButton.onClick = [this]
    {
        const auto isActive = owner.settings.getLiveRemoteDeviceId() == deviceId;
        owner.settings.setLiveRemoteDeviceId (isActive ? juce::Uuid::null() : deviceId);
    };
    addChildComponent (liveButton);

    populatePortCombo (inBox, owner.hub.availableInputs(), device.midiInName);
    inBox.onChange = [this]
    { owner.settings.setMidiInName (deviceId, selectedPortName (inBox, owner.hub.availableInputs())); };
    addAndMakeVisible (inBox);

    populatePortCombo (outBox, owner.hub.availableOutputs(), device.midiOutName);
    outBox.onChange = [this]
    { owner.settings.setMidiOutName (deviceId, selectedPortName (outBox, owner.hub.availableOutputs())); };
    addAndMakeVisible (outBox);

    populateProfileCombo (device.controllerProfileName);
    profileBox.onChange = [this]
    {
        const auto selectedId = profileBox.getSelectedId();
        const auto& profiles = owner.controllerProfileLibrary.profiles();
        const auto profileIndex = selectedId - 2;   // 1 = kNoPortId-Aequivalent "-"
        owner.settings.setControllerProfileName (
            deviceId, profileIndex >= 0 && profileIndex < (int) profiles.size()
                          ? profiles[(size_t) profileIndex].device
                          : juce::String());
    };
    addChildComponent (profileBox);   // sichtbar nur bei kind == controller (resized())

    // M6: Takeover-Verhalten (nur Controller) -- Pickup = Soft-Takeover mit
    // Pickup-LEDs, Sprung = Werte greifen sofort (Motorfader/Ribbons, M7).
    takeoverBox.addItem ("Pickup", 1 + (int) TakeoverMode::pickup);
    takeoverBox.addItem ("Sprung", 1 + (int) TakeoverMode::jump);
    takeoverBox.setSelectedId (1 + (int) device.takeoverMode, juce::dontSendNotification);
    takeoverBox.onChange = [this]
    { owner.settings.setTakeoverMode (deviceId, (TakeoverMode) (takeoverBox.getSelectedId() - 1)); };
    addChildComponent (takeoverBox);   // sichtbar nur bei kind == controller (resized())

    removeButton.onClick = [this] { owner.settings.removeDevice (deviceId); };
    addAndMakeVisible (removeButton);

    refreshStatus();
}

void MidiRigSettingsComponent::DeviceRow::populateProfileCombo (const juce::String& savedProfileName)
{
    profileBox.clear (juce::dontSendNotification);
    profileBox.addItem (juce::String::fromUTF8 ("\xe2\x80\x94"), kNoPortId);   // „—" = kein Profil

    const auto& profiles = owner.controllerProfileLibrary.profiles();
    for (int i = 0; i < (int) profiles.size(); ++i)
        profileBox.addItem (profiles[(size_t) i].device, i + 2);

    profileBox.setSelectedId (kNoPortId, juce::dontSendNotification);

    for (int i = 0; savedProfileName.isNotEmpty() && i < (int) profiles.size(); ++i)
    {
        if (profiles[(size_t) i].device == savedProfileName)
        {
            profileBox.setSelectedId (i + 2, juce::dontSendNotification);
            return;
        }
    }

    // Gespeichertes Profil aktuell nicht geladen: als deaktivierten Eintrag
    // zeigen statt still auf „—" zu fallen (Muster (getrennt)-Geist).
    if (savedProfileName.isNotEmpty())
    {
        const auto ghostId = (int) profiles.size() + 2;
        profileBox.addItem (savedProfileName + " (fehlt)", ghostId);
        profileBox.setSelectedId (ghostId, juce::dontSendNotification);
    }
}

void MidiRigSettingsComponent::DeviceRow::populatePortCombo (juce::ComboBox& combo,
                                                             const juce::Array<juce::MidiDeviceInfo>& ports,
                                                             const juce::String& savedName)
{
    combo.clear (juce::dontSendNotification);
    combo.addItem (juce::String::fromUTF8 ("\xe2\x80\x94"), kNoPortId);   // „—" = kein Port

    for (int i = 0; i < ports.size(); ++i)
        combo.addItem (ports.getReference (i).name, i + 2);

    combo.setSelectedId (kNoPortId, juce::dontSendNotification);

    for (int i = 0; savedName.isNotEmpty() && i < ports.size(); ++i)
    {
        if (ports.getReference (i).name == savedName)
        {
            combo.setSelectedId (i + 2, juce::dontSendNotification);
            return;
        }
    }

    // Gespeicherter Port aktuell nicht vorhanden (abgesteckt): als
    // deaktivierten Eintrag anzeigen statt still auf „—" zu fallen —
    // der Name bleibt in der Registry erhalten (Reconnect bindet neu).
    if (savedName.isNotEmpty())
    {
        const auto ghostId = ports.size() + 2;
        combo.addItem (savedName + " (getrennt)", ghostId);
        combo.setSelectedId (ghostId, juce::dontSendNotification);
    }
}

juce::String MidiRigSettingsComponent::DeviceRow::selectedPortName (
    const juce::ComboBox& combo, const juce::Array<juce::MidiDeviceInfo>& ports) const
{
    const auto index = combo.getSelectedId() - 2;
    if (index >= 0 && index < ports.size())
        return ports.getReference (index).name;

    if (combo.getSelectedId() == ports.size() + 2)   // „(getrennt)"-Geist
    {
        const auto rowIndex = owner.settings.indexOfId (deviceId);
        if (rowIndex >= 0)
        {
            const auto device = owner.settings.getDevice (rowIndex);
            return &combo == &inBox ? device.midiInName : device.midiOutName;
        }
    }

    return {};
}

void MidiRigSettingsComponent::DeviceRow::refreshStatus()
{
    const auto newIn  = owner.hub.isInputConnected (deviceId);
    const auto newOut = owner.hub.isOutputConnected (deviceId);

    if (newIn != inConnected || newOut != outConnected)
    {
        inConnected = newIn;
        outConnected = newOut;
        repaint();
    }
}

void MidiRigSettingsComponent::DeviceRow::paint (juce::Graphics& g)
{
    // Verbunden-Status: zwei LED-Punkte rechts — In (oben) / Out (unten).
    auto statusArea = getLocalBounds().removeFromRight (kStatusWidth).reduced (8);
    const auto dotSize = 10;

    // dotSize ist const-integral -- Clang verbietet die unnoetige Capture
    const auto drawDot = [&g] (juce::Rectangle<int> area, bool connected)
    {
        g.setColour (connected ? juce::Colour (0xff4caf50) : juce::Colour (0xff5a5a5a));
        g.fillEllipse (area.withSizeKeepingCentre (dotSize, dotSize).toFloat());
    };

    drawDot (statusArea.removeFromTop (statusArea.getHeight() / 2), inConnected);
    drawDot (statusArea, outConnected);
}

void MidiRigSettingsComponent::DeviceRow::resized()
{
    const auto index = owner.settings.indexOfId (deviceId);
    const auto isController = index >= 0
        && owner.settings.getDevice (index).kind == RigDeviceKind::controller;
    profileBox.setVisible (isController);
    takeoverBox.setVisible (isController);   // M6
    gridButton.setVisible (isController);
    liveButton.setVisible (isController);    // Bridge: Live-Remote-Marker

    auto area = getLocalBounds();
    area.removeFromRight (kStatusWidth);   // Status-LEDs (paint)

    removeButton.setBounds (area.removeFromRight (36).reduced (2));

    if (isController)
    {
        liveButton.setBounds (area.removeFromRight (48).reduced (2, 6));
        area.removeFromRight (2);
        gridButton.setBounds (area.removeFromRight (48).reduced (2, 6));
        area.removeFromRight (2);
    }

    nameLabel.setBounds (area.removeFromLeft (juce::jmax (80, area.getWidth() / 5)));
    area.removeFromLeft (4);

    // M4b: Kanal schmal, In/Out (und bei Controllern das Profil) teilen den Rest.
    channelBox.setBounds (area.removeFromLeft (64).reduced (0, 6));
    area.removeFromLeft (4);

    // M6: Controller-Zeilen tragen 4 Combos (In/Out/Profil/Takeover).
    const auto comboCount = isController ? 4 : 2;
    const auto comboWidth = (area.getWidth() - 4 * (comboCount - 1)) / comboCount;

    inBox.setBounds (area.removeFromLeft (comboWidth).reduced (0, 6));
    area.removeFromLeft (4);

    if (isController)
    {
        outBox.setBounds (area.removeFromLeft (comboWidth).reduced (0, 6));
        area.removeFromLeft (4);
        profileBox.setBounds (area.removeFromLeft (comboWidth).reduced (0, 6));
        area.removeFromLeft (4);
        takeoverBox.setBounds (area.reduced (0, 6));
    }
    else
    {
        outBox.setBounds (area.reduced (0, 6));
    }
}

//==============================================================================
MidiRigSettingsComponent::MidiRigSettingsComponent (MidiRigSettings& settingsToUse,
                                                    MidiPortHub& hubToUse,
                                                    MidiProfileLibrary& profileLibraryToUse,
                                                    ControllerProfileLibrary& controllerProfileLibraryToUse)
    : settings (settingsToUse), hub (hubToUse), profileLibrary (profileLibraryToUse),
      controllerProfileLibrary (controllerProfileLibraryToUse)
{
    header.setText (juce::String::fromUTF8 ("MIDI-Ger\xc3\xa4te (Rig)"), juce::dontSendNotification);
    header.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (header);

    columnsLabel.setText ("Name / Kanal / In / Out / Verbunden", juce::dontSendNotification);
    columnsLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (columnsLabel);

    // M4b: "+ Geraet" oeffnet den Anlage-Picker (Profil-Liste beider
    // Bibliotheken) statt direkt ein leeres Geraet anzulegen.
    addButton.onClick = [this] { openCreatePicker(); };
    addAndMakeVisible (addButton);

    instrumentsHeader.setText ("Instrumente", juce::dontSendNotification);
    instrumentsHeader.setColour (juce::Label::textColourId, juce::Colours::grey);
    rowContainer.addAndMakeVisible (instrumentsHeader);

    controllersHeader.setText ("Controller", juce::dontSendNotification);
    controllersHeader.setColour (juce::Label::textColourId, juce::Colours::grey);
    rowContainer.addAndMakeVisible (controllersHeader);

    viewport.setViewedComponent (&rowContainer, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // Sektion „Profile" (M2, ADR E1/E1b).
    profileHeader.setText ("Profile", juce::dontSendNotification);
    profileHeader.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (profileHeader);

    legacyToggle.setToggleState (settings.isLegacyCcListEnabled(), juce::dontSendNotification);
    legacyToggle.onClick = [this]
    { settings.setLegacyCcListEnabled (legacyToggle.getToggleState()); };
    addAndMakeVisible (legacyToggle);

    reportBox.setMultiLine (true);
    reportBox.setReadOnly (true);
    reportBox.setScrollbarsShown (true);
    addAndMakeVisible (reportBox);

    reloadButton.onClick = [this]
    {
        profileLibrary.reload();
        refreshProfileReport();
    };
    addAndMakeVisible (reloadButton);

    attributionLabel.setText (juce::String::fromUTF8 (
        "Ger\xc3\xa4teprofile: Daten von midi.guide (CC-BY-SA 4.0)"), juce::dontSendNotification);
    attributionLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (attributionLabel);
    refreshProfileReport();

    // Sektion „Controller-Profile" (M4, ADR E2) -- kein Legacy-Toggle-Aequivalent.
    controllerProfileHeader.setText (juce::String::fromUTF8 ("Controller-Profile"),
                                     juce::dontSendNotification);
    controllerProfileHeader.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (controllerProfileHeader);

    controllerReportBox.setMultiLine (true);
    controllerReportBox.setReadOnly (true);
    controllerReportBox.setScrollbarsShown (true);
    addAndMakeVisible (controllerReportBox);

    controllerReloadButton.onClick = [this]
    {
        controllerProfileLibrary.reload();
        refreshControllerProfileReport();
        rebuildRows();   // Profil-Dropdowns koennen neue/entfallene Eintraege haben
    };
    addAndMakeVisible (controllerReloadButton);
    refreshControllerProfileReport();

    settings.addChangeListener (this);
    rebuildRows();
    startTimer (1000);   // Verbunden-Status (der Hub broadcastet nicht)
}

void MidiRigSettingsComponent::refreshProfileReport()
{
    // Nicht-ASCII-Literale NUR über fromUTF8 (Rule ui-design).
    const auto dot        = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto deviceWord = juce::String::fromUTF8 (" Ger\xc3\xa4t");
    const auto skippedWord = juce::String::fromUTF8 (" \xc3\xbc" "bersprungen");

    juce::String text;

    for (const auto& source : profileLibrary.report())
    {
        text << (source.fromUserFolder ? "[User]    " : "[Factory] ")
             << source.sourceName
             << dot << source.devices << deviceWord << (source.devices == 1 ? "" : "e")
             << ", " << source.parse.accepted << " Parameter";

        if (source.parse.skipped > 0)
            text << ", " << source.parse.skipped << skippedWord;

        text << "\n";

        for (const auto& warning : source.parse.warnings)
            text << "    ! " << warning << "\n";
    }

    if (text.isEmpty())
        text = "Keine Profile geladen.";

    reportBox.setText (text, false);
}

void MidiRigSettingsComponent::refreshControllerProfileReport()
{
    const auto dot         = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto skippedWord = juce::String::fromUTF8 (" \xc3\xbc" "bersprungen");

    juce::String text;

    for (const auto& source : controllerProfileLibrary.report())
    {
        text << (source.fromUserFolder ? "[User]    " : "[Factory] ")
             << source.sourceName
             << dot << source.controls << " Controls"
             << ", " << source.parse.accepted << " Zeilen";

        if (source.parse.skipped > 0)
            text << ", " << source.parse.skipped << skippedWord;

        text << "\n";

        for (const auto& warning : source.parse.warnings)
            text << "    ! " << warning << "\n";
    }

    if (text.isEmpty())
        text = "Keine Controller-Profile geladen.";

    controllerReportBox.setText (text, false);
}

MidiRigSettingsComponent::~MidiRigSettingsComponent()
{
    stopTimer();
    settings.removeChangeListener (this);
}

void MidiRigSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &settings)
        rebuildRows();
}

void MidiRigSettingsComponent::timerCallback()
{
    for (const auto& row : rows)
        row->refreshStatus();
}

void MidiRigSettingsComponent::rebuildRows()
{
    hub.refreshAvailableDevices();
    rows.clear();

    // M4b: gruppiert -- erst Instrumente, dann Controller (die Abschnitts-
    // Header setzt resized() vor die jeweils erste Zeile der Gruppe).
    for (const auto wantedKind : { RigDeviceKind::soundGenerator, RigDeviceKind::controller })
    {
        for (int i = 0; i < settings.getNumDevices(); ++i)
        {
            const auto device = settings.getDevice (i);
            if (device.kind != wantedKind)
                continue;

            auto row = std::make_unique<DeviceRow> (*this, device.id);
            rowContainer.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
    }

    resized();
}

//==============================================================================
// M4b: Anlage-Picker ("+ Geraet")

namespace
{
    /** CallOutBox-Inhalt: flache Liste bekannter Profile beider Bibliotheken
        + "Eigenes ..."-Eintraege (Muster TrackSelectorPanel: custom paint,
        Tap waehlt, schliesst selbst). */
    class DeviceCreatePicker final : public juce::Component
    {
    public:
        struct Entry
        {
            juce::String label;
            conduit::RigDeviceKind kind = conduit::RigDeviceKind::soundGenerator;
            juce::String profileName;   // leer = "Eigenes ..." ohne Profil
            bool isHeader = false;
        };

        DeviceCreatePicker (const conduit::MidiProfileLibrary& instruments,
                            const conduit::ControllerProfileLibrary& controllers)
        {
            entries.push_back ({ "Instrumente", conduit::RigDeviceKind::soundGenerator, {}, true });
            for (const auto& profile : instruments.profiles())
                entries.push_back ({ profile.displayName(), conduit::RigDeviceKind::soundGenerator,
                                     profile.displayName(), false });
            entries.push_back ({ juce::String::fromUTF8 ("Eigenes Instrument\xe2\x80\xa6"),
                                 conduit::RigDeviceKind::soundGenerator, {}, false });

            entries.push_back ({ "Controller", conduit::RigDeviceKind::controller, {}, true });
            for (const auto& profile : controllers.profiles())
                entries.push_back ({ profile.device, conduit::RigDeviceKind::controller,
                                     profile.device, false });
            entries.push_back ({ juce::String::fromUTF8 ("Eigener Controller\xe2\x80\xa6"),
                                 conduit::RigDeviceKind::controller, {}, false });

            setSize (kWidth, (int) entries.size() * kEntryHeight);
        }

        std::function<void (conduit::RigDeviceKind, const juce::String& profileName)> onChosen;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff23262b));

            for (int i = 0; i < (int) entries.size(); ++i)
            {
                const auto& entry = entries[(size_t) i];
                const juce::Rectangle<int> bounds (0, i * kEntryHeight, getWidth(), kEntryHeight);

                g.setColour (entry.isHeader ? juce::Colours::grey : juce::Colours::white);
                g.setFont (juce::Font (juce::FontOptions (entry.isHeader ? 12.0f : 14.0f,
                                                          entry.isHeader ? juce::Font::bold
                                                                         : juce::Font::plain)));
                g.drawFittedText (entry.label, bounds.reduced (entry.isHeader ? 8 : 16, 0),
                                  juce::Justification::centredLeft, 1, 1.0f);
            }
        }

        void mouseUp (const juce::MouseEvent& event) override
        {
            const auto index = event.getPosition().y / kEntryHeight;
            if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].isHeader)
                return;

            if (onChosen != nullptr)
                onChosen (entries[(size_t) index].kind, entries[(size_t) index].profileName);

            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        }

        static constexpr int kWidth = 280;
        static constexpr int kEntryHeight = 44;   // Touch-Zone (CLAUDE.md 10.0)

    private:
        std::vector<Entry> entries;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceCreatePicker)
    };
}

void MidiRigSettingsComponent::openCreatePicker()
{
    auto picker = std::make_unique<DeviceCreatePicker> (profileLibrary, controllerProfileLibrary);
    picker->onChosen = [this] (RigDeviceKind kind, const juce::String& profileName)
    { createDeviceFromPicker (kind, profileName); };

    juce::CallOutBox::launchAsynchronously (std::move (picker),
                                            addButton.getScreenBounds(), nullptr);
}

void MidiRigSettingsComponent::createDeviceFromPicker (RigDeviceKind kind,
                                                       const juce::String& profileName)
{
    const auto label = profileName.isNotEmpty() ? profileName
                                                : juce::String::fromUTF8 ("Neues Ger\xc3\xa4t");
    const auto id = settings.addDevice (label, kind);

    if (kind == RigDeviceKind::controller)
    {
        if (profileName.isNotEmpty())
            settings.setControllerProfileName (id, profileName);

        // Erster Controller uebernimmt automatisch die Grid-Rolle.
        if (settings.getGridControllerDeviceId().isNull()
            || settings.indexOfId (settings.getGridControllerDeviceId()) < 0)
            settings.setGridControllerDeviceId (id);
    }
}

void MidiRigSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (kPadding);
    header.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    columnsLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);

    // Sektion „Controller-Profile" (M4) ganz unten, feste Höhe — kein
    // Legacy-Toggle-Aequivalent, daher etwas niedriger als die M2-Sektion.
    auto controllerProfileArea = area.removeFromBottom (140);
    auto controllerButtonRow = controllerProfileArea.removeFromBottom (28);
    controllerReloadButton.setBounds (controllerButtonRow.removeFromLeft (110));
    controllerProfileArea.removeFromBottom (4);
    controllerProfileHeader.setBounds (controllerProfileArea.removeFromTop (24));
    controllerProfileArea.removeFromTop (4);
    controllerReportBox.setBounds (controllerProfileArea);
    area.removeFromBottom (8);

    // Sektion „Profile" (M2) unten, feste Höhe — Geräteliste flext oben.
    auto profileArea = area.removeFromBottom (190);
    attributionLabel.setBounds (profileArea.removeFromBottom (20));
    profileArea.removeFromBottom (4);
    auto profileButtonRow = profileArea.removeFromBottom (28);
    reloadButton.setBounds (profileButtonRow.removeFromLeft (110));
    profileArea.removeFromBottom (4);
    profileHeader.setBounds (profileArea.removeFromTop (24));
    legacyToggle.setBounds (profileArea.removeFromTop (24));
    profileArea.removeFromTop (4);
    reportBox.setBounds (profileArea);
    area.removeFromBottom (8);

    addButton.setBounds (area.removeFromBottom (kRowHeight).removeFromLeft (140));
    area.removeFromBottom (8);

    viewport.setBounds (area);

    // M4b: gruppiert -- Abschnitts-Header vor der jeweils ersten Zeile
    // jeder Kategorie (rows ist bereits Instrumente-zuerst sortiert).
    const auto rowWidth = viewport.getMaximumVisibleWidth();

    int y = 0;
    bool seenInstrumentHeader = false;
    bool seenControllerHeader = false;

    for (const auto& row : rows)
    {
        const auto index = settings.indexOfId (row->id());
        const auto isController = index >= 0
            && settings.getDevice (index).kind == RigDeviceKind::controller;

        if (! isController && ! seenInstrumentHeader)
        {
            instrumentsHeader.setBounds (0, y, rowWidth, kGroupGap);
            seenInstrumentHeader = true;
            y += kGroupGap;
        }
        else if (isController && ! seenControllerHeader)
        {
            controllersHeader.setBounds (0, y, rowWidth, kGroupGap);
            seenControllerHeader = true;
            y += kGroupGap;
        }

        row->setBounds (0, y, rowWidth, kRowHeight);
        y += kRowHeight + kRowGap;
    }

    instrumentsHeader.setVisible (seenInstrumentHeader);
    controllersHeader.setVisible (seenControllerHeader);
    rowContainer.setSize (rowWidth, y);
}

} // namespace conduit
