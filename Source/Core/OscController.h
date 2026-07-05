#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_osc/juce_osc.h>

#include "Core/OscAddress.h"
#include "Util/SpscQueue.h"

namespace conduit
{

class GraphManager;

//==============================================================================
/** POD-Message für den Echtzeit-Pfad des Dual-State (CLAUDE.md 6.1):
    Netzwerk-Thread → SpscQueue → Audio Thread, < 1ms. */
struct ParameterUpdate
{
    std::atomic<float>* target = nullptr;  // Atomic im Ziel-Modul (getParameterTarget)
    float value = 0.0f;                    // bereits auf [min, max] geclamped
};

//==============================================================================
/**
    OSC-Integration (CLAUDE.md 7) mit Dual-State-Dispatch (6.1).

    Jede eingehende Parameter-Message läuft auf zwei Pfaden:
      1. SpscQueue → Audio Thread (sofort, lock-free) — der EngineProcessor
         dräniert die Queue am Anfang jedes processBlock()
      2. pendingTreeUpdates → AsyncUpdater → ValueTree [Message Thread]
         (UI + Serialisierung folgen nach, ~1 Frame); isDirty schützt
         Preset-Save/Undo-Snapshot vor verlorenen OSC-Werten

    Auto-Registration (7.1): lauscht als ValueTree::Listener auf den Root-Tree
    und baut die Adress-Registry bei jeder Topologie-Änderung neu auf —
    Pfad-Schema /conduit/{type}/{named_id}/{param_id}, z.B.
    /conduit/utility/attenuator/gain. Bei nodeState → Deleting (Phase 1 des
    Deletes, 5.3) verschwinden die Adressen des Nodes sofort — nicht erst bei
    valueTreeChildRemoved. Noch nicht materialisierte Module (5.2 Schritt 1–3)
    werden per Self-Re-Dispatch nachaufgelöst.

    Tragen mehrere Nodes dieselbe moduleId, gewinnt der erste — eindeutige,
    user-vergebbare named_ids folgen mit der Node-UI.

    Lebensdauer der target-Pointer (harte Invariante, kein Timing-Argument):
    der Queue-Push erfolgt unter registryLock. Da die Deregistrierung
    (rebuildEndpoints — Registry-Swap, Phase 1 des Deletes) denselben Lock
    nimmt, kann nach abgeschlossener Deregistrierung kein stale target mehr
    in die Queue gelangen. Per-Block-Drain plus Phase-2-Zerstörung ≥ 1 Frame
    später schließen das Fenster vollständig. Bei gestopptem Audio leert
    EngineProcessor::releaseResources() die Queue.

    Thread-Ownership:
      - oscMessageReceived()                  → Netzwerk-Thread (OSCReceiver)
      - connect()/disconnect(), Flush,
        ValueTree-Callbacks, handleAsyncUpdate() → Message Thread
      - Registry/pendingTreeUpdates: CriticalSection zwischen Netzwerk- und
        Message Thread — der Audio Thread ist NIE beteiligt (3.1 bleibt gewahrt)

    DSP-Module wissen nichts von OSC (Single Responsibility, 7.1).
*/
class OscController final : private juce::ValueTree::Listener,
                            private juce::AsyncUpdater,
                            private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    /** Registriert sich als Listener am Root-Tree. Bindet noch keinen Port —
        connect() explizit aufrufen (Tests/CI bleiben netzwerkfrei). */
    OscController (juce::ValueTree rootTree,
                   GraphManager& graphManagerToUse,
                   SpscQueue<ParameterUpdate>& audioQueueToUse);

    ~OscController() override;

    static constexpr int defaultPort = 9000;

    //==========================================================================
    // Message Thread

    /** Bindet den UDP-Empfang. false, wenn der Port belegt ist. */
    [[nodiscard]] bool connect (int udpPort);
    void disconnect();

    /** Aktiver UDP-Port, -1 wenn nicht verbunden — für die Status-UI. */
    [[nodiscard]] int getConnectedPort() const noexcept { return connectedPort; }

    /** true, solange empfangene OSC-Werte noch nicht im ValueTree stehen.
        Preset-Save/Undo-Snapshot müssen vorher flushPendingUpdates() rufen (6.1). */
    [[nodiscard]] bool isStateDirty() const noexcept;

    /** Wendet ausstehende Tree-Updates sofort synchron an (statt auf den
        nächsten Message-Loop-Durchlauf zu warten) und löst die Registry auf —
        deterministischer Serialisierungs-Guard, auch für Tests. */
    void flushPendingUpdates();

    /** Aktuell registrierte OSC-Adressen — für Tests und die spätere UI. */
    [[nodiscard]] juce::StringArray getRegisteredAddresses() const;

    /** [Message Thread] Feuert nach jedem in den Tree übernommenen
        Empfangswert (6.1 Pfad 2) — der OscSendService impft darüber seinen
        Echo-Cache (noteRemoteValue, 7.3). Verdrahtet vom EngineProcessor;
        optional (Tests laufen ohne). */
    std::function<void (const juce::String& nodeUuid,
                        const juce::String& parameterId,
                        float value)> onRemoteValueApplied;

    /** [Message Thread] Feuert nach einem empfangenen /conduit/sync (7.3) —
        der EngineProcessor verdrahtet darüber den Voll-Dump des
        OscSendService. Optional (Tests laufen ohne). */
    std::function<void()> onSyncRequested;

    /** [Message Thread] Ein validiertes /conduit/announce (7.4) — der
        EngineProcessor verdrahtet den RemoteModuleBinder (find-or-create)
        plus Node-Werte-Dump. Optional (Tests laufen ohne). */
    std::function<void (const osc::AnnounceInfo&)> onAnnounce;

    /** [Message Thread] Eine validierte Looper-Aktion (M8: Commit/Stop/
        Target via Push-Pads/Fußschalter) — der EngineProcessor verdrahtet
        SessionModel + commitToTarget. Optional (Tests laufen ohne). */
    std::function<void (const osc::LooperOscAction&)> onLooperAction;

    //==========================================================================
    // IP-Learn (7.3) — Message Thread

    /** Callback der Learn-Probe: Absender-IP des ersten UDP-Pakets, leer bei
        Timeout oder Bind-Fehler. Läuft auf dem Message Thread; der Receiver
        ist beim Aufruf bereits wiederhergestellt. */
    using LearnCallback = std::function<void (const juce::String& senderIp)>;

    /** Startet die Learn-Probe: juce::OSCReceiver verwirft die Absender-IP
        (socket->read ohne senderAddress in juce_OSCReceiver.cpp) — deshalb
        wird der Receiver kurz getrennt und ein eigener DatagramSocket bindet
        den Empfangsport, dessen read() die IP liefert. Kein OSC-Parsing
        nötig — das erste Paket gewinnt. false, wenn nicht verbunden oder
        schon im Learn-Modus. */
    [[nodiscard]] bool beginIpLearn (LearnCallback callback, int timeoutMs = 15000);

    /** Bricht die Learn-Probe ab und stellt den Receiver wieder her —
        der Callback feuert danach nicht mehr. No-op ohne aktive Probe. */
    void cancelIpLearn();

    [[nodiscard]] bool isLearning() const noexcept { return learnProbe != nullptr; }

    //==========================================================================
    /** Netzwerk-Thread. Public, damit Tests Messages ohne Socket einspeisen
        können (juce::OSCMessage ist frei konstruierbar). */
    void oscMessageReceived (const juce::OSCMessage& message) override;

private:
    //==========================================================================
    struct Endpoint
    {
        juce::String nodeUuid;
        juce::String parameterId;
        std::atomic<float>* target = nullptr;  // nullptr bis Modul materialisiert
        float minValue = 0.0f;
        float maxValue = 1.0f;
    };

    struct TreeUpdate
    {
        juce::String nodeUuid;
        juce::String parameterId;
        float value = 0.0f;
    };

    //==========================================================================
    // juce::ValueTree::Listener [Message Thread] — markiert die Registry dirty
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // juce::AsyncUpdater [Message Thread]
    void handleAsyncUpdate() override;

    //==========================================================================
    void markRegistryDirty();

    /** Baut die Adress-Registry komplett neu aus dem Tree auf (idempotent —
        gleiches Muster wie der Topologie-Sync des GraphManager). */
    void rebuildEndpoints();

    /** Wendet gesammelte OSC-Werte auf den ValueTree an (ohne UndoManager —
        Parameter-Sweeps sind keine patchbaren Aktionen). */
    void applyTreeUpdates();

    [[nodiscard]] static bool touchesNodeTopology (const juce::ValueTree& parent,
                                                   const juce::ValueTree& child) noexcept;

    //==========================================================================
    /** Beendet die Learn-Probe regulär (aus handleAsyncUpdate): Thread
        joinen, Receiver wiederherstellen, Callback mit dem Ergebnis rufen. */
    void finishIpLearn();

    /** Rebind nach der Probe — kurzer Retry, das Port-Freigabe-Fenster des
        Probe-Sockets kann nachlaufen (v.a. Windows). */
    void restoreReceiverAfterLearn();

    //==========================================================================
    juce::ValueTree rootState;             // ref-counted Handle
    GraphManager& graphManager;
    SpscQueue<ParameterUpdate>& audioQueue;

    juce::OSCReceiver receiver;

    // Netzwerk-Thread liest, Message Thread schreibt (rebuild)
    mutable juce::CriticalSection registryLock;
    std::map<juce::String, Endpoint> endpoints;  // Key: voller OSC-Pfad

    // Netzwerk-Thread schreibt, Message Thread dräniert
    juce::CriticalSection treeUpdateLock;
    std::vector<TreeUpdate> pendingTreeUpdates;

    // Announces (7.4) — eigener Lock, gleiche Marshalling-Richtung
    juce::CriticalSection announceLock;
    std::vector<osc::AnnounceInfo> pendingAnnounces;

    // Looper-Aktionen (M8) — gleiches Muster (sammeln → MT-Hook)
    juce::CriticalSection looperActionLock;
    std::vector<osc::LooperOscAction> pendingLooperActions;

    std::atomic<bool> stateDirty { false };
    std::atomic<bool> syncRequested { false };  // Netzwerk → Message Thread

    bool registryDirty = true;  // nur Message Thread
    int connectedPort = -1;     // nur Message Thread

    //==========================================================================
    // IP-Learn: Probe-Thread schreibt learnResultIp VOR dem Release-Store auf
    // learnDone; der Message Thread liest nach dem Acquire-Exchange — geordnet.
    class LearnProbe;

    std::unique_ptr<LearnProbe> learnProbe;  // nur Message Thread
    LearnCallback learnCallback;             // nur Message Thread
    int learnPort = -1;                      // nur Message Thread
    std::atomic<bool> learnDone { false };
    juce::String learnResultIp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscController)
};

} // namespace conduit
