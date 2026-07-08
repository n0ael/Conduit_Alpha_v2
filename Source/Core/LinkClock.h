#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_events/juce_events.h>

#include "Interfaces/IClockSource.h"

namespace conduit
{

//==============================================================================
/**
    Clock-Master auf Basis einer Ableton-Link-Session (CLAUDE.md 2/13.1).

    Pimpl: der Link-/asio-Header bleibt in der .cpp — er zieht plattform-
    spezifische Netzwerk-Header, die nicht in Projekt-Header gehören.

    Basis ist ableton::LinkAudio, das ableton::Link vollständig ERSETZT
    (CLAUDE.md 7.2 — nie beide Klassen parallel). Timing-Verhalten ist
    identisch zu Link; Audio-Sharing ist initial deaktiviert und wird erst
    vom ersten Send-Modul via enableAudio(true) aktiviert.

    Link ist ab Konstruktion aktiv (Peer-Discovery im lokalen Netz) inkl.
    Start/Stop-Sync. Ohne Peers verhält sich die Session wie eine lokale
    Clock — der Session-Beat läuft auf der Wall-Clock weiter.

    ChangeBroadcaster: benachrichtigt (auf dem Message Thread), wenn sich
    die in der Session verfügbaren Audio-Kanäle ändern — der zugrunde
    liegende ChannelsChangedCallback kommt auf einem Link-Thread und wird
    via MessageManager::callAsync gemarshallt (CLAUDE.md 7.2 Threading).

    Thread-Ownership:
      - ctor/dtor, setTempo(), getTempo(), getNumPeers() → Message Thread
        (Link kapselt die Synchronisation mit seinen Netzwerk-Threads)
      - enableAudio(), peerName(), setPeerName(), createSink()
                              → Message Thread
      - isAudioEnabled()      → beliebiger Thread (Link-Garantie, RT-safe)
      - prepare()             → Message Thread, Audio gestoppt
      - captureClockState()   → Audio Thread, lock-free (Link-Garantie)
*/
class LinkClock final : public IClockSource,
                        public juce::ChangeBroadcaster,
                        private juce::AsyncUpdater
{
public:
    explicit LinkClock (double initialBpm = 120.0,
                        const juce::String& peerName = "Conduit");
    ~LinkClock() override;

    /** Vor Audio-Start (EngineProcessor::prepareToPlay). */
    void prepare (double sampleRate) noexcept;

    //==========================================================================
    // Message Thread
    void setTempo (double bpm);
    [[nodiscard]] double getTempo() const;
    [[nodiscard]] std::size_t getNumPeers() const;

    //==========================================================================
    // Link-Transport (Start/Stop-Sync) — Message Thread

    /** Start/Stop-Sync der Session (Konstruktor-Default: an). Aus = Play
        wirkt nur lokal, Peers bleiben unberührt. */
    void setStartStopSyncEnabled (bool enabled);
    [[nodiscard]] bool isStartStopSyncEnabled() const;

    /** Committet den Transport-Wunsch in die Session — mit aktivem
        Start/Stop-Sync starten/stoppen alle Peers (inkl. Ableton). */
    void requestIsPlaying (bool shouldPlay);
    [[nodiscard]] bool isPlaying() const;

    /** Session-Beat JETZT (inkl. Clock-Offset) — für die Positions-Anzeige
        des Headers. Message Thread (die Audio-Seite nutzt captureClockState). */
    [[nodiscard]] double getBeatPosition() const;

    //==========================================================================
    // Clock-Offset (User-Wunsch 2026-07-02, Muster Latenz-Trim 8.3)

    /** Versatz der Beat-Ablesung in Millisekunden (±100, geclamped) —
        gleicht Interface-/Peer-Latenz gegen andere Link-Peers an.
        Message Thread; der Audio-Thread liest das Atomic pro Block in
        captureClockState() (beatAtTime auf der verschobenen Zeitbasis). */
    void setClockOffsetMs (double offsetMs);
    [[nodiscard]] double getClockOffsetMs() const noexcept;

    //==========================================================================
    // Link Audio (CLAUDE.md 7.2) — Message Thread

    /** Refcount-Semantik: jedes aktive Send-Modul ruft enableAudio(true) bei
        Aktivierung und enableAudio(false) bei Deaktivierung (Phase 1 des
        zweiphasigen Delete). Link-Audio ist enabled, solange der Zähler > 0
        ist — das erste Modul aktiviert, das letzte deaktiviert.

        Das FINALE Deaktivieren (Zähler → 0) läuft um einen Message-Loop-Hop
        verzögert (AsyncUpdater): enableLinkAudio(false) postet Bye-Arbeit auf
        den Link-IO-Thread; wird die LinkAudio-Instanz kurz danach destruiert
        (App-Shutdown), läuft diese Arbeit gegen bereits zerstörte Callback-
        Member — std::bad_function_call → terminate/abort (SDK-Teardown-Race,
        Stacktrace-Diagnose 02.07.2026: Controller::setChannelsChangedCallback
        postet den Reset async, die IO-Queue arbeitet FIFO — die zuvor
        gequeute Bye-Arbeit trifft den noch alten Callback). Mit dem Hop
        deaktiviert der laufende Betrieb einen Loop-Durchlauf später
        (Idle-Sinks sind gratis, 7.2); beim Shutdown steht der Loop und der
        ~LinkAudio-Teardown übernimmt racefrei (sein Callback-Reset liegt
        VOR der Teardown-Arbeit in der IO-Queue).
    */
    void enableAudio (bool shouldBeEnabled);

    /** Test-Seam: verzögertes Deaktivieren synchron ausführen (Message
        Thread) — für Assertions direkt nach dem letzten enableAudio(false). */
    void flushPendingAudioState();

    /** Beliebiger Thread, RT-safe (Link-Garantie). */
    [[nodiscard]] bool isAudioEnabled() const;

    /** Peer-Name zur Identifikation in der Link-Session (Default "Conduit"). */
    [[nodiscard]] juce::String peerName() const;
    void setPeerName (const juce::String& name);

    //==========================================================================
    /**
        Opaker Handle auf einen announceten Audio-Kanal (Sink) der Session.

        Design-Entscheidung: createSink() liefert diesen Pimpl-Wrapper statt
        ableton::LinkAudioSink direkt — so bleibt der Link-/asio-Header in
        der .cpp gekapselt (IWYU-Falle 7.2) und kein Projekt-Header zieht
        Netzwerk-Header. Die RT-Schreib-API (BufferHandle::commit) kommt im
        nächsten Schritt mit dem LinkAudioSendModule auf diesen Wrapper.

        Lifecycle: ein Sink darf die LinkClock nicht überleben. Kanal-Name
        == moduleId (7.2); der announcete Kanal verschwindet mit der
        Destruktion des Sinks (Phase 1 des zweiphasigen Delete — sonst
        Zombie-Kanäle bei den Peers).

        Methoden: Message Thread, außer commitFromClockState (Audio Thread)
        und getMaxNumSamples (beliebiger Thread, Link-Garantie).
    */
    class Sink final
    {
    public:
        ~Sink();

        [[nodiscard]] juce::String getName() const;

        /** Rename wird live zu den Peers propagiert (7.2). Parallel zum
            RT-Commit unkritisch: der Name liegt Link-intern hinter einem
            util::Locked, der Buffer-Pfad fasst ihn nicht an. */
        void setName (const juce::String& newName);

        /** Aktuelle Buffer-Kapazität in SAMPLES (Frames × Kanäle, 7.2). */
        [[nodiscard]] std::size_t getMaxNumSamples() const noexcept;

        /** Kapazität anheben (in SAMPLES) — No-op, wenn kleiner als die
            aktuelle (Link-Semantik). Für Re-Prepare mit größerem Block. */
        void requestMaxNumSamples (std::size_t numSamples) noexcept;

        //======================================================================
        enum class CommitResult
        {
            committed,   // Buffer geschrieben + committed — Peers streamen
            noBuffer,    // kein Buffer verfügbar: kein Subscriber ODER Queue
                         // voll (Overrun) — die Link-API unterscheidet das
                         // nach außen nicht
            rejected     // Größe > Kapazität oder kein captureClockState in
                         // diesem Callback — Programmierfehler, nie im Betrieb
        };

        /** RT-Schreibpfad (CLAUDE.md 7.2) — NUR Audio Thread, RT-safe.

            Muss im selben Audio-Callback NACH LinkClock::captureClockState()
            laufen (der EngineProcessor füllt den ClockBus vor dem Graph-
            Render): commit() nutzt den dort gestashten SessionState plus
            Beat/Quantum-Basis — exakt die des lokalen Renderings, kein
            zweites captureAudioSessionState im Modul.

            interleavedSamples: numFrames × numChannels Samples, interleaved,
            16-bit signed (Dither macht der Aufrufer, 7.2). clock ist der
            ClockState des Blocks (beatAtBlockStart, sampleRate). */
        [[nodiscard]] CommitResult commitFromClockState (const std::int16_t* interleavedSamples,
                                                         int numFrames, int numChannels,
                                                         const ClockState& clock) noexcept;

    private:
        friend class LinkClock;
        struct Impl;
        explicit Sink (std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sink)
    };

    /** Announced einen Audio-Kanal in der Session. maxNumSamples ist in
        SAMPLES, nicht Frames: samplesPerBlock * numChannels (7.2).
    */
    [[nodiscard]] std::unique_ptr<Sink> createSink (const juce::String& name,
                                                    std::size_t maxNumSamples);

    //==========================================================================
    // Link Audio Receive (CLAUDE.md 7.2, Schritt 3) — Message Thread

    /** Header-sichere, opake Kanal-Identität: die 8-Byte-Link-NodeId als
        std::uint64_t gepackt (Big-Endian). Persistent für die Lebensdauer
        des Kanals (7.2 — Names ändern sich, IDs nicht). 0 == ungültig.

        Bewusst NICHT serialisierbar: ChannelIds werden pro Session neu
        vergeben — Peer-Kanäle sind discoverbar, nie Teil des Patches
        (CLAUDE.md 6, v1-Phantom-Connection-Lektion). */
    using ChannelKey = std::uint64_t;

    /** Ein in der Session announceter Audio-Kanal. name/peerName nur zur
        Anzeige, id ist der stabile Schlüssel. */
    struct ChannelInfo
    {
        ChannelKey   id { 0 };
        juce::String name;
        juce::String peerName;
    };

    /** Aktuell in der Session verfügbare Audio-Kanäle. Die Liste speist
        sich aus den Netzwerk-Announcements der PEERS (Link-SDK
        Channels::sawAnnouncement) — eigene Sinks erscheinen NICHT, und
        ohne verbundenen Peer ist sie leer (empirisch verifiziert
        08.07.2026, Receive-Testlauf). Änderungen melden sich über den
        ChangeBroadcaster (ChannelsChanged, 7.2). */
    [[nodiscard]] std::vector<ChannelInfo> availableChannels() const;

    //==========================================================================
    /**
        Opaker Handle auf einen abonnierten Audio-Kanal (Source) der Session
        — das Empfangs-Gegenstück zu Sink (7.2).

        Design wie Sink: Pimpl-Wrapper, damit der Link-/asio-Header in der
        .cpp bleibt. Der Empfangs-Callback läuft auf einem Link-Thread und
        rechnet dort bereits das Beat-Alignment (Info::beginBeats gegen den
        aktuellen SessionState + quantum) — der Aufrufer bekommt einen
        beat-gestempelten Buffer und muss nie selbst captureAudioSessionState
        aufrufen (7.2). Die Samples sind NUR während des Callbacks gültig.

        Lifecycle: eine Source darf die LinkClock nicht überleben. Der
        Callback kann bis zur Destruktion der Source feuern (Link-Thread) —
        Teardown-Race gegen den Audio-Thread löst das empfangende Modul über
        das zweiphasige Delete (5.3), analog zum Sink-Retire.
    */
    class Source final
    {
    public:
        ~Source();

        [[nodiscard]] ChannelKey channelId() const noexcept;

        /** Ein empfangener Buffer. Gültig NUR während des Callbacks
            (Link-Thread) — der Empfänger kopiert synchron heraus. */
        struct ReceivedBuffer
        {
            const std::int16_t* interleavedSamples;  // numFrames × numChannels
            int    numFrames;
            int    numChannels;
            double sampleRate;
            double tempo;           // Session-Tempo beim Senden (Info.tempo) —
                                    // ergibt die Buffer-Dauer im Beat-Raum
            std::uint64_t count;    // Sequenznummer des Senders (Lücken-Diagnose)

            /** Lokale Beat-Zeit am Buffer-Anfang (beginBeats gegen den
                aktuellen SessionState, quantum). nullopt: Buffer aus einer
                FREMDEN Link-Session — nicht alignbar, der Empfänger verwirft
                ihn (nie naiv FIFO'en, v1-Drift-Lektion 7.2). */
            std::optional<double> beatAtBufferBegin;
        };

        /** Callback läuft auf einem Link-Thread (7.2). */
        using ReceiveCallback = std::function<void (const ReceivedBuffer&)>;

    private:
        friend class LinkClock;
        struct Impl;
        explicit Source (std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Source)
    };

    /** Abonniert einen Session-Kanal. callback feuert auf einem Link-Thread,
        sobald ein Buffer eintrifft. channel == 0 oder unbekannt → die Source
        entsteht trotzdem, empfängt aber nichts, bis der Kanal auftaucht. */
    [[nodiscard]] std::unique_ptr<Source> createSource (ChannelKey channel,
                                                        Source::ReceiveCallback callback);

    //==========================================================================
    // Audio Thread
    [[nodiscard]] ClockState captureClockState (int numSamples) noexcept override;

private:
    static constexpr double quantum = 4.0;  // 4/4 — Phase-Sync auf Takt-Ebene

    // Verzögertes enableLinkAudio(false) — Begründung an enableAudio()
    void handleAsyncUpdate() override;

    struct Impl;
    std::unique_ptr<Impl> impl;

    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<double> clockOffsetMicros { 0.0 };  // Message schreibt, Audio liest
    int audioRefCount = 0;  // nur Message Thread (enableAudio)

    JUCE_DECLARE_WEAK_REFERENCEABLE (LinkClock)
};

} // namespace conduit
