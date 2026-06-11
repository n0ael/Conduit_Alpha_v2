#include "GraphManager.h"

namespace conduit
{

GraphManager::GraphManager (juce::ValueTree rootTree,
                            juce::AudioProcessorGraph& processorGraph,
                            GraphFader& faderToUse)
    : rootState (std::move (rootTree)),
      graph (processorGraph),
      fader (faderToUse)
{
    rootState.addListener (this);
}

GraphManager::~GraphManager()
{
    rootState.removeListener (this);
    cancelPendingUpdate();  // AsyncUpdater darf nie auf ein zerstörtes Objekt feuern
}

//==============================================================================
bool GraphManager::isTopologyDirty() const noexcept     { return topologyDirty; }
bool GraphManager::isWaitingForSilence() const noexcept { return swapPhase == SwapPhase::waitingForSilence; }
int GraphManager::getRebuildCount() const noexcept      { return rebuildCount; }

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

//==============================================================================
void GraphManager::handleAsyncUpdate()
{
    if (swapPhase == SwapPhase::waitingForSilence)
    {
        // Schritt 3: kein Busy-Poll, kein Timer — Self-Re-Dispatch bis der
        // Audio Thread Stille meldet. Stoppt Audio währenddessen (Fader
        // unprepared), wird ohne Fade direkt geswappt.
        if (fader.isPrepared() && ! fader.isFadeOutComplete())
        {
            triggerAsyncUpdate();
            return;
        }

        performTopologySwap();  // Topologie-Swap auf Stille
        fader.beginFadeIn();    // Schritt 4
        swapPhase = SwapPhase::idle;
        return;
    }

    if (! topologyDirty)
        return;

    if (! fader.isPrepared())
    {
        // Audio läuft nicht — Fade wäre wirkungslos, direkt swappen.
        performTopologySwap();
        return;
    }

    // Schritt 2: Fade-Out anstoßen, die Graph-Topologie bleibt unverändert.
    // Änderungen, die während des Fade-Outs eintreffen, erhöhen nur das
    // Delta und landen im selben Swap (5.5).
    fader.beginFadeOut();
    swapPhase = SwapPhase::waitingForSilence;
    triggerAsyncUpdate();
}

void GraphManager::performTopologySwap()
{
    const auto coalescedChanges = pendingChangeCount;
    topologyDirty = false;
    pendingChangeCount = 0;
    ++rebuildCount;

    // Platzhalter für die echten Graph-Mutationen aus dem ValueTree-Delta:
    // addNode() / addConnection() / removeConnection() — nächste Ausbaustufe.
    juce::Logger::writeToLog ("GraphManager: kombinierter Graph-Rebuild für Frame-Delta ("
                              + juce::String (coalescedChanges) + " Änderungen coalesced)");
}

} // namespace conduit
