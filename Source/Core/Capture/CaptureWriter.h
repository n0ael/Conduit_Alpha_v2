#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace conduit
{

//==============================================================================
/**
    Export-Backend des Capture-Systems — Schreiben passiert NIE im Audio-Thread.

    Jobs kommen vom Message Thread über enqueueJob() (Lock + notify(), hier
    erlaubt); geschrieben wird auf diesem dedizierten juce::Thread. Die
    Aufnahme läuft währenddessen weiter: der Writer liest hinter dem
    Schreib-Cursor (SPSC-Leser-Disziplin des CaptureRingBuffer) bis zur
    eingefrorenen endPosition, der Audio Thread schreibt dahinter weiter.

    ALIGNMENT (Kern-Feature): exportStart = min(startPosition aller
    Job-Kanäle). Jede Datei bekommt padSamples = start − exportStart
    Null-Samples vorangestellt und bext TimeReference = exportStart —
    alle Dateien beginnen damit am selben absoluten Sample. DAW-Import
    (Live/Reaper) liegt samplegenau übereinander, später gestartete Spuren
    beginnen mit Stille, die BWF-Position wird von der DAW erkannt.

    Überholschutz (dokumentiertes Protokoll): Der Ring ist groß genug
    dimensioniert (Minuten an Material gegen Sekunden Schreibdauer —
    15 min bei 48 kHz sind ~43 M Samples, der Writer streamt das in
    einstelligen Sekunden, während der Live-Cursor nur 48 k Samples/s
    vorrückt). Trotzdem: die Chunk-Vergabe priorisiert IMMER das
    Job-Teilstück mit dem vollsten Ring (drohender Overrun zuerst), und
    vor jedem Chunk wird der Headroom geprüft — fällt er unter die
    Sicherheitsmarge (Kapazität / overrunMarginDivisor), bricht der Kanal
    ab, BEVOR sich Leser und Live-Schreiber räumlich überlappen.
    CaptureChannel::read() validiert zusätzlich nach dem Kopieren.
    Gegenstück im CaptureService: Snapshots noch LAUFENDER Aufnahmen werden
    beim Einreihen auf Kapazität − 2×Marge gekürzt (voller Ring würde sonst
    sofort am Margin-Check scheitern); held-Quellen behalten alles.
    Quellen mit ringCapacitySamples == 0 (eingefrorene Test-/FIFO-Quellen)
    sind vom Schutz ausgenommen.

    Format & Robustheit: BWF via juce::WavAudioFormat (RF64 schreibt JUCE
    ab 4 GB automatisch), Bit-Tiefe aus dem Job. Dateien werden vorab in
    Blöcken auf Zielgröße allokiert (ENOSPC fällt früh auf, keine
    Fragmentierung) und am Ende exakt gekürzt; der Header wird alle 10 s
    geflusht — ein Crash kostet höchstens 10 s Material. Schreib-/Lese-
    fehler brechen NUR den betroffenen Job-Kanal ab (Datei wird gelöscht),
    übrige Kanäle werden fertiggestellt; das Ergebnis steht im Report.

    Dateiname: {yyyy-MM-dd_HHmmss}_{in{N}|stripName}_{take}.wav

    Live-FIFO-Vorsorge: TrackSource ist eine schmale funktionale
    Schnittstelle (read / getCurrentEnd / Kapazität) — ein kontinuierlicher
    Multitrack-FIFO kann dieselbe Writer-Pipeline später füttern, ohne dass
    sich am Writer etwas ändert (nur vorgesehen, nicht implementiert).

    Threading: enqueueJob() [Message Thread]. onJobFinished wird auf DIESEM
    Writer-Thread gerufen (vor dem ersten enqueueJob setzen!) — der
    CaptureService hebt den Report per AsyncUpdater auf den Message Thread.
    Job::releaseResources läuft IMMER (auch bei Abbruch/Teardown), ebenfalls
    auf dem Writer-Thread — dort hängen die Export-Pins (Halte-Protokoll).
*/
class CaptureWriter : public juce::Thread
{
public:
    //==========================================================================
    /** Schmale Quell-Schnittstelle — heute der Ring-Snapshot eines
        CaptureChannel, später auch ein Live-FIFO. Alle Funktionen laufen
        auf dem Writer-Thread. */
    struct TrackSource
    {
        /** false = Bereich nicht (mehr) lesbar (Overrun, Re-Anker, Freigabe). */
        std::function<bool (std::uint64_t position, float* dest, int numSamples)> read;

        /** Aktuelles Live-Ende für den Überholschutz (bei eingefrorenen
            Quellen konstant). Optional, wenn ringCapacitySamples == 0. */
        std::function<std::uint64_t()> getCurrentEnd;

        int ringCapacitySamples = 0;  // 0 = kein Überholschutz nötig
    };

    /** Ein Kanal eines Jobs — Snapshots sind zum Trigger-Zeitpunkt
        eingefroren, die Aufnahme läuft dahinter weiter. */
    struct Task
    {
        TrackSource source;
        std::uint64_t startPosition = 0;
        std::uint64_t endPosition   = 0;
        juce::String trackName;       // "in{N}" oder später der Strip-Name
        int channelIndex = -1;        // für Folgeaktionen (Release nach Export)
    };

    struct Job
    {
        std::vector<Task> tasks;
        double sampleRate = 48000.0;
        int bitDepth = 24;            // 16 / 24 / 32 (CaptureSettings)
        juce::File directory;
        int takeNumber = 1;
        std::function<void()> releaseResources;  // Pins lösen — läuft IMMER
    };

    struct TaskResult
    {
        juce::String trackName;
        juce::File file;              // bei Fehler bereits gelöscht
        int channelIndex = -1;
        bool success = false;
        juce::String error;
    };

    struct Report
    {
        std::vector<TaskResult> tasks;
        juce::File directory;
        std::uint64_t exportStart = 0;
        int numSucceeded = 0;
        int numFailed = 0;
    };

    //==========================================================================
    CaptureWriter();
    ~CaptureWriter() override;

    /** [Message Thread] Job einreihen — der Writer-Thread wacht auf. */
    void enqueueJob (Job job);

    /** [beliebiger Thread] true, solange Jobs anstehen oder geschrieben wird. */
    [[nodiscard]] bool isBusy() const noexcept
    {
        return pendingJobs.load (std::memory_order_acquire) > 0;
    }

    /** [Writer-Thread!] Vor dem ersten enqueueJob setzen (Klassendoku). */
    std::function<void (const Report&)> onJobFinished;

    //==========================================================================
    // Pure Helfer — testbar ohne Thread und ohne Datei-I/O

    [[nodiscard]] static std::uint64_t computeExportStart (const std::vector<Task>& tasks) noexcept;

    [[nodiscard]] static std::uint64_t computePadSamples (std::uint64_t taskStart,
                                                          std::uint64_t exportStart) noexcept;

    [[nodiscard]] static juce::String makeFileName (const juce::String& timestamp,
                                                    const juce::String& trackName,
                                                    int takeNumber);

    static constexpr int chunkSamples = 1 << 16;               // 64 k Samples pro Chunk
    static constexpr int headerFlushIntervalMs = 10'000;       // Crash kostet max. 10 s
    static constexpr int overrunMarginDivisor = 8;             // Marge = Kapazität/8
    static constexpr std::int64_t preallocBlockBytes = 16LL * 1024 * 1024;

private:
    void run() override;

    [[nodiscard]] std::unique_ptr<Job> popNextJob();
    [[nodiscard]] Report processJob (const Job& job);

    juce::CriticalSection queueLock;
    std::vector<std::unique_ptr<Job>> queue;   // FIFO, nur unter queueLock
    std::atomic<int> pendingJobs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureWriter)
};

} // namespace conduit
