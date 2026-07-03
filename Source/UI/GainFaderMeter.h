#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"

namespace conduit
{

class LevelMeter;

//==============================================================================
/**
    Ableton-artiger Kanalzug fürs FX-Chassis (CLAUDE.md 4.6): vertikaler
    Gain-Fader (dB) mit integriertem Stereo-Level-Meter und dB-Skala —
    je eine Instanz für input_gain und output_gain im FxModulePanel.

    Bindung (5.3): schreibt NUR paramValue in den Parameter-Subtree, ohne
    UndoManager (Sweeps sind keine patchbaren Aktionen); externe Änderungen
    (OSC-Nachzug, Undo, Preset-Load) kommen über den ValueTree-Listener.

    Meter (Zombie-UI-Regel): die LevelMeter-Instanz lebt IM Modul — hier wird
    NIE ein Pointer gecached, sondern pro 30-fps-Tick transient über
    GraphManager::getModuleFor() aufgelöst (Muster ScopeDisplay/StatusBadge);
    nullptr (Deleting/Preset-Load/Tests) → leerer Track, kein Crash.

    Klick auf das Clip-Feld (oben) setzt den Clip-Latch beider Kanäle zurück.
*/
class GainFaderMeter final : public juce::Component,
                             private juce::Timer,
                             private juce::ValueTree::Listener
{
public:
    /** useInputMeter: true → getInputMeter() des Chassis, false → Output. */
    GainFaderMeter (juce::ValueTree nodeTreeToBind, juce::String gainParamId,
                    GraphManager& graphManagerToUse, bool useInputMeter);
    ~GainFaderMeter() override;

    /** Teardown-Hook (Phase 1, 5.3): Timer + Interaktion sofort stoppen. */
    void stopUpdates();

    static constexpr int preferredWidth = 60;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

    //==========================================================================
    // Controls public — UI-Tests treiben sie direkt (Muster ParameterPanel)
    juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };

    [[nodiscard]] const juce::String& getParamId() const noexcept { return paramId; }

private:
    //==========================================================================
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    /** Transiente Auflösung pro Tick — Pointer nie über den Tick hinaus halten. */
    [[nodiscard]] const LevelMeter* resolveMeter() const;

    [[nodiscard]] juce::ValueTree paramTree() const;
    [[nodiscard]] juce::Rectangle<float> meterArea() const;
    [[nodiscard]] juce::Rectangle<float> clipZone() const;

    //==========================================================================
    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    const juce::String paramId;
    GraphManager& graphManager;
    const juce::String nodeUuid;
    const bool isInputMeter;

    // Zwischengespeicherte Meter-Werte (Change-Detection, 2 Kanäle)
    float lastRms[2] {}, lastPeak[2] {}, lastHold[2] {};
    bool lastClipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainFaderMeter)
};

} // namespace conduit
