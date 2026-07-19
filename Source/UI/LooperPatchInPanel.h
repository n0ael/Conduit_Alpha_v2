#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Modules/LooperPatchInModule.h"

namespace conduit
{

//==============================================================================
/**
    Bedien-Panel eines LooperPatchInModule (ADR 010) — lebt im NodeComponent
    (Muster LinkAudioSendPanel).

    Pro Slot eine Zeile: Name (Doppelklick → inputUserName; leer = zurück
    zum Auto-Namen), Mono/Stereo-Badge, ×-Button (GraphManager::
    removeLooperPatchInSlot, undo-fähig, inkl. Kabel-Remap). Footer: „+ Mono" /
    „+ Stereo" (addLooperPatchInSlot) — Slot-Änderungen re-materialisieren den
    Node im nächsten gefadeten Swap; die Zeilen folgen dem ValueTree.

    Bindung NUR an den Subtree (5.3); Namen schreiben inputUserName ohne
    Undo (Namens-Pflege wie beim Link-Send), Struktur-Änderungen laufen
    durch die GraphManager-Patch-Aktionen.
*/
class LooperPatchInPanel final : public juce::Component,
                                 private juce::ValueTree::Listener
{
public:
    LooperPatchInPanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse);
    ~LooperPatchInPanel() override;

    /** Teardown-Hook (Phase 1, 5.3): Interaktion sofort stoppen. */
    void stopUpdates();

    /** Höhe der Kachel-Innenfläche für n Slots (NodeComponent-Sizing). */
    [[nodiscard]] static int heightForInputs (int numInputs) noexcept
    {
        return topPadding + juce::jmax (1, numInputs) * rowHeight + footerHeight;
    }

    /** Vertikale Mitte der Slot-Zeile rowIndex (panel-lokal) — die Ports
        des NodeComponent fluchten damit horizontal mit ihren Zeilen. */
    [[nodiscard]] static int rowCentreY (int rowIndex) noexcept
    {
        return topPadding + rowIndex * rowHeight + rowHeight / 2;
    }

    [[nodiscard]] int getNumRows() const noexcept { return (int) rows.size(); }

    void resized() override;
    void paint (juce::Graphics& g) override;

    //==========================================================================
    // Zeile eines Slots — Controls public, UI-Tests treiben sie direkt
    struct InputRow
    {
        juce::String inputId;
        int index = 0;
        bool stereo = false;
        juce::Label nameLabel;
        juce::TextButton removeButton { juce::String::fromUTF8 ("\xc3\x97") };  // ×
    };

    std::vector<std::unique_ptr<InputRow>> rows;
    juce::TextButton addMonoButton   { "+ Mono" };
    juce::TextButton addStereoButton { "+ Stereo" };

private:
    // juce::ValueTree::Listener [Message Thread] — Slot-Struktur/Namen
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    void buildRows();
    void refreshNameLabel (int rowIndex);
    [[nodiscard]] juce::ValueTree inputsTree() const;
    [[nodiscard]] juce::Rectangle<int> rowBounds (int index) const;

    static constexpr int rowHeight    = 30;
    static constexpr int footerHeight = 34;
    static constexpr int topPadding   = 6;

    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;
    const juce::String nodeUuid;
    bool frozen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPatchInPanel)
};

} // namespace conduit
