#include "GraphManager.h"

namespace conduit
{

GraphManager::GraphManager (juce::ValueTree rootTree, juce::AudioProcessorGraph& processorGraph)
    : rootState (std::move (rootTree)),
      graph (processorGraph)
{
    rootState.addListener (this);
}

GraphManager::~GraphManager()
{
    rootState.removeListener (this);
    cancelPendingUpdate();  // AsyncUpdater darf nie auf ein zerstörtes Objekt feuern
}

//==============================================================================
bool GraphManager::isTopologyDirty() const noexcept { return topologyDirty; }
int GraphManager::getRebuildCount() const noexcept  { return rebuildCount; }

void GraphManager::flushPendingTopologyUpdate()
{
    handleUpdateNowIfNeeded();
}

//==============================================================================
bool GraphManager::isTopologyContainer (const juce::ValueTree& tree) noexcept
{
    return tree.hasType (id::nodes) || tree.hasType (id::connections);
}

void GraphManager::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    // Bewusst leer: Parameter-Updates kommen via OSC-Dual-State (6.1) im
    // Millisekundentakt — sie ändern die Topologie nicht und dürfen keinen
    // Graph-Rebuild auslösen.
    // Spätere Ausbaustufe: nodeState → Deleting (5.3) wird hier behandelt.
}

void GraphManager::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    // parent: Node/Connection in einen Container eingefügt.
    // child:  ganzer Container ersetzt (Preset-Load via
    //         copyPropertiesAndChildrenFrom hängt Nodes[]/Connections[]
    //         als Subtree an den Root — parent ist dann der Root).
    if (isTopologyContainer (parent) || isTopologyContainer (child))
        markTopologyDirty();
}

void GraphManager::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (isTopologyContainer (parent) || isTopologyContainer (child))
        markTopologyDirty();
}

void GraphManager::valueTreeRedirected (juce::ValueTree&)
{
    // Kompletter Tree-Austausch → ein voller Rebuild.
    markTopologyDirty();
}

void GraphManager::markTopologyDirty()
{
    // ValueTree-Mutationen sind nur auf dem Message Thread erlaubt (CLAUDE.md 6)
    JUCE_ASSERT_MESSAGE_THREAD

    topologyDirty = true;
    ++pendingChangeCount;
    triggerAsyncUpdate();  // coalesct mehrfache Trigger bis zum nächsten Loop-Durchlauf
}

void GraphManager::handleAsyncUpdate()
{
    if (! topologyDirty)
        return;

    const auto coalescedChanges = pendingChangeCount;
    topologyDirty = false;
    pendingChangeCount = 0;
    ++rebuildCount;

    // Platzhalter für den kombinierten Graph-Swap (5.2):
    // Fade-Out → Topologie-Swap → Fade-In, EIN Zyklus für das gesamte Delta.
    juce::Logger::writeToLog ("GraphManager: kombinierter Graph-Rebuild für Frame-Delta ("
                              + juce::String (coalescedChanges) + " Änderungen coalesced)");
}

} // namespace conduit
