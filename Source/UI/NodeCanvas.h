#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "UI/NodeComponent.h"

namespace conduit
{

//==============================================================================
/**
    Patching-Fläche: spiegelt Nodes[] des Root-Trees als NodeComponents —
    rein ValueTree-getrieben (read/listen + Patch-Aktionen über den
    GraphManager), analog zum Topologie-Sync der Engine.

    Sync-Regeln:
      - childAdded (Node)        → Component erzeugen (außer nodeState == Deleting)
      - nodeState → Active       → Component nachziehen (Undo-Restore: der
                                   Subtree kommt mit nodeState == Deleting zurück,
                                   erst der Swap setzt ihn auf Active)
      - childRemoved (Node)      → Component sofort zerstören (Fallback: Redo
                                   eines Deletes/Preset-Load entfernen ohne
                                   Deleting-Phase)
      - Container-Austausch /
        redirected (Preset-Load) → Full-Rebuild

    Doppelklick/Doppel-Tap auf freie Fläche legt ein Modul an der Klickposition
    an (bis zur Modul-Palette fest: Attenuator).
*/
class NodeCanvas final : public juce::Component,
                         private juce::ValueTree::Listener
{
public:
    NodeCanvas (juce::ValueTree rootTree,
                GraphManager& graphManagerToUse,
                NodeUiRegistry& uiRegistryToUse);
    ~NodeCanvas() override;

    [[nodiscard]] int getNumNodeComponents() const noexcept;
    [[nodiscard]] NodeComponent* findNodeComponent (const juce::String& nodeUuid) const;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread]
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    void rebuildAll();
    void addComponentFor (juce::ValueTree nodeTree);
    void removeComponentFor (const juce::String& nodeUuid);

    //==========================================================================
    juce::ValueTree rootState;  // ref-counted Handle
    GraphManager& graphManager;
    NodeUiRegistry& uiRegistry;

    std::vector<std::unique_ptr<NodeComponent>> nodeComponents;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeCanvas)
};

} // namespace conduit
