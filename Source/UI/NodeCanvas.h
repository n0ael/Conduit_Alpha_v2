#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "UI/NodeComponent.h"
#include "UI/PortComponent.h"

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
    /** channelNamesToUse darf nullptr sein (Tests) — die NodeComponents
        der I/O-Endpunkte zeigen dann keine Port-Labels. inputLevels/
        outputLevels (nullptr in Tests) speisen die Pegelanzeigen der
        I/O-Endpunkte. */
    NodeCanvas (juce::ValueTree rootTree,
                GraphManager& graphManagerToUse,
                NodeUiRegistry& uiRegistryToUse,
                ChannelNames* channelNamesToUse = nullptr,
                LevelMeter* inputLevelsToUse = nullptr,
                LevelMeter* outputLevelsToUse = nullptr);
    ~NodeCanvas() override;

    [[nodiscard]] int getNumNodeComponents() const noexcept;
    [[nodiscard]] NodeComponent* findNodeComponent (const juce::String& nodeUuid) const;

    //==========================================================================
    // Kabel-Gesten — aufgerufen von den PortComponents (Canvas-Koordinaten)
    void beginCableDrag (const PortInfo& fromPort, juce::Point<int> position);
    void updateCableDrag (juce::Point<int> position);
    void endCableDrag (juce::Point<int> position);

    /** Kabel unter dem Punkt (Toleranz ~8px), invalides ValueTree wenn keins.
        Public für Tests. */
    [[nodiscard]] juce::ValueTree findConnectionAt (juce::Point<int> position) const;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
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

    [[nodiscard]] std::optional<juce::Point<int>> getPortCentreInCanvas (const juce::String& nodeUuid,
                                                                         bool isInput, int channel) const;
    [[nodiscard]] static juce::Path makeCablePath (juce::Point<float> start, juce::Point<float> end);

    //==========================================================================
    juce::ValueTree rootState;  // ref-counted Handle
    GraphManager& graphManager;
    NodeUiRegistry& uiRegistry;
    ChannelNames* channelNames;  // nullptr in Tests
    LevelMeter* inputLevels;     // Sicht-Metering audio_in (nullptr in Tests)
    LevelMeter* outputLevels;    // Sicht-Metering audio_out (nullptr in Tests)

    std::vector<std::unique_ptr<NodeComponent>> nodeComponents;

    struct CableDrag
    {
        PortInfo from;
        juce::Point<int> currentPosition;
    };

    std::optional<CableDrag> activeCableDrag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeCanvas)
};

} // namespace conduit
