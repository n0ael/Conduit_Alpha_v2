#include "OscController.h"

#include "GraphManager.h"
#include "Modules/ConduitModule.h"
#include "OscAddress.h"

namespace conduit
{

//==============================================================================
/** Learn-Probe (7.3): bindet den freigegebenen Empfangsport mit einem
    eigenen DatagramSocket und liest die Absender-IP des ersten Pakets.
    Ergebnis-Übergabe: learnResultIp schreiben → learnDone (release) →
    triggerAsyncUpdate. Bei Cancel (threadShouldExit) endet der Thread
    ohne Signal — der Aufrufer restauriert selbst. */
class OscController::LearnProbe final : public juce::Thread
{
public:
    LearnProbe (OscController& ownerToUse, int portToUse, int timeoutMsToUse)
        : juce::Thread ("Conduit OSC IP-Learn"),
          owner (ownerToUse), port (portToUse), timeoutMs (timeoutMsToUse)
    {
    }

    ~LearnProbe() override { stopThread (2000); }

    void run() override
    {
        juce::DatagramSocket socket;
        juce::String senderIp;

        // Port-Rebind-Fenster: der Receiver hat den Port gerade erst
        // freigegeben — kurzer Retry statt sofortigem Fehler
        bool bound = false;

        for (int attempt = 0; attempt < 20 && ! threadShouldExit(); ++attempt)
        {
            if (socket.bindToPort (port))
            {
                bound = true;
                break;
            }

            wait (50);
        }

        if (bound)
        {
            const auto deadline = juce::Time::getMillisecondCounter()
                                  + static_cast<juce::uint32> (juce::jmax (0, timeoutMs));

            while (! threadShouldExit()
                   && juce::Time::getMillisecondCounter() < deadline)
            {
                if (socket.waitUntilReady (true, 100) != 1)
                    continue;

                char buffer[64];
                int senderPort = 0;
                juce::String ip;

                if (socket.read (buffer, sizeof (buffer), false, ip, senderPort) > 0
                    && ip.isNotEmpty())
                {
                    senderIp = ip;
                    break;
                }
            }
        }

        if (threadShouldExit())
            return;  // Cancel — kein Signal, cancelIpLearn() restauriert

        owner.learnResultIp = senderIp;  // vor dem Release-Store (Ordering)
        owner.learnDone.store (true, std::memory_order_release);
        owner.triggerAsyncUpdate();
    }

private:
    OscController& owner;
    const int port;
    const int timeoutMs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnProbe)
};

//==============================================================================
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
    cancelIpLearn();  // joint den Probe-Thread, bevor Member sterben
    disconnect();
    receiver.removeListener (this);
    rootState.removeListener (this);
    cancelPendingUpdate();  // AsyncUpdater darf nie auf ein zerstörtes Objekt feuern
}

//==============================================================================
bool OscController::connect (int udpPort)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto ok = receiver.connect (udpPort);
    connectedPort = ok ? udpPort : -1;
    return ok;
}

void OscController::disconnect()
{
    receiver.disconnect();
    connectedPort = -1;
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

//==============================================================================
bool OscController::beginIpLearn (LearnCallback callback, int timeoutMs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (learnProbe != nullptr || connectedPort <= 0 || callback == nullptr)
        return false;

    learnPort = connectedPort;
    learnCallback = std::move (callback);
    learnDone.store (false, std::memory_order_release);

    disconnect();  // Port freigeben — learnPort merkt sich das Rebind-Ziel

    learnProbe = std::make_unique<LearnProbe> (*this, learnPort, timeoutMs);
    learnProbe->startThread();
    return true;
}

void OscController::cancelIpLearn()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (learnProbe == nullptr)
        return;

    learnProbe->stopThread (2000);
    learnProbe.reset();

    // Ein evtl. schon gesetztes Ergebnis verwerfen — nach Cancel darf der
    // Callback nicht mehr feuern
    learnDone.store (false, std::memory_order_release);
    learnCallback = nullptr;

    restoreReceiverAfterLearn();
}

void OscController::finishIpLearn()
{
    if (learnProbe == nullptr)
        return;  // Cancel hat gewonnen

    learnProbe->stopThread (2000);
    learnProbe.reset();

    restoreReceiverAfterLearn();

    const auto callback = std::move (learnCallback);
    learnCallback = nullptr;

    if (callback != nullptr)
        callback (learnResultIp);
}

void OscController::restoreReceiverAfterLearn()
{
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        if (connect (learnPort))
            return;

        juce::Thread::sleep (20);  // Freigabe des Probe-Sockets kann nachlaufen
    }

    // connectedPort bleibt -1 — die Status-UI zeigt „nicht verbunden"
}

//==============================================================================
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

    // /conduit/sync (7.3): Voll-Dump-Anfrage — VOR dem Endpoint-Lookup,
    // Argumente sind egal; Ausführung gemarshallt auf den Message Thread
    if (message.getAddressPattern().toString() == osc::syncAddress)
    {
        syncRequested.store (true, std::memory_order_release);
        triggerAsyncUpdate();
        return;
    }

    // /conduit/announce (7.4): s:remoteId s:factoryKey s:trackName i:colour —
    // hier nur validieren und sammeln, find-or-create läuft auf dem
    // Message Thread (onAnnounce → RemoteModuleBinder)
    if (message.getAddressPattern().toString() == osc::announceAddress)
    {
        if (message.size() >= 4
            && message[0].isString() && message[1].isString()
            && message[2].isString()
            && (message[3].isInt32() || message[3].isFloat32()))
        {
            osc::AnnounceInfo info;
            info.remoteId   = message[0].getString();
            info.factoryKey = message[1].getString();
            info.trackName  = message[2].getString();
            // Max/js garantiert die Int-Kodierung von Zahlen nicht — Float
            // tolerieren (Track-Farbe ist ein 24-Bit-Wert, verlustfrei)
            info.tintArgb   = message[3].isInt32()
                                ? message[3].getInt32()
                                : static_cast<int> (message[3].getFloat32());

            if (osc::isValidRemoteId (info.remoteId) && info.factoryKey.isNotEmpty())
            {
                {
                    const juce::ScopedLock lock (announceLock);
                    pendingAnnounces.push_back (std::move (info));
                }

                triggerAsyncUpdate();
            }
        }

        return;  // Garbage still verwerfen — kein Crash, kein Log-Spam
    }

    // /conduit/looper/... (M8): Aktions-Adressen VOR dem Endpoint-Lookup —
    // Adresse pur parsen, Argumente validieren, auf den MT marshallen
    if (message.getAddressPattern().toString().startsWith (osc::looperAddressPrefix))
    {
        auto action = osc::parseLooperActionAddress (
            message.getAddressPattern().toString());

        const auto intArg = [&message] (int index, int fallback) -> int
        {
            if (index >= message.size())
                return fallback;
            if (message[index].isInt32())
                return message[index].getInt32();
            if (message[index].isFloat32())   // Max/js-Toleranz wie Announce
                return static_cast<int> (message[index].getFloat32());
            return fallback;
        };

        switch (action.type)
        {
            case osc::LooperOscAction::Type::commit:
                action.bars = juce::jlimit (1, 8, intArg (0, 0));
                if (intArg (0, 0) < 1)
                    action.type = osc::LooperOscAction::Type::none;  // bars-Pflicht
                break;

            case osc::LooperOscAction::Type::target:
            {
                const auto track = intArg (0, 0);
                const auto slot  = intArg (1, 0);
                if (track >= 1 && track <= 4 && slot >= 1 && slot <= 12)
                {
                    action.trackIndex = track - 1;
                    action.slotIndex  = slot - 1;
                }
                else
                {
                    action.type = osc::LooperOscAction::Type::none;
                }
                break;
            }

            case osc::LooperOscAction::Type::stopLooper:
            case osc::LooperOscAction::Type::stopTrack:
            case osc::LooperOscAction::Type::stopAll:
            case osc::LooperOscAction::Type::none:
                break;
        }

        if (action.type != osc::LooperOscAction::Type::none)
        {
            {
                const juce::ScopedLock lock (looperActionLock);
                pendingLooperActions.push_back (action);
            }

            triggerAsyncUpdate();
        }

        return;  // Looper-Namensraum konsumiert — nie Endpoint-Lookup
    }

    if (message.size() < 1)
        return;

    float rawValue = 0.0f;

    if (message[0].isFloat32())
        rawValue = message[0].getFloat32();
    else if (message[0].isInt32())
        rawValue = static_cast<float> (message[0].getInt32());
    else
        return;  // unbekannter Argument-Typ — ignorieren, kein Crash

    juce::String nodeUuid;
    juce::String parameterId;
    float clamped = 0.0f;

    {
        // Lookup UND Push im selben Lock-Scope: rebuildEndpoints() swappt die
        // Registry unter demselben registryLock — ein Push kann daher nie
        // einen bereits deregistrierten Endpoint (Phase 1, 5.3) übergeben.
        // push() ist wait-free, der Audio Thread nimmt diesen Lock nie
        // (3.1 bleibt gewahrt).
        const juce::ScopedLock lock (registryLock);
        const auto it = endpoints.find (message.getAddressPattern().toString());

        if (it == endpoints.end())
            return;  // unregistrierte Adresse — ignorieren

        const auto& endpoint = it->second;
        clamped = juce::jlimit (endpoint.minValue, endpoint.maxValue, rawValue);

        // Pfad 1: sofort in den Audio Thread (lock-free, < 1ms). Volle Queue →
        // Update verworfen; der Tree-Pfad unten trägt den Wert trotzdem nach.
        if (endpoint.target != nullptr)
            audioQueue.push ({ endpoint.target, clamped });

        nodeUuid    = endpoint.nodeUuid;
        parameterId = endpoint.parameterId;
    }

    // Pfad 2: async in den ValueTree — UI + Serialisierung folgen nach (6.1)
    {
        const juce::ScopedLock lock (treeUpdateLock);
        pendingTreeUpdates.push_back ({ std::move (nodeUuid), std::move (parameterId), clamped });
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

    // Announces VOR dem Sync-Dump: neu angelegte Nodes stehen sofort im
    // Tree (createState) und landen damit noch im selben Dump
    {
        std::vector<osc::AnnounceInfo> announces;

        {
            const juce::ScopedLock lock (announceLock);
            announces.swap (pendingAnnounces);
        }

        if (onAnnounce != nullptr)
            for (const auto& announce : announces)
                onAnnounce (announce);
    }

    // Looper-Aktionen (M8) — gleiche Marshalling-Richtung wie Announces
    {
        std::vector<osc::LooperOscAction> actions;

        {
            const juce::ScopedLock lock (looperActionLock);
            actions.swap (pendingLooperActions);
        }

        if (onLooperAction != nullptr)
            for (const auto& action : actions)
                onLooperAction (action);
    }

    // Sync NACH applyTreeUpdates — der Dump enthält damit auch Werte,
    // die im selben Durchlauf angekommen sind
    if (syncRequested.exchange (false, std::memory_order_acq_rel))
        if (onSyncRequested != nullptr)
            onSyncRequested();

    // Learn-Probe fertig (Ergebnis oder Timeout) — Receiver wiederherstellen
    if (learnDone.exchange (false, std::memory_order_acq_rel))
        finishIpLearn();
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
        {
            parameter.setProperty (id::paramValue, update.value, nullptr);

            // Echo-Impfung des Send-Pfads (7.3): der Wert gilt als gesendet
            if (onRemoteValueApplied != nullptr)
                onRemoteValueApplied (update.nodeUuid, update.parameterId, update.value);
        }
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

        const auto parameters = nodeTree.getChildWithName (id::parameters);

        if (parameters.getNumChildren() == 0)
            continue;  // nichts zu registrieren (z.B. externe I/O-Endpunkte)

        auto* module = graphManager.getModuleFor (nodeUuid);

        if (module == nullptr
            && nodeTree.getProperty (id::nodeError).toString().isEmpty())
            unresolvedModuleRemaining = true;  // materialisiert erst nach dem Swap

        // Announce-gebundene Nodes (7.4) hören ZUSÄTZLICH auf ihren
        // rename-festen Alias /conduit/remote/{remoteId}/{paramId}
        const auto remoteId = nodeTree.getProperty (id::remoteId).toString();

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

            if (remoteId.isNotEmpty())
                rebuilt.try_emplace (osc::remoteAliasAddress (remoteId, parameterId),
                                     endpoint);

            // try_emplace: bei moduleId-Kollision gewinnt der erste Node
            rebuilt.try_emplace (osc::parameterAddress (nodeTree, parameterId),
                                 std::move (endpoint));
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
