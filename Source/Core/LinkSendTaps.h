#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "LinkClock.h"

namespace conduit
{

//==============================================================================
/**
    Wiederverwendbare Link-Audio-Send-Mechanik (CLAUDE.md 7.2) — extrahiert aus
    dem LinkAudioSendModule, damit Module UND der EngineProcessor eingebettete
    Send-Taps tragen können (Input-Send, später Mixer/FX pre/post).

    Ein Tap = ein announcter Link-Kanal. Der Tap-PUNKT ist Sache des Aufrufers:
    er ruft im Audio-Thread commit() mit dem Signal, das gesendet werden soll —
    pre/post ist damit reine Aufrufstellen-Frage, der Kanal selbst bleibt
    identisch (Kernanforderung: der Ableton-Stream reißt bei Umschalten/
    Umbenennen nicht ab; nur setName()/setWidth() ändern sich am LEBENDEN Sink).

    Sink-Kapazität ist immer samplesPerBlock × 2 SAMPLES — ein Tap trägt mono
    und stereo, setWidth() schaltet ohne Sink-Neuanlage um (BufferHandle::
    commit nimmt numChannels pro Commit, LinkAudio.hpp).

    Lifecycle & Threading:
      - createTap()/retireTap()/retireAll()/setName()/setWidth()/prepare()
        → Message Thread. createTap liefert nullptr ohne LinkClock (Tests).
      - Tap-Objekte leben als Pool bis zur LinkSendTaps-Destruktion — der
        Aufrufer darf rohe Tap* über retireTap() hinaus halten (commit/
        noteIdle auf einem retirten Tap sind harmlos: rtSink == nullptr).
        retireTap() gibt nur den SINK in die Retire-Liste; createTap()
        reaktiviert inaktive Pool-Einträge (stabile Adressen).
      - Epoch-Handshake (Muster LinkAudioSendModule/5.2): retireTap stored
        rtSink = nullptr (seq_cst) und liest die Block-Epoche; die Sink-
        Destruktion wartet per AsyncUpdater-Self-Re-Dispatch, bis der
        Audio-Thread nach dem Store einen neuen Block begonnen hat
        (noteBlockBegin), mit 100-ms-Deadline für gestopptes Audio.
      - enableAudio-Refcount der LinkClock: erster aktiver Tap aktiviert,
        letzter retirte Tap deaktiviert; der Destruktor balanciert auch ohne
        vorheriges retireAll (Preset-Load/Shutdown ohne Phase 1, 5.3).
      - Der Besitzer garantiert, dass zur LinkSendTaps-Destruktion kein
        Audio-Callback mehr läuft (Modul: Graph hält keine Render-Referenz
        mehr; EngineProcessor: Member-Reihenfolge).

    Audio-Pfad (3.1, allocation-/lock-frei): noteBlockBegin() EINMAL pro
    Callback VOR allen commits (Handshake-Paar), dann pro Tap commit() —
    TPDF-Dither (inline LCG) in den vorallokierten Tap-Buffer → interleaved
    16-bit → Sink::commitFromClockState mit dem ClockState des Blocks
    (muss im selben Callback NACH LinkClock::captureClockState laufen).
*/
class LinkSendTaps final : private juce::AsyncUpdater
{
public:
    LinkSendTaps();
    ~LinkSendTaps() override;

    enum class Status : int { offline = 0, announced = 1, streaming = 2 };

    //==========================================================================
    class Tap final
    {
    public:
        //======================================================================
        // Message Thread

        /** Vollständiger Kanal-Name (inkl. Präfix des Aufrufers) — live zu
            den Peers propagiert, der Stream läuft weiter (7.2). */
        void setName (const juce::String& fullChannelName);

        /** Aktueller Sink-Name; leer, wenn der Tap retired ist. */
        [[nodiscard]] juce::String getSinkName() const;

        /** Kanalbreite (1 mono / 2 stereo) am LEBENDEN Sink umschalten —
            kein Sink-Neuanlegen, die Kapazität (block × 2) trägt beides. */
        void setWidth (int newWidth) noexcept;
        [[nodiscard]] int getWidth() const noexcept;

        /** Sink announcet (nicht retired)? */
        [[nodiscard]] bool isActive() const noexcept;

        /** Sink-Kapazität in SAMPLES — Diagnose/Tests. */
        [[nodiscard]] std::size_t getSinkCapacity() const noexcept;

        //======================================================================
        // Audio Thread

        /** channelData: getWidth() Kanal-Pointer, numFrames Samples. Läuft im
            selben Callback NACH captureClockState (Sink-Doku). Setzt den
            Status: offline (retired), announced (kein Subscriber/kein Platz),
            streaming (committed). */
        void commit (const float* const* channelData, int numFrames,
                     const ClockState& clock) noexcept;

        /** Kein Commit in diesem Block (z.B. Kanäle fehlen): Status announced
            halten statt fälschlich streaming/offline. */
        void noteIdle() noexcept;

        //======================================================================
        /** Beliebiger Thread (atomic). */
        [[nodiscard]] Status getStatus() const noexcept;

    private:
        friend class LinkSendTaps;
        Tap() = default;

        std::unique_ptr<LinkClock::Sink> sink;              // Message Thread owned
        std::atomic<LinkClock::Sink*>    rtSink { nullptr };  // Audio Thread liest
        std::atomic<int>                 width  { 1 };
        std::atomic<int>                 status { static_cast<int> (Status::offline) };
        std::uint32_t                    ditherState = 0x6c078965u;  // nur Audio Thread
        std::vector<std::int16_t>        interleaved;       // block × 2, prepare()

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tap)
    };

    //==========================================================================
    // Message Thread

    /** Vor dem ersten createTap. clock darf nullptr sein (Tests). */
    void setLinkClock (LinkClock* clock);

    /** Blockgröße setzen/anheben: Sink-Kapazitäten (wächst-nur) und
        Interleave-Buffer aller Taps. Audio gestoppt (prepareToPlay-Pfad). */
    void prepare (int samplesPerBlock);

    /** Announced einen Kanal. Reaktiviert einen inaktiven Pool-Eintrag oder
        legt einen neuen an (stabile Adressen). nullptr ohne LinkClock.
        Der erste aktive Tap aktiviert den enableAudio-Refcount. */
    [[nodiscard]] Tap* createTap (const juce::String& fullChannelName, int width);

    /** Phase 1 (5.3): Audio-Thread sofort per Atomic getrennt, der Sink geht
        in die Retire-Liste (Epoch-Handshake); der Tap bleibt als inaktiver
        Pool-Eintrag gültig. Letzter aktiver Tap gibt den Refcount frei. */
    void retireTap (Tap* tap);

    /** retireTap für alle aktiven Taps. */
    void retireAll();

    [[nodiscard]] bool isRetirePending() const noexcept;
    void flushPendingRetirement();   // Handshake synchron abschließen (Tests)

    [[nodiscard]] int getNumActiveTaps() const noexcept;

    //==========================================================================
    // Audio Thread

    /** EINMAL pro Audio-Callback VOR allen commits: Handshake-Paar
        (seq_cst-Inkrement vor den rtSink-Loads). */
    void noteBlockBegin() noexcept;

    //==========================================================================
    /** Float → Int16 mit TPDF-Dither (CLAUDE.md 3.1/7.2): LCG-basiert,
        deterministisch pro Seed, ±1 LSB Dreieck um 0. Static + pure für die
        Dither-Statistik-Tests (13.4). */
    static void convertToInt16Tpdf (const float* const* channelData,
                                    int numChannels, int numFrames,
                                    std::int16_t* dest,
                                    std::uint32_t& ditherState) noexcept;

private:
    void handleAsyncUpdate() override;
    void disableAudioOnce();

    //==========================================================================
    // Message Thread
    juce::WeakReference<LinkClock> linkClock;
    int samplesPerBlock = 1;
    bool audioEnabled = false;
    int activeTaps = 0;

    // Pool: Tap-Objekte leben bis zur Destruktion (unique_ptr — Taps halten
    // Atomics, der Vektor braucht stabile Adressen für die Aufrufer).
    std::vector<std::unique_ptr<Tap>> taps;

    // Retire-Handshake (gebündelt über alle retirten Sinks)
    std::vector<std::unique_ptr<LinkClock::Sink>> retiredSinks;
    std::uint64_t retireEpoch = 0;
    std::uint32_t retireDeadlineMs = 0;
    static constexpr std::uint32_t retireGraceMs = 100;

    // Handshake-Paar (seq_cst)
    std::atomic<std::uint64_t> blocksProcessed { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkSendTaps)
};

} // namespace conduit
