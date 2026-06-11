#include "OscController.h"

#include "GraphManager.h"
#include "Modules/ConduitModule.h"

namespace conduit
{

OscController::OscController (juce::ValueTree rootTree,
                              GraphManager& graphManagerToUse,
                              SpscQueue<ParameterUpdate>& audioQueueToUse)
    : rootState (std::move (rootTree)),
      graphManager (graphManagerToUse),
      audioQueue (audioQueueToUse)
{
    rootState.addListener (this);
    receiver.addListener (this);
    triggerAsyncUpdate();  // initiale Registry (Tree kann schon Nodes tragen)
}

OscController::~OscController()
{
    disconnect();
    receiver.removeListener (this);
    rootState.removeListener (this);
    cancelPendingUpdate();  // AsyncUpdater darf nie auf ein zerstörtes Objekt feuern
}

//==============================================================================
bool OscController::connect (int udpPort)
{
    JUCE_ASSERT_MESSAGE_THREAD
    return receiver.connect (udpPort);
}

void OscController::disconnect()
{
    receiver.disconnect();
}

bool OscController::isStateDirty() const noexcept
{
    return stateDirty.load (std::memory_order_acquire);
}

void OscController::flushPendingUpdates()
{
    JUCE_ASSERT_MESSAGE_THREAD
    handleUpdateNowIfNeeded();
}

juce::StringArray OscController::getRegisteredAddresses() const
{
    juce::StringArray addresses;

    const juce::ScopedLock lock (registryLock);

    for (const auto& [address, endpoint] : endpoints)
    {
        juce::ignoreUnused (endpoint);
        addresses.add (address);
    }

    return addresses;
}

//==============================================================================
void OscController::oscMessageReceived (const juce::OSCMessage& message)
{
    // Netzwerk-Thread (CLAUDE.md 7) — keine ValueTree-Zugriffe, kein Kontakt
    // zum Audio Thread außer dem lock-freien Queue-Push.
    if (message.size() < 1)
        return;

    float rawValue = 0.0f;

    if (message[0].isFloat32())
        rawValue = message[0].getFloat32();
    else if (message[0].isInt32())
        rawValue = static_cast<float> (message[0].getInt32());
    else
        return;  // unbekannter Argument-Typ — ignorieren, kein Crash

    Endpoint endpoint;

    {
        const juce::ScopedLock lock (registryLock);
        const auto it = endpoints.find (message.getAddressPattern().toString());

        if (it == endpoints.end())
            return;  // unregistrierte Adresse — ignorieren

        endpoint = it->second;
    }

    const auto clamped = juce::jlimit (endpoint.minValue, endpoint.maxValue, rawValue);

    // Pfad 1: sofort in den Audio Thread (lock-free, < 1ms). Volle Queue →
    // Update verworfen; der Tree-Pfad unten trägt den Wert trotzdem nach.
    if (endpoint.target != nullptr)
        audioQueue.push ({ endpoint.target, clamped });

    // Pfad 2: async in den ValueTree — UI + Serialisierung folgen nach (6.1)
    {
        const juce::ScopedLock lock (treeUpdateLock);
        pendingTreeUpdates.push_back ({ endpoint.nodeUuid, endpoint.parameterId, clamped });
    }

    stateDirty.store (true, std::memory_order_release);  // Serialisierung muss warten
    triggerAsyncUpdate();
}

//==============================================================================
void OscController::handleAsyncUpdate()
{
    // stateDirty VOR dem Drain zurücksetzen: eine Message, die während
    // applyTreeUpdates() eintrifft, setzt es danach wieder — so geht beim
    // Preset-Save (6.1) nie ein Wert verloren (schlimmstenfalls ein
    // überflüssiger Flush).
    stateDirty.store (false, std::memory_order_release);

    applyTreeUpdates();

    if (registryDirty)
        rebuildEndpoints();
}

void OscController::applyTreeUpdates()
{
    std::vector<TreeUpdate> updates;

    {
        const juce::ScopedLock lock (treeUpdateLock);
        updates.swap (pendingTreeUpdates);
    }

    auto nodesTree = rootState.getChildWithName (id::nodes);

    for (const auto& update : updates)
    {
        const auto nodeTree = nodesTree.getChildWithProperty (id::nodeId, update.nodeUuid);

        if (! nodeTree.isValid())
            continue;  // Node wurde inzwischen entfernt

        auto parameter = nodeTree.getChildWithName (id::parameters)
                             .getChildWithProperty (id::paramId, update.parameterId);

        if (parameter.isValid())
            parameter.setProperty (id::paramValue, update.value, nullptr);
    }
}

//==============================================================================
void OscController::rebuildEndpoints()
{
    JUCE_ASSERT_MESSAGE_THREAD

    registryDirty = false;

    std::map<juce::String, Endpoint> rebuilt;
    bool unresolvedModuleRemaining = false;

    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        const auto nodeTree = nodesTree.getChild (i);
        const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();
        const auto moduleId = nodeTree.getProperty (id::moduleId).toString();

        if (nodeUuid.isEmpty() || moduleId.isEmpty())
            continue;

        // Phase 1 des zweiphasigen Deletes (5.3): Deleting-Nodes sind ab
        // sofort deregistriert — nicht erst bei valueTreeChildRemoved (7.1)
        if (nodeTree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
            continue;

        auto* module = graphManager.getModuleFor (nodeUuid);

        if (module == nullptr
            && nodeTree.getProperty (id::nodeError).toString().isEmpty())
            unresolvedModuleRemaining = true;  // materialisiert erst nach dem Swap

        const auto addressPrefix = "/conduit/"
                                   + nodeTree.getProperty (id::type).toString().toLowerCase()
                                   + "/" + moduleId + "/";

        const auto parameters = nodeTree.getChildWithName (id::parameters);

        for (int p = 0; p < parameters.getNumChildren(); ++p)
        {
            const auto parameter = parameters.getChild (p);
            const auto parameterId = parameter.getProperty (id::paramId).toString();

            if (parameterId.isEmpty())
                continue;

            Endpoint endpoint;
            endpoint.nodeUuid    = nodeUuid;
            endpoint.parameterId = parameterId;
            endpoint.minValue    = static_cast<float> ((double) parameter.getProperty (id::paramMin, 0.0));
            endpoint.maxValue    = static_cast<float> ((double) parameter.getProperty (id::paramMax, 1.0));
            endpoint.target      = module != nullptr ? module->getParameterTarget (parameterId) : nullptr;

            // try_emplace: bei moduleId-Kollision gewinnt der erste Node
            rebuilt.try_emplace (addressPrefix + parameterId, std::move (endpoint));
        }
    }

    {
        const juce::ScopedLock lock (registryLock);
        endpoints.swap (rebuilt);
    }

    if (unresolvedModuleRemaining)
    {
        // Self-Re-Dispatch (Muster aus 5.2 Schritt 3): der GraphManager
        // materialisiert das Modul in seinem eigenen Async-Zyklus — danach
        // löst dieser Durchlauf den target-Pointer auf. Terminiert, weil
        // jeder Tree-Node materialisiert wird, nodeError bekommt oder
        // aus dem Tree verschwindet.
        registryDirty = true;
        triggerAsyncUpdate();
    }
}

//==============================================================================
bool OscController::touchesNodeTopology (const juce::ValueTree& parent,
                                         const juce::ValueTree& child) noexcept
{
    return parent.hasType (id::nodes) || child.hasType (id::nodes)
        || parent.hasType (id::node)  || child.hasType (id::node)
        || parent.hasType (id::parameters) || child.hasType (id::parameters);
}

void OscController::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    juce::ignoreUnused (tree);

    // paramValue-Änderungen kommen im Millisekundentakt (u.a. von uns selbst
    // in applyTreeUpdates) — sie ändern die Registry nicht.
    if (property == id::nodeState || property == id::moduleId
        || property == id::nodeError || property == id::type
        || property == id::paramMin || property == id::paramMax
        || property == id::paramId)
        markRegistryDirty();
}

void OscController::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (touchesNodeTopology (parent, child))
        markRegistryDirty();
}

void OscController::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (touchesNodeTopology (parent, child))
        markRegistryDirty();
}

void OscController::valueTreeRedirected (juce::ValueTree&)
{
    markRegistryDirty();
}

void OscController::markRegistryDirty()
{
    JUCE_ASSERT_MESSAGE_THREAD

    registryDirty = true;
    triggerAsyncUpdate();
}

} // namespace conduit
