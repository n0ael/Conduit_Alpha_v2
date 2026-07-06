#include "GridPage.h"

namespace conduit
{

GridPage::GridPage (grid::GridVoiceEngine& engineToUse, grid::MidiDeviceTarget& midiTargetToUse)
    : engine (engineToUse), midiTarget (midiTargetToUse), keyboard (engineToUse)
{
    addAndMakeVisible (outputCombo);
    addAndMakeVisible (releaseAllButton);
    addAndMakeVisible (volumeRibbon);
    addAndMakeVisible (atOffsetRibbon);
    addAndMakeVisible (slideOffsetRibbon);
    addAndMakeVisible (pitchOffsetRibbon);
    addAndMakeVisible (keyboard);

    rebuildDeviceList();
    outputCombo.onChange = [this] { handleDeviceSelected(); };

    releaseAllButton.onClick = [this] { engine.allNotesOff(); };

    volumeRibbon.onValueChanged = [this] (float value) { engine.setGlobalVolume (value); };

    // Bipolar: Mitte (value01 == 0.5) -> Offset 0, oben -> +1, unten -> -1.
    atOffsetRibbon.onValueChanged = [this] (float value) { engine.setPressureOffset ((value - 0.5f) * 2.0f); };
    slideOffsetRibbon.onValueChanged = [this] (float value) { engine.setSlideOffset ((value - 0.5f) * 2.0f); };

    // Bipolar: Mitte -> 0 HT, oben/unten -> ±kPitchBendOffsetSemitones.
    pitchOffsetRibbon.onValueChanged = [this] (float value)
    {
        engine.setPitchBendOffset ((value - 0.5f) * 2.0f * kPitchBendOffsetSemitones);
    };
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

    auto topRow = bounds.removeFromTop (32);
    outputCombo.setBounds (topRow.removeFromLeft (200).reduced (8, 4));
    releaseAllButton.setBounds (topRow.removeFromRight (120).reduced (8, 4));

    constexpr int ribbonWidth = 72;

    // Links: Volume, dann Pressure/AT-Offset. Rechts: PitchBend-Offset außen,
    // dann Slide-Offset -- beide Gruppen wachsen vom Rand zur Mitte.
    volumeRibbon.setBounds      (bounds.removeFromLeft  (ribbonWidth));
    atOffsetRibbon.setBounds    (bounds.removeFromLeft  (ribbonWidth));
    pitchOffsetRibbon.setBounds (bounds.removeFromRight (ribbonWidth));
    slideOffsetRibbon.setBounds (bounds.removeFromRight (ribbonWidth));
    keyboard.setBounds (bounds);
}

} // namespace conduit
