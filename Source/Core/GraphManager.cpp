#include "GraphManager.h"

#include <algorithm>
#include <vector>

#include "Interfaces/IClockSlave.h"
#include "Interfaces/IStochastic.h"
#include "Modules/ModuleFactory.h"
#include "NodeUiRegistry.h"

namespace conduit
{

GraphManager::GraphManager (juce::ValueTree rootTree,
                            juce::AudioProcessorGraph& processorGraph,
                            GraphFader& faderToUse,
                            ModuleFactory& factoryToUse,
                            juce::UndoManager& undoManagerToUse,
                            NodeUiRegistry& uiRegistryToUse)
    : rootState (std::move (rootTree)),
      graph (processorGraph),
      fader (faderToUse),
      factory (factoryToUse),
      undoManager (undoManagerToUse),
      uiRegistry (uiRegistryToUse)
{
    rootState.addListener (this);

    // Gibt die letzte UI-Component einen Deleting-Node frei, kann eine
    // wartende Phase 2 sofort weiterlaufen (5.3)
    uiRegistry.setOnNodeFullyReleased ([this] (const juce::String&) { triggerAsyncUpdate(); });
}

GraphManager::~GraphManager()
{
    uiRegistry.setOnNodeFullyReleased (nullptr);
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
ConduitModule* GraphManager::getModuleFor (const juce::String& nodeUuid) const
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (const auto it = treeToGraphNode.find (nodeUuid); it != treeToGraphNode.end())
        if (const auto graphNode = graph.getNodeForId (it->second))
            return dynamic_cast<ConduitModule*> (graphNode->getProcessor());

    return nullptr;
}

//==============================================================================
juce::ValueTree GraphManager::addModuleNode (const juce::String& factoryKey, juce::Point<int> position)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Instanz nur für createState() (4.4) — die Graph-Instanz erzeugt der
    // Swap später frisch über prepareNewModules().
    const auto module = factory.create (factoryKey);

    if (module == nullptr)
        return {};

    auto nodeTree = module->createState();
    nodeTree.setProperty (id::positionX, position.x, nullptr);
    nodeTree.setProperty (id::positionY, position.y, nullptr);

    // Eindeutige named_id (OSC-Pfad, 7) — vor dem Einhängen, ohne Listener
    nodeTree.setProperty (id::moduleId, makeUniqueModuleName (factoryKey), nullptr);

    undoManager.beginNewTransaction ("Modul hinzufügen");
    rootState.getChildWithName (id::nodes).appendChild (nodeTree, &undoManager);
    return nodeTree;
}

bool GraphManager::renameNode (const juce::String& nodeUuid, const juce::String& requestedName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid())
        return false;

    const auto sanitized = sanitizeModuleName (requestedName);

    if (sanitized.isEmpty())
        return false;

    if (sanitized == nodeTree.getProperty (id::moduleId).toString())
        return true;  // No-op

    if (isModuleNameTaken (sanitized))
        return false;

    // Rename ändert OSC-Pfade — patchbare Aktion, also undo-fähig
    undoManager.beginNewTransaction ("Modul umbenennen");
    nodeTree.setProperty (id::moduleId, sanitized, &undoManager);
    return true;
}

//==============================================================================
juce::String GraphManager::sanitizeModuleName (const juce::String& raw)
{
    // OSC-pfadtauglich (7): lowercase, [a-z0-9_]; Trenner werden '_'
    juce::String result;
    result.preallocateBytes (static_cast<size_t> (raw.length()));

    for (const auto character : raw.trim().toLowerCase())
    {
        if ((character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9') || character == '_')
            result << character;
        else if (character == ' ' || character == '-')
            result << '_';
    }

    return result;
}

bool GraphManager::isModuleNameTaken (const juce::String& name) const
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
        if (nodesTree.getChild (i).getProperty (id::moduleId).toString() == name)
            return true;

    return false;
}

juce::String GraphManager::makeUniqueModuleName (const juce::String& factoryKey) const
{
    for (int counter = 1;; ++counter)
        if (const auto candidate = factoryKey + "_" + juce::String (counter);
            ! isModuleNameTaken (candidate))
            return candidate;
}

juce::String GraphManager::factoryKeyOf (const juce::ValueTree& nodeTree)
{
    if (nodeTree.hasProperty (id::factoryId))
        return nodeTree.getProperty (id::factoryId).toString();

    return nodeTree.getProperty (id::moduleId).toString();  // Alt-Bestand
}

void GraphManager::normalizeNode (juce::ValueTree nodeTree)
{
    // Migration alter States: moduleId trug früher den Factory-Schlüssel
    if (! nodeTree.hasProperty (id::factoryId))
        nodeTree.setProperty (id::factoryId,
                              nodeTree.getProperty (id::moduleId).toString(), nullptr);
}

//==============================================================================
bool GraphManager::requestNodeDelete (const juce::String& nodeUuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid()
        || isExternalEndpoint (factoryKeyOf (nodeTree)))
        return false;

    // Phase 1: UI entkoppelt sich über ihren nodeState-Listener.
    // Bewusst ohne UndoManager — undo-fähig ist die Subtree-Entfernung
    // in Phase 2, nicht der Übergangszustand.
    nodeTree.setProperty (id::nodeState, toString (NodeState::deleting), nullptr);
    return true;
}

//==============================================================================
bool GraphManager::addConnection (const juce::String& sourceUuid, int sourceChannel,
                                  const juce::String& destUuid, int destChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto nodesTree = rootState.getChildWithName (id::nodes);

    if (sourceUuid == destUuid  // direkte Selbstverbindung wäre ein Graph-Zyklus
        || ! nodesTree.getChildWithProperty (id::nodeId, sourceUuid).isValid()
        || ! nodesTree.getChildWithProperty (id::nodeId, destUuid).isValid()
        || findConnectionTree (sourceUuid, sourceChannel, destUuid, destChannel).isValid())
        return false;

    juce::ValueTree connection (id::connection);
    connection.setProperty (id::sourceNodeId,  sourceUuid,    nullptr);
    connection.setProperty (id::sourceChannel, sourceChannel, nullptr);
    connection.setProperty (id::destNodeId,    destUuid,      nullptr);
    connection.setProperty (id::destChannel,   destChannel,   nullptr);

    undoManager.beginNewTransaction ("Kabel verbinden");
    rootState.getChildWithName (id::connections).appendChild (connection, &undoManager);
    return true;
}

bool GraphManager::removeConnection (const juce::String& sourceUuid, int sourceChannel,
                                     const juce::String& destUuid, int destChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto connection = findConnectionTree (sourceUuid, sourceChannel, destUuid, destChannel);

    if (! connection.isValid())
        return false;

    undoManager.beginNewTransaction ("Kabel trennen");
    rootState.getChildWithName (id::connections).removeChild (connection, &undoManager);
    return true;
}

juce::ValueTree GraphManager::findConnectionTree (const juce::String& sourceUuid, int sourceChannel,
                                                  const juce::String& destUuid, int destChannel) const
{
    const auto connectionsTree = rootState.getChildWithName (id::connections);

    for (int i = 0; i < connectionsTree.getNumChildren(); ++i)
    {
        const auto connection = connectionsTree.getChild (i);

        if (connection.getProperty (id::sourceNodeId).toString() == sourceUuid
            && (int) connection.getProperty (id::sourceChannel) == sourceChannel
            && connection.getProperty (id::destNodeId).toString() == destUuid
            && (int) connection.getProperty (id::destChannel) == destChannel)
            return connection;
    }

    return {};
}

//==============================================================================
void GraphManager::registerExternalEndpoint (const juce::String& moduleId,
                                             juce::AudioProcessorGraph::NodeID graphNodeId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    externalEndpoints[moduleId] = graphNodeId;
}

bool GraphManager::isExternalEndpoint (const juce::String& moduleId) const noexcept
{
    return externalEndpoints.contains (moduleId);
}

bool GraphManager::isExternalGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const
{
    return std::any_of (externalEndpoints.begin(), externalEndpoints.end(),
                        [nodeId] (const auto& entry) { return entry.second == nodeId; });
}

void GraphManager::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

//==============================================================================
bool GraphManager::isTopologyContainer (const juce::ValueTree& tree) noexcept
{
    return tree.hasType (id::nodes) || tree.hasType (id::connections);
}

void GraphManager::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Phase 1 des zweiphasigen Deletes (5.3): moduleId wird JETZT gecacht,
    // nicht erst bei valueTreeChildRemoved — der künftige OscController
    // deregistriert seine OSC-Adressen darüber (7.1).
    if (property == id::nodeState && tree.hasType (id::node)
        && tree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
    {
        pendingDeletes[tree.getProperty (id::nodeId).toString()]
            = tree.getProperty (id::moduleId).toString();
        triggerAsyncUpdate();
        return;
    }

    // Tree → Atomic: paramValue-Änderungen (UI-Slider, OSC-Nachzug, Undo,
    // Preset-Load) auf das Echtzeit-Target spiegeln — KEIN Rebuild.
    if (property == id::paramValue && tree.hasType (id::parameter))
    {
        syncParameterValue (tree);
        return;
    }

    // Sonst bewusst keine Reaktion — Topologie ändert sich hier nicht,
    // ein Graph-Rebuild wäre falsch (6.1).
}

void GraphManager::syncParameterValue (juce::ValueTree parameterTree)
{
    // Parameter → Parameters[] → Node (Schema 6.2)
    const auto nodeTree = parameterTree.getParent().getParent();

    if (! nodeTree.hasType (id::node))
        return;

    auto* module = getModuleFor (nodeTree.getProperty (id::nodeId).toString());

    if (module == nullptr)
        return;  // noch nicht materialisiert — addNewNodes() spiegelt initial

    if (auto* target = module->getParameterTarget (parameterTree.getProperty (id::paramId).toString()))
    {
        const auto minValue = static_cast<float> ((double) parameterTree.getProperty (id::paramMin, 0.0));
        const auto maxValue = static_cast<float> ((double) parameterTree.getProperty (id::paramMax, 1.0));
        const auto value    = static_cast<float> ((double) parameterTree.getProperty (id::paramValue, 0.0));

        target->store (juce::jlimit (minValue, maxValue, value), std::memory_order_relaxed);
    }
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
    // Phase 2 ausstehender Deletes (5.3): entfernt freigegebene Subtrees
    // und markiert dadurch topologyDirty — der Swap unten nimmt sie mit.
    processPendingDeletes();

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
void GraphManager::processPendingDeletes()
{
    if (pendingDeletes.empty())
        return;

    auto nodesTree       = rootState.getChildWithName (id::nodes);
    auto connectionsTree = rootState.getChildWithName (id::connections);

    for (auto it = pendingDeletes.begin(); it != pendingDeletes.end();)
    {
        const auto& nodeUuid = it->first;

        // Zombie-UI-Schutz: Phase 2 startet erst, wenn keine UI-Component
        // mehr eine Referenz auf diesen Subtree hält (5.3). Die Freigabe
        // stößt via NodeUiRegistry-Callback den nächsten Durchlauf an.
        if (uiRegistry.getRefCount (nodeUuid) > 0)
        {
            ++it;
            continue;
        }

        if (auto nodeTree = nodesTree.getChildWithProperty (id::nodeId, nodeUuid); nodeTree.isValid())
        {
            // Node und seine Kabel in EINER UndoManager-Transaktion —
            // ein Undo stellt alles wieder her und löst einen einzigen
            // gemeinsamen Swap aus (5.5)
            undoManager.beginNewTransaction ("Modul löschen");

            for (int i = connectionsTree.getNumChildren(); --i >= 0;)
            {
                const auto connection = connectionsTree.getChild (i);

                if (connection.getProperty (id::sourceNodeId).toString() == nodeUuid
                    || connection.getProperty (id::destNodeId).toString() == nodeUuid)
                    connectionsTree.removeChild (i, &undoManager);
            }

            nodesTree.removeChild (nodeTree, &undoManager);
        }

        it = pendingDeletes.erase (it);
    }
}

//==============================================================================
void GraphManager::prepareNewModules()
{
    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

        normalizeNode (nodeTree);  // Migration vor jeder Auswertung

        if (nodeUuid.isEmpty()
            || treeToGraphNode.contains (nodeUuid)
            || preparedModules.contains (nodeUuid)
            || isExternalEndpoint (factoryKeyOf (nodeTree))
            || nodeTree.getProperty (id::nodeError).toString().isNotEmpty())
            continue;

        if (auto module = materializeModule (nodeTree))
            preparedModules[nodeUuid] = std::move (module);
    }
}

std::unique_ptr<ConduitModule> GraphManager::materializeModule (juce::ValueTree nodeTree)
{
    const auto factoryKey = factoryKeyOf (nodeTree);
    auto module = factory.create (factoryKey);

    if (module == nullptr)
    {
        nodeTree.setProperty (id::nodeError, "Unbekanntes Modul: " + factoryKey, nullptr);
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

    // Takt-Injektion VOR der Graph-Aufnahme (IClockSlave, 4.2) — danach
    // läuft processBlock und der Pointer darf sich nicht mehr ändern
    if (auto* clockSlave = dynamic_cast<IClockSlave*> (module.get()))
        clockSlave->setClockBus (clockBus);

    // Seed-Injektion (IStochastic, 4.2): deterministisch aus der nodeUuid —
    // Zufalls-Patterns sind damit pro Node reproduzierbar
    if (auto* stochastic = dynamic_cast<IStochastic*> (module.get()))
        stochastic->setRandomSeed (static_cast<std::uint64_t> (
            nodeTree.getProperty (id::nodeId).toString().hashCode64()));

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

        // Externe Endpunkte (I/O des EngineProcessor) gehören nicht uns —
        // nur das Mapping lösen, den Graph-Node niemals zerstören
        if (! isExternalGraphNode (it->second))
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

        normalizeNode (nodeTree);

        if (nodeUuid.isEmpty()
            || treeToGraphNode.contains (nodeUuid)
            || nodeTree.getProperty (id::nodeError).toString().isNotEmpty())
            continue;

        // Externer Endpunkt: nur das Mapping setzen — kein Factory-Modul
        if (const auto factoryKey = factoryKeyOf (nodeTree);
            isExternalEndpoint (factoryKey))
        {
            treeToGraphNode[nodeUuid] = externalEndpoints[factoryKey];
            nodeTree.setProperty (id::nodeState, toString (NodeState::active), nullptr);
            continue;
        }

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
        {
            treeToGraphNode[nodeUuid] = graphNode->nodeID;

            // Lifecycle-Status zurücksetzen — relevant nach Undo eines
            // Deletes: der Subtree wurde mit nodeState == Deleting restauriert
            nodeTree.setProperty (id::nodeState, toString (NodeState::active), nullptr);

            // Persistierte Parameter auf die frischen Atomic-Targets spiegeln
            // (Preset-Load, Undo-Restore — Tree ist Single Source of Truth)
            const auto parameters = nodeTree.getChildWithName (id::parameters);

            for (int p = 0; p < parameters.getNumChildren(); ++p)
                syncParameterValue (parameters.getChild (p));
        }
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
