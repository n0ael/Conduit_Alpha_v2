#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GridVoiceEngine.h"
#include "Core/MidiDeviceTarget.h"
#include "GridKeyboardComponent.h"

namespace conduit
{

//==============================================================================
/**
    Grid-Page (Ω, M1 Teil 3 — erster spielbarer Ton): GridKeyboardComponent
    plus ein minimales MIDI-Out-Port-Dropdown. Kein Feinschliff — reine
    Funktion, bis der Touch-Controller-Baukasten (CLAUDE.md 14 ADR) als
    eigener Meilenstein folgt.
*/
class GridPage final : public juce::Component
{
public:
    GridPage (grid::GridVoiceEngine& engine, grid::MidiDeviceTarget& midiTargetToUse);

    void resized() override;

private:
    void rebuildDeviceList();
    void handleDeviceSelected();

    grid::MidiDeviceTarget& midiTarget;
    juce::Array<juce::MidiDeviceInfo> devices;

    juce::ComboBox outputCombo;
    GridKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridPage)
};

} // namespace conduit
