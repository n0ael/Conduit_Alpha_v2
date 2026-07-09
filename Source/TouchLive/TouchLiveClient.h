#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <juce_events/juce_events.h>
#include <juce_osc/juce_osc.h>

#include "IRemoteTransport.h"
#include "LiveSetModel.h"
#include "TouchLiveMeterBus.h"
#include "TouchLiveSettings.h"

namespace conduit
{

//==============================================================================
/**
    Netzwerk-Client der TouchLive-Remote (docs/TouchLive.md §3) — eigener
    OSC-Kanal (Command-Port 9010 → Script, Listen-Port 9011 ← Script),
    KOMPLETT getrennt von OscController/OscSendService. Reiner
    Message-Thread-Code — der Audio-Thread ist an der Remote NIE beteiligt.

    Lifecycle: enable → pro Domain /remote/state/{d}/get + /subscribe →
    Snapshots/Diffs ins LiveSetModel. Heartbeat /remote/ping alle 2 s;
    3 verpasste Pongs → Status disconnected. Die Ping-Kadenz IST der
    2-s-Reconnect-Backoff (UDP kennt keinen Verbindungsaufbau); JEDER
    Übergang zu connected subscribed neu — so heilen auch beim Enable
    verlorene Subscribes, der doppelte Start-Snapshot ist gewollt billig.

    State-Sync (Wire-Format der Gegenseite, Tools/Live/ConduitRemote):
    /remote/state/{d}/snapshot|diff [seq:int, chunk:int, chunks:int, json:str].
    Chunks gleicher seq werden erst komplett gesammelt (Reassembly), dann als
    EIN Payload angewendet. Seq-Lücke pro Domain → Snapshot-Re-Request via
    /get (höchstens einer pro Heartbeat-Intervall — verlorene Requests heilt
    der nächste Tick).

    Threading: Der Transport liefert Messages auf dem Netzwerk-Thread; sie
    werden gequeued und via AsyncUpdater auf dem Message Thread angewendet
    (Muster OscController — gleiche Garantie wie MessageManager::callAsync,
    aber synchron testbar über flushPendingMessages()).

    Touch-Pfad (§5.1): sendTouchValue() thinned pro Adresse auf max. einen
    Send pro ~16 ms, der letzte Wert gewinnt. Echo-Suppression:
    noteTouchedParameter (Key = Domain + Stable-ID + Feld) verwirft
    eingehende Werte für berührte Keys bis 250 ms nach der letzten Berührung.
*/
class TouchLiveClient final : public juce::ChangeBroadcaster,
                              private juce::MultiTimer,
                              private juce::ChangeListener,
                              private juce::AsyncUpdater
{
public:
    enum class Status { disabled, connecting, connected, disconnected };

    static constexpr int heartbeatIntervalMs = 2000;
    static constexpr int maxMissedPongs = 3;
    static constexpr double touchThinningIntervalMs = 16.0;
    static constexpr double echoSuppressionReleaseMs = 250.0;

    /** Abonnierte Domains (docs/TouchLive.md §2; devices seit M3). */
    static constexpr std::array<const char*, 5> domainNames { "transport", "tracks",
                                                              "mixer", "session",
                                                              "devices" };

    /** transport nullptr → UDP (App-Pfad); Tests injizieren einen Fake.
        Lauscht auf die Settings und verbindet sofort, falls enabled.
        meterBus empfängt den Hochraten-Pfad /remote/meters (M2). */
    TouchLiveClient (LiveSetModel& modelToUse,
                     TouchLiveMeterBus& meterBusToUse,
                     TouchLiveSettings& settingsToUse,
                     std::unique_ptr<IRemoteTransport> transportToUse = nullptr);

    ~TouchLiveClient() override;

    [[nodiscard]] Status getStatus() const noexcept { return status; }

    //==========================================================================
    // Commands — Message Thread

    /** Ein Command an das Script (AbletonOSC-Schema, README der Gegenseite).
        false wenn disabled oder Transport nicht verbunden. */
    bool sendCommand (const juce::OSCMessage& message);

    /** Hochraten-Touch-Pfad: max. ein Send pro ~16 ms pro Adresse, der
        letzte Wert gewinnt (Nachzügler flusht der Thinning-Timer). */
    void sendTouchValue (const juce::OSCMessage& message);

    //==========================================================================
    // Echo-Suppression — Message Thread

    /** Bei jedem Touch-Event rufen: eingehende Diffs für den Key werden bis
        250 ms nach dem letzten Aufruf verworfen. */
    void noteTouchedParameter (const juce::String& suppressionKey);

    /** Key-Schema Domain + Stable-ID + Feld, z. B. ("mixer", "tr:3", "volume").
        Für Skalar-Domains ohne Stable-ID ist der Top-Level-Key das Feld:
        ("transport", "tempo"). */
    [[nodiscard]] static juce::String makeParameterKey (const juce::String& domainName,
                                                        const juce::String& key,
                                                        const juce::String& field = {});

    //==========================================================================
    // IP-Learn (Learn-Probe-Muster aus docs/OscSend.md) — Message Thread

    /** Callback der Learn-Probe: IP des Live-Rechners, leer bei Timeout oder
        Bind-Fehler. Läuft auf dem Message Thread; der Transport ist beim
        Aufruf bereits wiederhergestellt. */
    using LearnCallback = std::function<void (const juce::String& senderIp)>;

    /** Startet die Learn-Probe. Das Script sendet NIE spontan — deshalb
        broadcastet die Probe periodisch /remote/ping an den Command-Port
        (255.255.255.255 + konfigurierter Host) und liest die Absender-IP
        des ersten Antwortpakets am freigegebenen Listen-Port.
        false, wenn schon im Learn-Modus. */
    [[nodiscard]] bool beginIpLearn (LearnCallback callback, int timeoutMs = 15000);

    /** Bricht die Learn-Probe ab und stellt den Transport wieder her —
        der Callback feuert danach nicht mehr. No-op ohne aktive Probe. */
    void cancelIpLearn();

    [[nodiscard]] bool isLearning() const noexcept { return learnProbe != nullptr; }

    //==========================================================================
    // Test-Seams — Message Thread

    /** Wendet gequeuete Netzwerk-Messages sofort an (statt AsyncUpdater). */
    void flushPendingMessages();

    /** Führt einen Heartbeat-Tick synchron aus (der 2-s-Timer ruft dieselbe
        Logik). */
    void runHeartbeatTick();

    /** Flusht fällige gedrosselte Touch-Sends (der Thinning-Timer ruft
        dieselbe Logik). */
    void flushPendingTouchValues();

    /** Fake-Clock für Thinning + Echo-Suppression (Millisekunden). */
    void setTimeSource (std::function<double()> nowMsToUse);

    //==========================================================================
    /** Diagnose-Zähler (Feel-Messlatte §5.1: „erst Raten messen, dann
        schrauben") — kumulativ seit Enable, nur Message Thread. */
    struct Stats
    {
        std::int64_t snapshotsApplied = 0;
        std::int64_t diffsApplied = 0;
        std::int64_t meterFrames = 0;
        std::int64_t touchValuesSent = 0;
    };

    [[nodiscard]] const Stats& getStats() const noexcept { return stats; }

private:
    //==========================================================================
    enum TimerIds { heartbeatTimerId = 1, thinningTimerId = 2 };

    /** Sync-Zustand einer Domain (nur Message Thread). */
    struct DomainSync
    {
        int lastAppliedSeq = -1;
        bool snapshotRequested = false;  // Re-Request-Drossel, Reset pro Heartbeat-Tick

        struct Reassembly
        {
            int seq = -1;
            int expectedChunks = 0;
            bool isSnapshot = false;
            std::map<int, juce::String> parts;  // chunkIndex → JSON
        };

        Reassembly pending;
    };

    //==========================================================================
    void timerCallback (int timerId) override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void handleAsyncUpdate() override;

    /** Settings → Transport: (re)connect/disconnect + Timer + Subscribe. */
    void applySettings();

    void setStatus (Status newStatus);
    void subscribeAll();
    void requestSnapshot (const juce::String& domainName, DomainSync& sync);

    // Eingehende Messages [Message Thread nach dem Queue-Hop]
    void routeMessage (const juce::OSCMessage& message);
    void handlePong();
    void handleMeters (const juce::OSCMessage& message);
    void handleStatePayload (const juce::String& domainName, bool isSnapshot,
                             const juce::OSCMessage& message);
    void applyPayload (const juce::String& domainName, bool isSnapshot,
                       int seq, const juce::var& payload);

    [[nodiscard]] bool isSuppressed (const juce::String& domainName,
                                     const juce::String& key,
                                     const juce::String& field);

    void finishIpLearn();

    //==========================================================================
    class LearnProbe;

    LiveSetModel& model;
    TouchLiveMeterBus& meterBus;
    TouchLiveSettings& settings;
    std::unique_ptr<IRemoteTransport> transport;

    Status status = Status::disabled;
    bool transportConnected = false;
    int pingsSinceLastPong = 0;
    Stats stats;

    std::map<juce::String, DomainSync> domainSync;

    // Netzwerk-Thread schreibt, Message Thread drained (AsyncUpdater)
    juce::CriticalSection incomingLock;
    std::vector<juce::OSCMessage> pendingIncoming;

    // Touch-Pfad + Echo-Suppression (nur Message Thread)
    std::function<double()> nowMs;
    std::map<juce::String, double> lastTouchSendMs;              // Adresse → Zeit
    std::map<juce::String, juce::OSCMessage> pendingTouchValues; // Adresse → letzter Wert
    std::map<juce::String, double> touchedUntilMs;               // Suppression-Key → Ablauf

    // IP-Learn (Muster OscController)
    std::unique_ptr<LearnProbe> learnProbe;
    LearnCallback learnCallback;
    juce::String learnResultIp;
    std::atomic<bool> learnDone { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveClient)
};

} // namespace conduit
