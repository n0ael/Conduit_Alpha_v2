#include "GridPage.h"

namespace conduit
{

GridPage::GridPage (grid::GridVoiceEngine& engine, grid::MidiDeviceTarget& midiTargetToUse)
    : midiTarget (midiTargetToUse), keyboard (engine)
{
    addAndMakeVisible (outputCombo);
    addAndMakeVisible (keyboard);

    rebuildDeviceList();
    outputCombo.onChange = [this] { handleDeviceSelected(); };
}

void GridPage::rebuildDeviceList()
{
    devices = grid::MidiDeviceTarget::availableDevices();

    outputCombo.clear (juce::dontSendNotification);
    outputCombo.addItem ("Kein Ausgang", 1);

    for (int i = 0; i < devices.size(); ++i)
        outputCombo.addItem (devices.getReference (i).name, i + 2);

    outputCombo.setSelectedId (1, juce::dontSendNotification);
}

void GridPage::handleDeviceSelected()
{
    const auto selectedId = outputCombo.getSelectedId();

    if (selectedId <= 1)
    {
        midiTarget.closeDevice();
        return;
    }

    const auto index = selectedId - 2;
    if (index >= 0 && index < devices.size())
        midiTarget.openDevice (devices.getReference (index).identifier);
}

void GridPage::resized()
{
    auto bounds = getLocalBounds();
    outputCombo.setBounds (bounds.removeFromTop (32).reduced (8, 4));
    keyboard.setBounds (bounds);
}

} // namespace conduit
