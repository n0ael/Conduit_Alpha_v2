#include "MidiRigSettingsComponent.h"

namespace conduit
{

namespace
{
    constexpr int kRowHeight   = 44;   // Touch-Target (CLAUDE.md §10.0)
    constexpr int kRowGap      = 4;
    constexpr int kPadding     = 18;
    constexpr int kStatusWidth = 40;   // zwei LED-Punkte (In/Out)

    constexpr int kKindSoundGenerator = 1;
    constexpr int kKindController     = 2;
    constexpr int kNoPortId           = 1;
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

    kindBox.addItem ("Klangerzeuger", kKindSoundGenerator);
    kindBox.addItem ("Controller", kKindController);
    kindBox.setSelectedId (device.kind == RigDeviceKind::controller ? kKindController
                                                                    : kKindSoundGenerator,
                           juce::dontSendNotification);
    kindBox.onChange = [this]
    {
        owner.settings.setKind (deviceId,
                                kindBox.getSelectedId() == kKindController
                                    ? RigDeviceKind::controller
                                    : RigDeviceKind::soundGenerator);
    };
    addAndMakeVisible (kindBox);

    populatePortCombo (inBox, owner.hub.availableInputs(), device.midiInName);
    inBox.onChange = [this]
    { owner.settings.setMidiInName (deviceId, selectedPortName (inBox, owner.hub.availableInputs())); };
    addAndMakeVisible (inBox);

    populatePortCombo (outBox, owner.hub.availableOutputs(), device.midiOutName);
    outBox.onChange = [this]
    { owner.settings.setMidiOutName (deviceId, selectedPortName (outBox, owner.hub.availableOutputs())); };
    addAndMakeVisible (outBox);

    removeButton.onClick = [this] { owner.settings.removeDevice (deviceId); };
    addAndMakeVisible (removeButton);

    refreshStatus();
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

    const auto drawDot = [&g, dotSize] (juce::Rectangle<int> area, bool connected)
    {
        g.setColour (connected ? juce::Colour (0xff4caf50) : juce::Colour (0xff5a5a5a));
        g.fillEllipse (area.withSizeKeepingCentre (dotSize, dotSize).toFloat());
    };

    drawDot (statusArea.removeFromTop (statusArea.getHeight() / 2), inConnected);
    drawDot (statusArea, outConnected);
}

void MidiRigSettingsComponent::DeviceRow::resized()
{
    auto area = getLocalBounds();
    area.removeFromRight (kStatusWidth);   // Status-LEDs (paint)

    removeButton.setBounds (area.removeFromRight (44).reduced (2));
    nameLabel.setBounds (area.removeFromLeft (juce::jmax (90, area.getWidth() / 4)));
    area.removeFromLeft (4);

    const auto comboWidth = (area.getWidth() - 8) / 3;
    kindBox.setBounds (area.removeFromLeft (comboWidth).reduced (0, 6));
    area.removeFromLeft (4);
    inBox.setBounds (area.removeFromLeft (comboWidth).reduced (0, 6));
    area.removeFromLeft (4);
    outBox.setBounds (area.reduced (0, 6));
}

//==============================================================================
MidiRigSettingsComponent::MidiRigSettingsComponent (MidiRigSettings& settingsToUse,
                                                    MidiPortHub& hubToUse)
    : settings (settingsToUse), hub (hubToUse)
{
    header.setText (juce::String::fromUTF8 ("MIDI-Ger\xc3\xa4te (Rig)"), juce::dontSendNotification);
    header.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    addAndMakeVisible (header);

    columnsLabel.setText ("Name / Rolle / In / Out / Verbunden", juce::dontSendNotification);
    columnsLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (columnsLabel);

    addButton.onClick = [this]
    { settings.addDevice (juce::String::fromUTF8 ("Neues Ger\xc3\xa4t"), RigDeviceKind::soundGenerator); };
    addAndMakeVisible (addButton);

    viewport.setViewedComponent (&rowContainer, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    settings.addChangeListener (this);
    rebuildRows();
    startTimer (1000);   // Verbunden-Status (der Hub broadcastet nicht)
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

    for (int i = 0; i < settings.getNumDevices(); ++i)
    {
        auto row = std::make_unique<DeviceRow> (*this, settings.getDevice (i).id);
        rowContainer.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    resized();
}

void MidiRigSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (kPadding);
    header.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    columnsLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);

    addButton.setBounds (area.removeFromBottom (kRowHeight).removeFromLeft (140));
    area.removeFromBottom (8);

    viewport.setBounds (area);

    const auto rowWidth = viewport.getMaximumVisibleWidth();
    rowContainer.setSize (rowWidth,
                          static_cast<int> (rows.size()) * (kRowHeight + kRowGap));

    int y = 0;
    for (const auto& row : rows)
    {
        row->setBounds (0, y, rowWidth, kRowHeight);
        y += kRowHeight + kRowGap;
    }
}

} // namespace conduit
