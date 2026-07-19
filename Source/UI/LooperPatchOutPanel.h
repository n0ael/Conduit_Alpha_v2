#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Modules/LooperPatchOutModule.h"

namespace conduit
{

//==============================================================================
/**
    Anzeige-Panel eines LooperPatchOutModule — lebt im NodeComponent
    (Muster LooperPatchInPanel-Zeilenraster), aber READ-ONLY: die Slots folgen automatisch
    der Looper-Struktur (syncLooperPatchOutConfigs), es gibt nichts zu
    bedienen. Pro Slot eine Label-Zeile; dünne Trenner zwischen den
    Sektionen (Tracks | Busse | Sends | Master).

    Bindung NUR an den Subtree (5.3).
*/
class LooperPatchOutPanel final : public juce::Component,
                                  private juce::ValueTree::Listener
{
public:
    explicit LooperPatchOutPanel (juce::ValueTree nodeTreeToBind);
    ~LooperPatchOutPanel() override;

    /** Teardown-Hook (Phase 1, 5.3). */
    void stopUpdates();

    /** Höhe der Kachel-Innenfläche für n Slots (NodeComponent-Sizing) —
        ohne Footer (keine Bedien-Buttons). */
    [[nodiscard]] static int heightForOutputs (int numOutputs) noexcept
    {
        return topPadding + juce::jmax (1, numOutputs) * rowHeight + bottomPadding;
    }

    /** Vertikale Mitte der Slot-Zeile rowIndex (panel-lokal) — die Ports
        des NodeComponent fluchten damit horizontal. */
    [[nodiscard]] static int rowCentreY (int rowIndex) noexcept
    {
        return topPadding + rowIndex * rowHeight + rowHeight / 2;
    }

    [[nodiscard]] int getNumRows() const noexcept { return (int) specs.size(); }

    void paint (juce::Graphics& g) override;

private:
    // juce::ValueTree::Listener [Message Thread] — Slot-Struktur
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    void rebuildSpecs();

    static constexpr int rowHeight     = 30;
    static constexpr int topPadding    = 6;
    static constexpr int bottomPadding = 6;

    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    std::vector<LooperPatchOutModule::OutputSpec> specs;
    bool frozen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPatchOutPanel)
};

} // namespace conduit
