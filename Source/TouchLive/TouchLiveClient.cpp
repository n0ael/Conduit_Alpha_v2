#include "TouchLiveClient.h"

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
namespace
{
    constexpr const char* pongAddress        = "/remote/pong";
    constexpr const char* pingAddress        = "/remote/ping";
    constexpr const char* metersAddress      = "/remote/meters";
    constexpr const char* browserListAddress = "/remote/browser/list";
    constexpr const char* statePrefix        = "/remote/state/";

    [[nodiscard]] juce::String stateAddress (const juce::String& domainName,
                                             const char* suffix)
    {
        return juce::String (statePrefix) + domainName + "/" + suffix;
    }

    //==========================================================================
    /** UDP-Transport der App: OSCSender → host:commandPort, OSCReceiver auf
        listenPort mit Realtime-Callback (Netzwerk-Thread). Bundles werden
        flach an den Handler durchgereicht. */
    class UdpRemoteTransport final
        : public IRemoteTransport,
          private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
    {
    public:
        UdpRemoteTransport() { receiver.addListener (this); }

        ~UdpRemoteTransport() override
        {
            disconnect();
            receiver.removeListener (this);
        }

        void setMessageHandler (MessageHandler handlerToUse) override
        {
            // VOR connect() — der Receiver-Thread läuft erst danach
            handler = std::move (handlerToUse);
        }

        bool connect (const juce::String& host, int commandPort, int listenPort) override
        {
            disconnect();

            if (! receiver.connect (listenPort))
                return false;

            if (! sender.connect (host, commandPort))
            {
                receiver.disconnect();
                return false;
            }

            connected = true;
            return true;
        }

        void disconnect() override
        {
            receiver.disconnect();
            sender.disconnect();
            connected = false;
        }

        bool send (const juce::OSCMessage& message) override
        {
            return connected && sender.send (message);
        }

    private:
        void oscMessageReceived (const juce::OSCMessage& message) override
        {
            if (handler != nullptr)
                handler (message);
        }

        void oscBundleReceived (const juce::OSCBundle& bundle) override
        {
            for (const auto& element : bundle)
            {
                if (element.isMessage())
                    oscMessageReceived (element.getMessage());
                else if (element.isBundle())
                    oscBundleReceived (element.getBundle());
            }
        }

        juce::OSCSender sender;
        juce::OSCReceiver receiver;
        MessageHandler handler;
        bool connected = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UdpRemoteTransport)
    };
} // namespace

std::unique_ptr<IRemoteTransport> makeUdpRemoteTransport()
{
    return std::make_unique<UdpRemoteTransport>();
}

//==============================================================================
/** Learn-Probe (Muster OscController, docs/OscSend.md): bindet den
    freigegebenen Listen-Port mit einem eigenen DatagramSocket und liest die
    Absender-IP des ersten Pakets. Da das Script NIE spontan sendet, provoziert
    die Probe die Antwort selbst: periodischer /remote/ping-Broadcast an den
    Command-Port (255.255.255.255 + konfigurierter Host) — der Pong des
    Scripts geht an Absender-IP:Listen-Port, also genau an die Probe.
    Ergebnis-Übergabe: learnResultIp schreiben → learnDone (release) →
    triggerAsyncUpdate. Bei Cancel (threadShouldExit) endet der Thread ohne
    Signal — der Aufrufer restauriert selbst. */
class TouchLiveClient::LearnProbe final : public juce::Thread
{
public:
    LearnProbe (TouchLiveClient& ownerToUse, int listenPortToUse,
                int commandPortToUse, juce::String configuredHostToUse,
                int timeoutMsToUse)
        : juce::Thread ("Conduit TouchLive IP-Learn"),
          owner (ownerToUse),
          listenPort (listenPortToUse),
          commandPort (commandPortToUse),
          configuredHost (std::move (configuredHostToUse)),
          timeoutMs (timeoutMsToUse)
    {
    }

    ~LearnProbe() override { stopThread (2000); }

    void run() override
    {
        juce::DatagramSocket socket (true);  // Broadcast erlaubt
        juce::String senderIp;

        // Port-Rebind-Fenster: der Receiver hat den Port gerade erst
        // freigegeben — kurzer Retry statt sofortigem Fehler
        bool bound = false;

        for (int attempt = 0; attempt < 20 && ! threadShouldExit(); ++attempt)
        {
            if (socket.bindToPort (listenPort))
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
            juce::uint32 nextPingAt = 0;

            while (! threadShouldExit()
                   && juce::Time::getMillisecondCounter() < deadline)
            {
                if (juce::Time::getMillisecondCounter() >= nextPingAt)
                {
                    sendPings (socket);
                    nextPingAt = juce::Time::getMillisecondCounter() + 1000u;
                }

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
    void sendPings (juce::DatagramSocket& socket)
    {
        // Handkodiertes OSC "/remote/ping" ohne Argumente: Adresse (13 Bytes
        // → 16 gepolstert) + Typetag "," (2 → 4) = 20 Bytes
        static constexpr char pingPacket[20] = {
            '/', 'r', 'e', 'm', 'o', 't', 'e', '/', 'p', 'i', 'n', 'g', 0, 0, 0, 0,
            ',', 0, 0, 0
        };

        socket.write ("255.255.255.255", commandPort, pingPacket,
                      static_cast<int> (sizeof (pingPacket)));

        if (configuredHost.isNotEmpty())
            socket.write (configuredHost, commandPort, pingPacket,
                          static_cast<int> (sizeof (pingPacket)));
    }

    TouchLiveClient& owner;
    const int listenPort;
    const int commandPort;
    const juce::String configuredHost;
    const int timeoutMs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnProbe)
};

//==============================================================================
TouchLiveClient::TouchLiveClient (LiveSetModel& modelToUse,
                                  TouchLiveMeterBus& meterBusToUse,
                                  TouchLiveSettings& settingsToUse,
                                  std::unique_ptr<IRemoteTransport> transportToUse)
    : model (modelToUse),
      meterBus (meterBusToUse),
      settings (settingsToUse),
      transport (transportToUse != nullptr ? std::move (transportToUse)
                                           : makeUdpRemoteTransport()),
      nowMs ([] { return juce::Time::getMillisecondCounterHiRes(); })
{
    for (const auto* domainName : domainNames)
        domainSync.emplace (juce::String (domainName), DomainSync {});

    // Netzwerk-Thread → Queue → AsyncUpdater → Message Thread
    transport->setMessageHandler ([this] (const juce::OSCMessage& message)
    {
        {
            const juce::ScopedLock scope (incomingLock);
            pendingIncoming.push_back (message);
        }

        triggerAsyncUpdate();
    });

    settings.addChangeListener (this);
    applySettings();
}

TouchLiveClient::~TouchLiveClient()
{
    settings.removeChangeListener (this);
    cancelIpLearn();
    transport->disconnect();  // stoppt den Netzwerk-Thread VOR dem Abbau
    stopTimer (heartbeatTimerId);
    stopTimer (thinningTimerId);
    cancelPendingUpdate();
}

//==============================================================================
bool TouchLiveClient::sendCommand (const juce::OSCMessage& message)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (status == Status::disabled || ! transportConnected)
        return false;

    return transport->send (message);
}

void TouchLiveClient::sendTouchValue (const juce::OSCMessage& message)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (status == Status::disabled || ! transportConnected)
        return;

    const auto address = message.getAddressPattern().toString();
    const auto now = nowMs();
    const auto lastSent = lastTouchSendMs.find (address);

    if (lastSent == lastTouchSendMs.end()
        || now - lastSent->second >= touchThinningIntervalMs)
    {
        lastTouchSendMs[address] = now;
        pendingTouchValues.erase (address);  // Zwischenwert ist überholt
        transport->send (message);
        ++stats.touchValuesSent;
        return;
    }

    // Fenster noch zu — letzter Wert gewinnt, der Timer flusht den Rest
    pendingTouchValues.insert_or_assign (address, message);
    startTimer (thinningTimerId, static_cast<int> (touchThinningIntervalMs));
}

void TouchLiveClient::flushPendingTouchValues()
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto now = nowMs();

    for (auto it = pendingTouchValues.begin(); it != pendingTouchValues.end();)
    {
        if (now - lastTouchSendMs[it->first] >= touchThinningIntervalMs)
        {
            lastTouchSendMs[it->first] = now;
            transport->send (it->second);
            ++stats.touchValuesSent;
            it = pendingTouchValues.erase (it);
        }
        else
        {
            ++it;
        }
    }

    if (pendingTouchValues.empty())
        stopTimer (thinningTimerId);
}

//==============================================================================
void TouchLiveClient::noteTouchedParameter (const juce::String& suppressionKey)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (suppressionKey.isNotEmpty())
        touchedUntilMs[suppressionKey] = nowMs() + echoSuppressionReleaseMs;
}

juce::String TouchLiveClient::makeParameterKey (const juce::String& domainName,
                                                const juce::String& key,
                                                const juce::String& field)
{
    auto result = domainName + "/" + key;

    if (field.isNotEmpty())
        result << "/" << field;

    return result;
}

bool TouchLiveClient::isSuppressed (const juce::String& domainName,
                                    const juce::String& key,
                                    const juce::String& field)
{
    const auto it = touchedUntilMs.find (makeParameterKey (domainName, key, field));

    if (it == touchedUntilMs.end())
        return false;

    if (nowMs() < it->second)
        return true;

    touchedUntilMs.erase (it);  // Release abgelaufen
    return false;
}

//==============================================================================
void TouchLiveClient::setTimeSource (std::function<double()> nowMsToUse)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (nowMsToUse != nullptr)
        nowMs = std::move (nowMsToUse);
}

void TouchLiveClient::flushPendingMessages()
{
    JUCE_ASSERT_MESSAGE_THREAD
    handleUpdateNowIfNeeded();
}

//==============================================================================
void TouchLiveClient::timerCallback (int timerId)
{
    if (timerId == heartbeatTimerId)
        runHeartbeatTick();
    else if (timerId == thinningTimerId)
        flushPendingTouchValues();
}

void TouchLiveClient::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applySettings();
}

void TouchLiveClient::handleAsyncUpdate()
{
    std::vector<juce::OSCMessage> drained;

    {
        const juce::ScopedLock scope (incomingLock);
        drained.swap (pendingIncoming);
    }

    for (const auto& message : drained)
        routeMessage (message);

    // Learn-Probe fertig (Ergebnis oder Timeout) — Transport wiederherstellen
    if (learnDone.exchange (false, std::memory_order_acq_rel))
        finishIpLearn();
}

//==============================================================================
void TouchLiveClient::applySettings()
{
    JUCE_ASSERT_MESSAGE_THREAD

    stopTimer (heartbeatTimerId);
    stopTimer (thinningTimerId);
    transport->disconnect();
    transportConnected = false;
    pendingTouchValues.clear();
    lastTouchSendMs.clear();

    // Unfertige Chunk-Sammlungen verwerfen; Seq-Stände bleiben — der nächste
    // Snapshot setzt sie ohnehin neu
    for (auto& entry : domainSync)
        entry.second.pending = {};

    meterBus.clear();   // alte Pegel verwerfen (Disable UND Neuverbindung)

    if (! settings.isEnabled())
    {
        setStatus (Status::disabled);
        return;
    }

    setStatus (Status::connecting);
    pingsSinceLastPong = 0;
    stats = {};

    transportConnected = transport->connect (settings.getHost(),
                                             settings.getCommandPort(),
                                             settings.getListenPort());

    if (transportConnected)
    {
        subscribeAll();
        transport->send (juce::OSCMessage (juce::OSCAddressPattern (pingAddress)));
        pingsSinceLastPong = 1;
    }

    // Timer läuft auch bei Bind-Fehler — jeder Tick versucht den Reconnect
    startTimer (heartbeatTimerId, heartbeatIntervalMs);
}

void TouchLiveClient::setStatus (Status newStatus)
{
    if (status == newStatus)
        return;

    status = newStatus;
    sendChangeMessage();
}

void TouchLiveClient::subscribeAll()
{
    for (const auto* domainName : domainNames)
    {
        transport->send (juce::OSCMessage (juce::OSCAddressPattern (
            stateAddress (domainName, "get"))));
        transport->send (juce::OSCMessage (juce::OSCAddressPattern (
            stateAddress (domainName, "subscribe"))));
    }

    // Meter-Hochraten-Pfad (M2) — eigene Subscription neben den Domains
    transport->send (juce::OSCMessage (juce::OSCAddressPattern (
        juce::String (metersAddress) + "/subscribe")));
}

void TouchLiveClient::requestSnapshot (const juce::String& domainName, DomainSync& sync)
{
    if (sync.snapshotRequested)
        return;  // Drossel — Reset pro Heartbeat-Tick heilt verlorene Requests

    sync.snapshotRequested = true;
    transport->send (juce::OSCMessage (juce::OSCAddressPattern (
        stateAddress (domainName, "get"))));
}

void TouchLiveClient::runHeartbeatTick()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (status == Status::disabled)
        return;

    if (! transportConnected)
    {
        // Reconnect-Backoff = Tick-Kadenz (2 s)
        transportConnected = transport->connect (settings.getHost(),
                                                 settings.getCommandPort(),
                                                 settings.getListenPort());

        if (! transportConnected)
            return;

        subscribeAll();
    }

    // Verlorene Snapshot-Re-Requests heilen: nächste Lücke darf erneut fragen
    for (auto& entry : domainSync)
        entry.second.snapshotRequested = false;

    if (pingsSinceLastPong >= maxMissedPongs && status == Status::connected)
        setStatus (Status::disconnected);

    transport->send (juce::OSCMessage (juce::OSCAddressPattern (pingAddress)));
    ++pingsSinceLastPong;
}

//==============================================================================
void TouchLiveClient::routeMessage (const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();

    if (address == pongAddress)
    {
        handlePong();
        return;
    }

    if (address == metersAddress)
    {
        handleMeters (message);
        return;
    }

    if (address == browserListAddress)
    {
        handleBrowserList (message);
        return;
    }

    if (! address.startsWith (statePrefix))
        return;

    const auto remainder  = address.fromFirstOccurrenceOf (statePrefix, false, false);
    const auto domainName = remainder.upToFirstOccurrenceOf ("/", false, false);
    const auto kind       = remainder.fromFirstOccurrenceOf ("/", false, false);

    if (kind == "snapshot")
        handleStatePayload (domainName, true, message);
    else if (kind == "diff")
        handleStatePayload (domainName, false, message);
}

void TouchLiveClient::handleMeters (const juce::OSCMessage& message)
{
    // Flache Tripel [id:str, left:float, right:float] — roh in den Bus,
    // KEIN Slew/keine Suppression (Feel-Regel 5.1: Meter sind ausgenommen)
    const auto triplets = message.size() / 3;

    for (int i = 0; i < triplets; ++i)
    {
        const auto& keyArg   = message[i * 3];
        const auto& leftArg  = message[i * 3 + 1];
        const auto& rightArg = message[i * 3 + 2];

        if (! keyArg.isString() || ! leftArg.isFloat32() || ! rightArg.isFloat32())
            return;   // fremdes/defektes Format — Frame verwerfen

        meterBus.update (keyArg.getString(), leftArg.getFloat32(), rightArg.getFloat32());
    }

    meterBus.noteFrame();
    ++stats.meterFrames;
}

void TouchLiveClient::handleBrowserList (const juce::OSCMessage& message)
{
    // Gleiches Wire-Format wie die Domains: [seq, chunk, chunks, json];
    // Request/Response — verlorene Antworten heilt der nächste Tap
    if (message.size() != 4
        || ! message[0].isInt32() || ! message[1].isInt32()
        || ! message[2].isInt32() || ! message[3].isString())
        return;

    const auto seq    = message[0].getInt32();
    const auto chunk  = message[1].getInt32();
    const auto chunks = message[2].getInt32();
    const auto json   = message[3].getString();

    juce::var payload;

    if (chunks <= 1)
    {
        payload = juce::JSON::parse (json);
    }
    else
    {
        if (browserPending.seq != seq || browserPending.expectedChunks != chunks)
        {
            browserPending = {};
            browserPending.seq = seq;
            browserPending.expectedChunks = chunks;
        }

        if (chunk >= 1 && chunk <= chunks)
            browserPending.parts[chunk] = json;

        if ((int) browserPending.parts.size() < browserPending.expectedChunks)
            return;

        // Chunks tragen Teil-Objekte mit denselben Top-Level-Keys ("p"/"it")
        // — Arrays werden beim Merge aneinandergehängt
        juce::DynamicObject::Ptr merged = new juce::DynamicObject();

        for (const auto& part : browserPending.parts)
        {
            const auto parsed = juce::JSON::parse (part.second);
            auto* object = parsed.getDynamicObject();

            if (object == nullptr)
            {
                browserPending = {};
                return;
            }

            for (const auto& prop : object->getProperties())
            {
                auto existing = merged->getProperty (prop.name);

                if (auto* target = existing.getArray())
                    if (const auto* source = prop.value.getArray())
                    {
                        for (const auto& entry : *source)
                            target->add (entry);

                        continue;
                    }

                merged->setProperty (prop.name, prop.value);
            }
        }

        browserPending = {};
        payload = juce::var (merged.get());
    }

    auto* object = payload.getDynamicObject();

    if (object == nullptr || onBrowserList == nullptr)
        return;

    onBrowserList ((int) object->getProperty ("p"),
                   object->getProperty ("it"));
}

void TouchLiveClient::requestBrowserRoots()
{
    JUCE_ASSERT_MESSAGE_THREAD
    sendCommand (juce::OSCMessage (juce::OSCAddressPattern ("/remote/browser/roots")));
}

void TouchLiveClient::requestBrowserChildren (int nodeId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    juce::OSCMessage message { juce::OSCAddressPattern ("/remote/browser/children") };
    message.addInt32 (nodeId);
    sendCommand (message);
}

void TouchLiveClient::loadBrowserItem (int nodeId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    juce::OSCMessage message { juce::OSCAddressPattern ("/live/browser/load") };
    message.addInt32 (nodeId);
    sendCommand (message);
}

void TouchLiveClient::previewBrowserItem (int nodeId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    juce::OSCMessage message { juce::OSCAddressPattern ("/live/browser/preview") };
    message.addInt32 (nodeId);
    sendCommand (message);
}

void TouchLiveClient::stopBrowserPreview()
{
    JUCE_ASSERT_MESSAGE_THREAD
    sendCommand (juce::OSCMessage (juce::OSCAddressPattern ("/live/browser/stop_preview")));
}

void TouchLiveClient::handlePong()
{
    pingsSinceLastPong = 0;

    if (status == Status::connecting || status == Status::disconnected)
    {
        setStatus (Status::connected);
        // Wiederverbindung (oder verspäteter Script-Start): Subscribes können
        // verloren gegangen sein → immer neu anfordern; der Snapshot landet
        // als Tree-Diff im Modell (kein Flackern)
        subscribeAll();
    }
}

void TouchLiveClient::handleStatePayload (const juce::String& domainName, bool isSnapshot,
                                          const juce::OSCMessage& message)
{
    const auto it = domainSync.find (domainName);

    if (it == domainSync.end())
        return;  // unbekannte Domain (künftige Gegenseite) — ignorieren

    // Wire-Format [seq:int, chunk:int, chunks:int, json:str] defensiv prüfen
    if (message.size() != 4
        || ! message[0].isInt32() || ! message[1].isInt32()
        || ! message[2].isInt32() || ! message[3].isString())
        return;

    const auto seq    = message[0].getInt32();
    const auto chunk  = message[1].getInt32();
    const auto chunks = message[2].getInt32();
    const auto json   = message[3].getString();

    if (chunks <= 1)
    {
        const auto payload = juce::JSON::parse (json);

        if (payload.getDynamicObject() != nullptr)
            applyPayload (domainName, isSnapshot, seq, payload);

        return;
    }

    // Chunk-Reassembly: Teile gleicher seq sammeln, erst komplett anwenden
    auto& pending = it->second.pending;

    if (pending.seq != seq || pending.isSnapshot != isSnapshot
        || pending.expectedChunks != chunks)
    {
        pending = {};
        pending.seq = seq;
        pending.isSnapshot = isSnapshot;
        pending.expectedChunks = chunks;
    }

    if (chunk >= 1 && chunk <= chunks)
        pending.parts[chunk] = json;

    if (static_cast<int> (pending.parts.size()) < pending.expectedChunks)
        return;

    // Komplett: Top-Level-Keys aller Chunks zu EINEM Payload vereinigen
    juce::DynamicObject::Ptr merged = new juce::DynamicObject();

    for (const auto& part : pending.parts)
    {
        const auto parsed = juce::JSON::parse (part.second);
        auto* object = parsed.getDynamicObject();

        if (object == nullptr)
        {
            pending = {};  // defekter Chunk — Sammlung verwerfen
            return;
        }

        for (const auto& prop : object->getProperties())
            merged->setProperty (prop.name, prop.value);
    }

    pending = {};
    applyPayload (domainName, isSnapshot, seq, juce::var (merged.get()));
}

void TouchLiveClient::applyPayload (const juce::String& domainName, bool isSnapshot,
                                    int seq, const juce::var& payload)
{
    auto& sync = domainSync[domainName];

    const auto suppressionCheck = [this] (const juce::String& d, const juce::String& k,
                                          const juce::String& f)
    {
        return isSuppressed (d, k, f);
    };

    if (isSnapshot)
    {
        model.applySnapshot (domainName, payload, suppressionCheck);
        sync.lastAppliedSeq = seq;
        sync.snapshotRequested = false;
        ++stats.snapshotsApplied;
        return;
    }

    if (sync.lastAppliedSeq < 0)
    {
        requestSnapshot (domainName, sync);  // Diff vor dem ersten Snapshot
        return;
    }

    if (seq <= sync.lastAppliedSeq)
        return;  // Duplikat/veraltet

    if (seq != sync.lastAppliedSeq + 1)
    {
        requestSnapshot (domainName, sync);  // Seq-Lücke → Voll-Snapshot heilt
        return;
    }

    model.applyDiff (domainName, payload, suppressionCheck);
    sync.lastAppliedSeq = seq;
    ++stats.diffsApplied;
}

//==============================================================================
bool TouchLiveClient::beginIpLearn (LearnCallback callback, int timeoutMs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (learnProbe != nullptr || callback == nullptr)
        return false;

    learnCallback = std::move (callback);
    learnDone.store (false, std::memory_order_release);

    // Listen-Port freigeben — die Probe bindet ihn selbst
    transport->disconnect();
    transportConnected = false;

    learnProbe = std::make_unique<LearnProbe> (*this, settings.getListenPort(),
                                               settings.getCommandPort(),
                                               settings.getHost(), timeoutMs);
    learnProbe->startThread();
    return true;
}

void TouchLiveClient::cancelIpLearn()
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

    applySettings();  // Transport wiederherstellen (falls enabled)
}

void TouchLiveClient::finishIpLearn()
{
    if (learnProbe == nullptr)
        return;  // Cancel hat gewonnen

    learnProbe->stopThread (2000);
    learnProbe.reset();

    applySettings();  // Transport wiederherstellen (falls enabled)

    const auto callback = std::move (learnCallback);
    learnCallback = nullptr;

    if (callback != nullptr)
        callback (learnResultIp);
}

} // namespace conduit
