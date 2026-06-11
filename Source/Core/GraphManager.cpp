#include "GraphManager.h"

#include <algorithm>
#include <vector>

#include "Modules/ModuleFactory.h"

namespace conduit
{

GraphManager::GraphManager (juce::ValueTree rootTree,
                            juce::AudioProcessorGraph& processorGraph,
                            GraphFader& faderToUse,
                            ModuleFactory& factoryToUse)
    : rootState (std::move (rootTree)),
      graph (processorGraph),
      fader (faderToUse),
      factory (factoryToUse)
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

    // Schritt 1 (Async Prepare): neue Module VOR dem Fade-Out instanziieren
    // und vorbereiten — speicherintensive Allokationen passieren hier,
    // nicht während der Stille.
    prepareNewModules();

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

//==============================================================================
void GraphManager::prepareNewModules()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

        if (nodeUuid.isEmpty()
            || treeToGraphNode.contains (nodeUuid)
            || preparedModules.contains (nodeUuid)
            || nodeTree.getProperty (id::nodeError).toString().isNotEmpty())
            continue;

        if (auto module = materializeModule (nodeTree))
            preparedModules[nodeUuid] = std::move (module);
    }
}

std::unique_ptr<ConduitModule> GraphManager::materializeModule (juce::ValueTree nodeTree)
{
    const auto moduleId = nodeTree.getProperty (id::moduleId).toString();
    auto module = factory.create (moduleId);

    if (module == nullptr)
    {
        nodeTree.setProperty (id::nodeError, "Unbekanntes Modul: " + moduleId, nullptr);
        return nullptr;
    }

    // Läuft Audio noch nicht, werden die Latenz-Ziele aus CLAUDE.md 3.2
    // angenommen — graph.prepareToPlay() re-prepariert später mit Ist-Werten.
    const auto sampleRate = graph.getSampleRate() > 0.0 ? graph.getSampleRate() : 48000.0;
    const auto blockSize  = graph.getBlockSize()  > 0   ? graph.getBlockSize()  : 32;

    if (const auto result = module->prepareForGraph (sampleRate, blockSize); result.failed())
    {
        // Kein Crash, kein Retry-Loop — UI zeigt den Fehlerzustand (5.2 Schritt 1)
        nodeTree.setProperty (id::nodeError, result.getErrorMessage(), nullptr);
        return nullptr;
    }

    return module;
}

//==============================================================================
void GraphManager::performTopologySwap()
{
    const auto coalescedChanges = pendingChangeCount;
    topologyDirty = false;
    pendingChangeCount = 0;
    ++rebuildCount;

    removeVanishedNodes();
    addNewNodes();
    syncConnections();

    // Übrig gebliebene Instanzen (Tree-Node wurde während des Fade-Outs
    // wieder entfernt) verwerfen
    preparedModules.clear();

    juce::Logger::writeToLog ("GraphManager: Graph-Swap #" + juce::String (rebuildCount)
                              + " (" + juce::String (coalescedChanges) + " Änderungen coalesced, "
                              + juce::String (graph.getNumNodes()) + " Graph-Nodes)");
}

void GraphManager::removeVanishedNodes()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (auto it = treeToGraphNode.begin(); it != treeToGraphNode.end();)
    {
        if (nodesTree.getChildWithProperty (id::nodeId, it->first).isValid())
        {
            ++it;
            continue;
        }

        graph.removeNode (it->second);  // entfernt auch alle Kabel dieses Nodes
        it = treeToGraphNode.erase (it);
    }
}

void GraphManager::addNewNodes()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

        if (nodeUuid.isEmpty()
            || treeToGraphNode.contains (nodeUuid)
            || nodeTree.getProperty (id::nodeError).toString().isNotEmpty())
            continue;

        std::unique_ptr<ConduitModule> module;

        if (const auto it = preparedModules.find (nodeUuid); it != preparedModules.end())
        {
            module = std::move (it->second);  // in Schritt 1 vorbereitet
            preparedModules.erase (it);
        }
        else
        {
            // Node kam erst während des Fade-Outs hinzu — jetzt vorbereiten
            module = materializeModule (nodeTree);
        }

        if (module == nullptr)
            continue;

        if (const auto graphNode = graph.addNode (std::move (module)))
            treeToGraphNode[nodeUuid] = graphNode->nodeID;
    }
}

void GraphManager::syncConnections()
{
    // Soll-Menge aus dem Tree (Schema 6.2). Kabel mit fehlendem Endpunkt
    // (z.B. Node mit nodeError) bleiben außen vor — sie werden beim
    // nächsten Swap erneut geprüft.
    std::vector<juce::AudioProcessorGraph::Connection> desired;
    const auto connectionsTree = rootState.getChildWithName (id::connections);

    for (int i = 0; i < connectionsTree.getNumChildren(); ++i)
    {
        const auto connection = connectionsTree.getChild (i);
        const auto source = treeToGraphNode.find (connection.getProperty (id::sourceNodeId).toString());
        const auto dest   = treeToGraphNode.find (connection.getProperty (id::destNodeId).toString());

        if (source == treeToGraphNode.end() || dest == treeToGraphNode.end())
            continue;

        desired.push_back ({ { source->second, (int) connection.getProperty (id::sourceChannel) },
                             { dest->second,   (int) connection.getProperty (id::destChannel) } });
    }

    // Ist-Zustand abgleichen: nur Kabel zwischen tree-verwalteten Nodes —
    // die I/O-Nodes des EngineProcessor bleiben unangetastet.
    for (const auto& existing : graph.getConnections())
    {
        if (! isManagedGraphNode (existing.source.nodeID) || ! isManagedGraphNode (existing.destination.nodeID))
            continue;

        if (std::find (desired.begin(), desired.end(), existing) == desired.end())
            graph.removeConnection (existing);
    }

    for (const auto& wanted : desired)
        if (! graph.isConnected (wanted) && ! graph.addConnection (wanted))
            juce::Logger::writeToLog ("GraphManager: ungültige Verbindung verworfen (Kanal außerhalb des Busses?)");
}

bool GraphManager::isManagedGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const
{
    return std::any_of (treeToGraphNode.begin(), treeToGraphNode.end(),
                        [nodeId] (const auto& entry) { return entry.second == nodeId; });
}

} // namespace conduit
