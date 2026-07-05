#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include "BufferPool.h"
#include "CaptureChannel.h"
#include "CaptureGate.h"
#include "CaptureSettings.h"
#include "CaptureWriter.h"
#include "InputMeter.h"
#include "PreRollBuffer.h"
#include "SampleClock.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/**
    Engine-Service für das Capture-System — Audio-Pendant zu "Capture MIDI".

    Sitzt als Input-Tap VOR dem Graph: processInputTap() sieht den rohen
    Hardware-Input, bevor clockBus/graph/graphFader den Buffer anfassen.
    Graph-Fades und Modul-Outputs gehören nicht in die Aufzeichnung.

    Puffer-Architektur (Source/Core/Capture/):
      - PreRollBuffer pro Kanal, IMMER aktiv — hält die letzten
        preRollSeconds und überbrückt die Pool-Latenz nach Gate-Open
      - CaptureRingBuffer pro Kanal, aktiv ab erstem Gate-Open — Speicher
        kommt bedarfsgesteuert aus dem BufferPool (SPSC-Handshake)
      - CaptureChannel verbindet beides: Zustandsmaschine, amortisierte
        Pre-Roll-Übernahme, absolute Positionen (SampleClock)

    Reihenfolge im Tap (pro Kanal, entscheidend): erst
    CaptureChannel::process() — die Übernahme liest die ältesten
    Pre-Roll-Inhalte —, DANN PreRollBuffer::write() des aktuellen Blocks.

    Puffersatz-Swap (Handoff-Protokoll für reallocateBuffers bei laufendem
    Audio — angekündigt in Baustein 2): alle Audio-seitigen Puffer leben in
    einem unveränderlichen BufferSet. Der Message Thread baut bei
    Reallokation einen komplett neuen Satz und legt ihn in die
    Exchange-Mailbox (std::atomic<BufferSet*>); ersetzt er dabei einen noch
    nicht abgeholten Satz, zerstört er den sofort (Audio hat ihn nie
    gesehen — kein Pile-up). Der Audio Thread holt den Satz am Tap-Anfang
    ab und quittiert den alten über eine SPSC-Queue zurück; der
    RAM-Wächter-Timer sammelt die Quittungen ein und zerstört die Sätze.
    prepare() läuft mit stehendem Audio (prepareToPlay-Kontrakt) und darf
    deshalb direkt installieren.

    RAM-Wächter [Message Thread, Timer]: vergleicht die Summe committeter
    Puffer (Pool-Segmente + Pre-Rolls) gegen ramLimitGb und reagiert auf
    einen ausgehungerten Pool (Gate offen, aber kein Segment-Budget): pro
    Tick wird der älteste GEHALTENE Kanal freigegeben (atomares Flag, der
    Audio Thread quittiert im nächsten Block). Jeder Wechsel des
    Warnzustands feuert einen ChangeBroadcast für die UI.

    Als ICaptureBufferHost beantwortet der Service die Resize-Policy der
    Settings: Aktivitäts-Status (Gate offen oder held), Invalidierung
    (Gates zu, Puffer verwerfen, bewusst KEIN Auto-Export) und Reallokation.
    Die einzige Ausnahme vom Verwerfen ohne Export ist prepare(): ein
    Device-/Samplerate-Wechsel exportiert aktives Material vorab
    (Sicherheitsnetz — der Wechsel kommt von außen, ohne Rückfrage-Dialog).

    Gate-Detektion (Baustein 4): pro Kanal entscheidet ein CaptureGate im
    Tap — Reihenfolge Meter → Gate → Kanal-Verarbeitung. Öffnet, sobald der
    Block-RMS die effektive Schwelle reißt; schließt erst nach holdMinutes
    durchgehend unter Schwelle − 6 dB (Hold in Samples, nie Wall-Clock).
    Die Gates leben UNABHÄNGIG vom Puffersatz (Detektionszustand, kein
    Speicher) und werden bei Satz-Swap und Invalidate zurückgesetzt. Die
    Gate-API (openGate/closeGate) bleibt public als Test-Seam für Signale
    unterhalb der Schwelle.

    AutoCalibrator [Message Thread, 1-Hz-Tick des Guard-Timers]: publiziert
    die effektive Schwelle in die Kanal-Atomics der Gates — bei
    autoCalibrate max(Settings-Threshold, NoiseFloor + 12 dB), der manuelle
    Threshold wirkt als Override-Untergrenze; ohne autoCalibrate der
    Settings-Threshold direkt.

    Export (Baustein 5): exportAll() [Message Thread] friert pro aktivem
    Kanal (recording/held) die ReadableRange ein und reiht einen Job beim
    CaptureWriter ein — die Aufnahme läuft währenddessen weiter. Damit
    weder ein Puffersatz-Swap noch eine Freigabe dem Writer den Speicher
    unter den Füßen wegzieht, gilt zweistufiges Pinning:
      - Kanal-Ebene: tryBeginExportRead()/endExportRead() (CaptureChannel,
        Dekker-Protokoll — Freigaben werden bei aktiven Lesern aufgeschoben)
      - Satz-Ebene: BufferSet::exportPins (atomar) — ausgemusterte Sätze
        landen in retiredAwaitingDestroy und werden erst zerstört, wenn
        ihre Pins auf null sind (Sweep im Guard-Timer-Tick)
    Job::releaseResources (läuft IMMER auf dem Writer-Thread) löst erst die
    Kanal-Leser, dann den Satz-Pin. Der Writer-Report wird per AsyncUpdater
    auf den Message Thread gehoben (onExportFinished); der Writer-Thread
    wird im Dtor VOR den Puffersätzen gestoppt.

    Virtuelle Kanäle (Capture-Taps aus dem Graph): registerVirtualChannel()
    [MT] vergibt einen von MAX_VIRTUAL_CHANNELS Registry-Slots; im Puffersatz
    liegen die zugehörigen Einträge HINTER den Hardware-Kanälen (Index =
    numChannels + Slot) und nutzen exakt dieselben Pfade (PreRoll, Gate,
    Ring, BufferPool, Auto-Kalibrierung). Hardware + virtuell teilen
    MAX_CAPTURE_CHANNELS, das RAM-Budget der Ring-Dimensionierung und den
    Pool. Sätze werden nur für tatsächlich registrierte Slots dimensioniert —
    ohne Taps ändert sich nichts. Braucht ein neuer Slot Puffer, wird bei
    inaktiven Kanälen still reallokiert (verlustfrei); bei aktiven Kanälen
    wartet die Erweiterung auf den Guard-Timer (needsVirtualExpansion) —
    laufende Aufnahmen werden NIE für einen Tap verworfen, der Tap nimmt
    bis dahin nichts auf.

    writeVirtualChannel() [Audio Thread, aus Modul-processBlock NACH dem
    Tap desselben Callbacks] stempelt mit derselben SampleClock wie die
    Hardware-Kanäle (blockStart = now − numSamples, die Clock tickt am
    Tap-Ende) — Capture All exportiert Hardware- und Tap-Spuren deshalb
    sample-aligned in einem Job. Tap-Spurnamen im Export = registrierter
    Kanal-Name (moduleId) statt "inN".

    Deregistrierung folgt Phase 1 des zweiphasigen Deletes (5.3): der
    Schreibpfad wird sofort atomar getrennt, der Audio Thread schließt das
    Gate im nächsten Block — laufendes Material bleibt als "held" erhalten
    (Export/Reclaim wie Hardware), bekommt aber keine neuen Daten. Der Slot
    wird erst wiedervergeben, wenn sein Kanal wieder idle ist.
*/
class CaptureService : public ICaptureBufferHost,
                       public juce::ChangeBroadcaster,
                       private juce::Timer,
                       private juce::AsyncUpdater
{
public:
    /** Registry-Obergrenze virtueller Kanäle (Capture-Taps). Teilt sich
        MAX_CAPTURE_CHANNELS mit der Hardware — buildSet() clampt. */
    static constexpr int MAX_VIRTUAL_CHANNELS = 8;

    explicit CaptureService (CaptureSettings& settingsToUse);
    ~CaptureService() override;

    /** [Message Thread, aus EngineProcessor::prepareToPlay — Audio steht]
        Sicherheitsnetz Device-/Samplerate-Wechsel: aktives Material
        (recording/held) wird VOR der Invalidierung automatisch exportiert —
        die einzige Ausnahme von "Verwerfen ohne Auto-Export", Begründung
        in der .cpp. Resettet dann die SampleClock (Samplerate-Wechsel
        invalidiert alle Positionen), konfiguriert das Metering und
        installiert einen frischen Puffersatz anhand der Settings. */
    void prepare (double sampleRate, int samplesPerBlock, int numInputChannels);

    /** [Audio Thread] ERSTE Operation in processBlock() — allocation-free,
        lock-free. Misst, puffert (Pre-Roll + aktive Capture-Ringe) und
        taktet den rohen Input. */
    void processInputTap (const juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept;

    //==========================================================================
    // Gate-API [Audio Thread] — die Detektion im Tap ruft sie automatisch;
    // public als Test-Seam (manuelles Öffnen unterhalb der Schwelle)
    void openGate (int channel) noexcept;
    void closeGate (int channel) noexcept;

    //==========================================================================
    // Looper-Arming (Looper-Baustein B1)

    /** [Message Thread] Hält das Gate des Kanals zwangsweise offen: die
        Detektion wird übersteuert (kein Schwellen-Open nötig, kein
        Auto-Close) — "die letzten 8 Takte" der Looper-Quelle existieren
        damit garantiert ab der Quellwahl, rückwirkend ergänzt durch den
        Pre-Roll. captureIndex zählt Hardware-Kanäle und virtuelle Slots
        (numChannels + slot). Entwaffnen lässt die normale Detektion
        weiterlaufen — ein offenes Gate schließt dann regulär über den
        Hold. Der Audio Thread übernimmt im nächsten Block. */
    void setChannelArmed (int captureIndex, bool shouldBeArmed) noexcept;

    [[nodiscard]] bool isChannelArmed (int captureIndex) const noexcept;

    //==========================================================================
    // Virtuelle Kanäle (Capture-Taps aus dem Graph) — Klassendoku oben

    /** Opaker Slot-Verweis — ungültig (slot == -1), wenn die Registry voll
        ist. Nach unregisterVirtualChannel() ist der Handle verbraucht. */
    struct VirtualChannelHandle
    {
        int slot = -1;
        [[nodiscard]] bool isValid() const noexcept { return slot >= 0; }
    };

    /** [Message Thread] Slot vergeben (Spurname für Export/UI = name).
        Ungültiger Handle, wenn alle Slots belegt sind oder noch gehaltenes
        Material tragen. Erweitert den Puffersatz verlustfrei, sobald kein
        Kanal aktiv ist (sonst übernimmt der Guard-Timer). */
    [[nodiscard]] VirtualChannelHandle registerVirtualChannel (const juce::String& name);

    /** [Message Thread, Delete Phase 1 (5.3)] Schreibpfad sofort trennen;
        laufendes Material geht in "held" (Gate-Schließung quittiert der
        Audio Thread im nächsten Block) und bleibt bis Export/Reclaim
        erhalten. Invalidiert den Handle. */
    void unregisterVirtualChannel (VirtualChannelHandle& handle);

    /** [Message Thread] Rename der moduleId → Spurname folgt live. */
    void setVirtualChannelName (VirtualChannelHandle handle, const juce::String& name);

    /** [Audio Thread, 3.1 — aus Modul-processBlock, NACH dem Input-Tap
        desselben Callbacks] Einen Block in den virtuellen Kanal schreiben:
        Meter → Gate → Kanal-Verarbeitung → Pre-Roll, identisch zum
        Hardware-Pfad. No-op bei ungültigem/getrenntem Handle oder solange
        der Puffersatz den Slot (noch) nicht trägt. Höchstens ein Aufruf
        pro Handle und Block. */
    void writeVirtualChannel (VirtualChannelHandle handle,
                              const float* data, int numSamples) noexcept;

    /** UI-Sicht eines Registry-Slots [Message Thread]. */
    struct VirtualChannelUiInfo
    {
        bool inUse = false;       // Modul registriert ODER Material noch gebunden
        juce::String name;        // registrierter Spurname (bleibt für held-Material)
        int captureIndex = -1;    // Index für getChannel()/exportChannel();
                                  // -1 solange der Puffersatz den Slot nicht trägt
    };
    [[nodiscard]] VirtualChannelUiInfo getVirtualChannelUiInfo (int slot) const;

    //==========================================================================
    /** [Message Thread] AutoCalibrator-Tick (1 Hz via Guard-Timer) — public,
        damit Tests ohne Dispatch-Loop kalibrieren können. */
    void runAutoCalibration();

    //==========================================================================
    // Export (Baustein 5) — Schreiben NIE auf dem Audio-Thread

    /** [Message Thread] "Capture All": friert für jeden aktiven Kanal
        (recording/held) den lesbaren Bereich ein und reiht einen Export-Job
        ein; die Aufnahme läuft weiter. Liefert die Zahl der eingereihten
        Spuren (0 = nichts Aktives oder Service nicht prepared). */
    int exportAll();

    /** [Message Thread] Einzel-Capture eines Kanals (CapturePanel-Zeile) —
        dieselbe Pipeline wie exportAll(), nur auf einen Kanal begrenzt. */
    int exportChannel (int channelIndex);

    /** [Message Thread] Externen Export-Job einreihen (M9: Looper-Clip-
        Export der Save-Geste): Bit-Tiefe, Verzeichnis und Take-Nummer
        ergänzt der Service; Writer-Thread und Report-→-MT-Pfad (Toast)
        werden wiederverwendet. Die TrackSources des Jobs sind
        EINGEFRORENE Quellen (ringCapacitySamples == 0, CaptureWriter-
        Doku) — der Caller pinnt sie bis releaseResources. false, wenn
        der Service nicht prepared ist. */
    bool enqueueExternalJob (CaptureWriter::Job&& job);

    /** [Message Thread] Nach Export + User-Bestätigung: die genannten Kanäle
        freigeben, sofern sie (noch) im Zustand held sind — die Quittung
        kommt vom Audio Thread im nächsten Block. */
    void releaseExportedHeldChannels (const std::vector<int>& channelIndices);

    /** [Message Thread] Export-Abschluss — per AsyncUpdater vom
        Writer-Thread gehoben. Der Editor setzt und cleart den Callback. */
    std::function<void (const CaptureWriter::Report&)> onExportFinished;

    /** [Message Thread, beim Enqueue gelesen] Spurname eines HARDWARE-Kanals
        für Export-Dateinamen — der EngineProcessor liefert das sanitierte
        ChannelNames-Label. Unverdrahtet oder leeres Ergebnis → "in{N}". */
    std::function<juce::String (int channel)> hardwareTrackName;

    [[nodiscard]] bool isExportBusy() const noexcept { return writer.isBusy(); }

    /** Toolbar-Aggregat (CaptureAllButton): Status + Füllstand über alle
        Kanäle. [Message Thread] — liest currentSet. */
    struct UiStatus
    {
        bool anyRecording = false;
        bool anyHeld = false;
        bool exporting = false;
        float fillNorm = 0.0f;  // vollster Kanal-Ring, 0..1
    };
    [[nodiscard]] UiStatus getUiStatus() const;

    //==========================================================================
    // ICaptureBufferHost [Message Thread] — Gegenstück der Resize-Policy
    [[nodiscard]] bool isAnyChannelActive() const override;
    void invalidateAllBuffers() override;
    void reallocateBuffers() override;

    //==========================================================================
    /** [Message Thread] RAM-Wächter-Tick — der Timer ruft das periodisch;
        public, damit Tests ohne Dispatch-Loop takten können. */
    void runRamGuard();

    /** Summe committeter Puffer: Pool-Segmente + Pre-Roll-Ringe. */
    [[nodiscard]] std::int64_t getCommittedBytes() const noexcept;

    [[nodiscard]] bool isRamWarningActive() const noexcept
    {
        return ramWarningActive.load (std::memory_order_relaxed);
    }

    //==========================================================================
    // Dimensionierung — pur und allokationsfrei, testbar ohne Allokation

    /** Ring-Kapazität pro Kanal: bufferMinutes bei sampleRate, gedeckelt
        durch ramLimitGb über alle Kanäle. */
    [[nodiscard]] static int computeRingCapacitySamples (int bufferMinutes, double sampleRate,
                                                         int numChannels, int ramLimitGb) noexcept;

    [[nodiscard]] static int computePreRollCapacitySamples (int preRollSeconds,
                                                            double sampleRate) noexcept;

    /** Wie viele Pool-Segmente das RAM-Budget nach Abzug der Pre-Rolls
        hergibt (höchstens eins pro Kanal). */
    [[nodiscard]] static int computeMaxSegments (int ramLimitGb, std::int64_t segmentBytes,
                                                 std::int64_t preRollBytesTotal,
                                                 int numChannels) noexcept;

    /** Hold-Zeit in Samples: holdMinutes × 60 × sampleRate — die Detektion
        zählt Puffer-Samples, nie Wall-Clock. */
    [[nodiscard]] static std::int64_t computeHoldSamples (int holdMinutes,
                                                          double sampleRate) noexcept;

    /** Effektive Gate-Schwelle: bei autoCalibrate max(manuell, Floor + 12 dB),
        sonst der manuelle Threshold. Stille (Floor 0) fällt auf manuell zurück. */
    [[nodiscard]] static float computeEffectiveThresholdDb (float manualThresholdDb,
                                                            float noiseFloorLinear,
                                                            bool autoCalibrate) noexcept;

    /** Wie viele virtuelle Slots ein Satz bei hardwareChannels tragen kann —
        geclampt auf MAX_VIRTUAL_CHANNELS und die geteilte Obergrenze
        MAX_CAPTURE_CHANNELS. */
    [[nodiscard]] static int clampedVirtualSlots (int hardwareChannels, int wanted) noexcept;

    //==========================================================================
    // Status des aktuellen (neuesten) Puffersatzes [Message Thread]
    [[nodiscard]] int getRingCapacitySamples() const noexcept;

    /** NUR Hardware-Kanäle — virtuelle Slots liegen dahinter
        (getVirtualChannelUiInfo liefert deren captureIndex). */
    [[nodiscard]] int getRingNumChannels() const noexcept;

    /** nullptr außerhalb des Kanalbereichs (Hardware + virtuelle Slots).
        Status-Getter des Kanals sind von jedem Thread lesbar; read() folgt
        der Leser-Disziplin. */
    [[nodiscard]] const CaptureChannel* getChannel (int channel) const noexcept;

    /** [NUR Audio Thread] Kanal-Sicht des AUDIO-seitigen Puffersatzes —
        für Audio-Thread-Konsumenten im selben Callback (LooperWaveformTap,
        Looper-Baustein B4): der Input-Tap dieses Blocks ist bereits
        gelaufen, der Ring-Inhalt bis SampleClock::now() vollständig.
        getChannel() liest dagegen die Message-Thread-Sicht (currentSet).
        nullptr außerhalb des Satzes oder vor prepare(). */
    [[nodiscard]] const CaptureChannel* getAudioChannelView (int captureIndex) const noexcept
    {
        auto* set = audioSet;
        if (set == nullptr || captureIndex < 0 || captureIndex >= set->totalEntries())
            return nullptr;

        return set->channels[static_cast<std::size_t> (captureIndex)].get();
    }

    /** nullptr außerhalb von [0, MAX_CAPTURE_CHANNELS). Status und
        effektive Schwelle sind von jedem Thread lesbar. */
    [[nodiscard]] const CaptureGate* getGate (int channel) const noexcept;

    [[nodiscard]] const SampleClock& getSampleClock() const noexcept { return sampleClock; }
    [[nodiscard]] const InputMeter& getInputMeter() const noexcept   { return inputMeter; }

private:
    //==========================================================================
    /** Unveränderlicher Audio-Puffersatz — komplett ersetzt statt mutiert. */
    struct BufferSet
    {
        // Einträge: [0, numChannels) Hardware, dahinter numVirtualSlots
        // virtuelle Kanäle (Registry-Slot s → Index numChannels + s)
        std::vector<std::unique_ptr<PreRollBuffer>> preRolls;
        std::vector<std::unique_ptr<CaptureChannel>> channels;
        BufferPool pool;
        int numChannels = 0;      // Hardware-Kanäle
        int numVirtualSlots = 0;  // gebaute virtuelle Slots (≤ MAX_VIRTUAL_CHANNELS)
        int ringCapacity = 0;     // Samples pro Kanal
        int preRollCapacity = 0;  // Samples pro Kanal

        [[nodiscard]] int totalEntries() const noexcept { return numChannels + numVirtualSlots; }

        // Export-Pinning: > 0 solange ein Writer-Job aus dem Satz liest —
        // ausgemusterte Sätze werden erst bei 0 zerstört
        std::atomic<int> exportPins { 0 };
    };

    BufferSet* buildSet();              // [MT] erzeugt + registriert als currentSet
    void destroySet (BufferSet* set);   // [MT] aus ownedSets entfernen
    void retireSet (BufferSet* set);    // [MT] zum Sweep vormerken (einmalig)
    void drainRetiredSets();            // [MT] Quittungen einsammeln + Sweep
    void timerCallback() override;      // RAM-Wächter + 1-Hz-AutoCalibrator
    void handleAsyncUpdate() override;  // Writer-Reports auf den MT heben

    /** [MT] Gemeinsamer Export-Pfad — onlyChannel = -1 exportiert alle. */
    int enqueueExport (int onlyChannel);

    //==========================================================================
    // Virtuelle Kanäle — Registry (Slot-Indizes stabil über Puffersatz-Swaps)

    /** [MT] Höchster belegter Registry-Slot + 1 (Sätze decken alle
        registrierten Slots ab, Lücken inklusive — Indizes sind stabil). */
    [[nodiscard]] int registeredVirtualSlotCount() const noexcept;

    /** [MT] true, wenn der aktuelle Satz weniger virtuelle Slots trägt als
        registriert sind — die Erweiterung läuft verlustfrei, sobald kein
        Kanal mehr aktiv ist (sofort oder im Guard-Tick). */
    [[nodiscard]] bool needsVirtualExpansion() const noexcept;

    struct VirtualSlot
    {
        // Nur Message Thread
        bool occupied = false;
        juce::String name;  // bleibt nach unregister stehen (Export gehaltenen Materials)

        // Message → Audio
        std::atomic<bool> writerActive { false };     // Modul darf schreiben
        std::atomic<bool> detachRequested { false };  // Gate im nächsten Block schließen
    };
    std::array<VirtualSlot, MAX_VIRTUAL_CHANNELS> virtualSlots;

    static constexpr int guardIntervalMs = 200;
    static constexpr int guardTicksPerCalibration = 5;     // 5 × 200 ms = 1 Hz
    static constexpr float autoCalibrateHeadroomDb = 12.0f;
    static constexpr float silenceFloorDb = -120.0f;        // gainToDecibels-Untergrenze

    CaptureSettings& settings;

    SampleClock sampleClock;
    InputMeter  inputMeter;

    // Nur Message Thread (prepare/realloc)
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    int preparedChannels = 0;

    // Gate-Detektion pro Kanal — lebt unabhängig vom Puffersatz
    std::array<CaptureGate, MAX_CAPTURE_CHANNELS> gates;

    // Looper-Arming [Message schreibt, Audio liest]: gearmte Kanäle halten
    // ihr Gate zwangsweise offen (Detektion übersteuert, forceOpen)
    std::array<std::atomic<bool>, MAX_CAPTURE_CHANNELS> channelArmed {};

    // Im Tap gelesen; nur in prepare() geschrieben (Audio steht)
    double audioSampleRate = 0.0;

    int guardTicksSinceCalibration = 0;  // nur Message Thread (Timer)

    // -- Puffersatz-Handoff (Protokoll siehe Klassendoku) ----------------------
    std::vector<std::unique_ptr<BufferSet>> ownedSets;  // MT: Besitz aller lebenden Sätze
    BufferSet* currentSet = nullptr;                    // MT-Sicht: neuester Satz
    std::atomic<BufferSet*> pendingSet { nullptr };     // Mailbox MT → Audio (exchange)
    SpscQueue<BufferSet*> retiredSets { 8 };            // Quittung Audio → MT
    std::vector<BufferSet*> retiredAwaitingDestroy;     // MT: wartet auf exportPins == 0
    BufferSet* audioSet = nullptr;                      // NUR Audio Thread
                                                        // (Ausnahme: prepare/Dtor, Audio steht)

    // -- Export (Baustein 5) ---------------------------------------------------
    CaptureWriter writer;                // eigener Schreib-Thread (nie Audio)
    int nextTakeNumber = 1;              // nur Message Thread
    juce::CriticalSection reportLock;    // Writer → MT (kein RT-Pfad)
    std::vector<CaptureWriter::Report> pendingReports;

    std::atomic<bool> invalidateRequested { false };

    // Gate-Status [Audio schreibt pro Block, Message liest für die Policy]
    std::atomic<bool> anyChannelActive { false };

    std::atomic<bool> ramWarningActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureService)
};

} // namespace conduit
