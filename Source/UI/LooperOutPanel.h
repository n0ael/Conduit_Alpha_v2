#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Modules/LooperOutModule.h"

namespace conduit
{

//==============================================================================
/**
    Bedien-Panel eines LooperOutModule (ADR 010) — lebt im NodeComponent
    (Muster LooperInPanel).

    Pro Abgriff eine Zeile: Label ("Master", "Looper 2 · L", …), PRE-Toggle
    (GraphManager::setLooperOutSlotPre — re-materialisiert gefadet) und
    ×-Button (removeLooperOutSlot, inkl. Kabel-Remap). Footer: „+"-Button
    mit PopupMenu (Ziel Master/Looper 1–4 × Modus Stereo/Summe/L/R).

    Bindung NUR an den Subtree (5.3); alle Struktur-Änderungen laufen als
    undo-fähige GraphManager-Patch-Aktionen.
*/
class LooperOutPanel final : public juce::Component,
                             private juce::ValueTree::Listener
{
public:
    LooperOutPanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse);
    ~LooperOutPanel() override;

    /** Teardown-Hook (Phase 1, 5.3). */
    void stopUpdates();

    /** Höhe der Kachel-Innenfläche für n Abgriffe (NodeComponent-Sizing). */
    [[nodiscard]] static int heightForOutputs (int numOutputs) noexcept
    {
        return topPadding + juce::jmax (1, numOutputs) * rowHeight + footerHeight;
    }

    /** Vertikale Mitte der Abgriff-Zeile rowIndex (panel-lokal) — die
        Ports des NodeComponent fluchten damit horizontal. */
    [[nodiscard]] static int rowCentreY (int rowIndex) noexcept
    {
        return topPadding + rowIndex * rowHeight + rowHeight / 2;
    }

    [[nodiscard]] int getNumRows() const noexcept { return (int) rows.size(); }

    void resized() override;
    void paint (juce::Graphics& g) override;

    //==========================================================================
    struct OutputRow
    {
        int index = 0;
        juce::Label label;
        juce::TextButton preButton { "PRE" };
        juce::TextButton removeButton { juce::String::fromUTF8 ("\xc3\x97") };  // ×
    };

    std::vector<std::unique_ptr<OutputRow>> rows;
    juce::TextButton addButton { "+" };

private:
    // juce::ValueTree::Listener [Message Thread] — Slot-Struktur/Pre-Flag
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    void buildRows();
    void showAddMenu();
    [[nodiscard]] juce::ValueTree outputsTree() const;
    [[nodiscard]] juce::Rectangle<int> rowBounds (int index) const;

    static constexpr int rowHeight    = 30;
    static constexpr int footerHeight = 34;
    static constexpr int topPadding   = 6;

    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;
    const juce::String nodeUuid;
    bool frozen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperOutPanel)
};

} // namespace conduit
