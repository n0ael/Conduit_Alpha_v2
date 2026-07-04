#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "BrowserSearchIndex.h"   // Dispatcher-Seam (geteilt mit dem Index)

namespace conduit
{

//==============================================================================
/**
    Verzeichnis-Scanner der Browser-Dateibereiche (PROJEKTE + AUDIO, M6):
    scanAsync() listet ein Verzeichnis auf dem geteilten Worker-Pool und
    liest für Audio-Dateien NUR die Header-Metadaten (Dauer, Format) —
    die UI blockiert nie.

    mtime-Cache: unveränderte Dateien überspringen den AudioFormatReader
    beim Rescan (der Captures-Ordner kann viele BWF-Takes halten). Der
    Cache wird nur innerhalb der Pool-Jobs berührt (CriticalSection —
    der Pool könnte theoretisch mehr als einen Thread haben).

    Ergebnis-Marshalling wie beim BrowserSearchIndex: Dispatcher-Seam
    (Default MessageManager::callAsync), Generation-Zähler PRO scanId
    (der jüngste Scan eines Bereichs gewinnt), Alive-Flag gegen
    Destruktion während des Jobs. Nur Message Thread (außer Job-Innerem).
*/
class BrowserFileScanner final
{
public:
    struct Entry
    {
        juce::File file;
        juce::String name;              // Dateiname ohne Endung
        double durationSeconds = 0.0;   // 0 bei Nicht-Audio
        juce::String formatSummary;     // "48k / 24 Bit / st", leer bei Nicht-Audio
        juce::int64 modTimeMs = 0;
    };

    explicit BrowserFileScanner (juce::ThreadPool& workerToUse,
                                 BrowserSearchIndex::Dispatcher dispatcherToUse = {});
    ~BrowserFileScanner();

    /** [Message Thread] Scannt dir nach wildcard (z.B. "*.conduit" oder
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"); readAudioMetadata liest
        Header (Dauer/Format) mit mtime-Cache. */
    void scanAsync (const juce::String& scanId, const juce::File& directory,
                    const juce::String& wildcard, bool readAudioMetadata);

    /** [Message Thread] Ergebnis eines Scans (jüngster pro scanId). */
    std::function<void (const juce::String& scanId,
                        std::vector<Entry> entries)> onScanComplete;

    /** Testdiagnose: wie oft ein AudioFormatReader geöffnet wurde
        (mtime-Cache-Beleg). */
    [[nodiscard]] int getMetadataReadCount() const noexcept
    { return resources->metadataReads.load(); }

private:
    struct CacheSlot
    {
        juce::int64 modTimeMs = 0;
        Entry entry;
    };

    /** Alles, was der Pool-Job anfasst, lebt hinter EINEM shared_ptr —
        der Job darf `this` nie dereferenzieren, weil der Scanner vor dem
        ThreadPool sterben kann (TSan-/ASan-Fund CI 04.07.2026, gleiches
        Muster wie beim BrowserSearchIndex). */
    struct Resources
    {
        juce::AudioFormatManager formatManager;   // nach Ctor unverändert (Pool liest)
        juce::CriticalSection cacheLock;
        std::map<juce::String, CacheSlot> cache;  // fullPath → Header-Metadaten
        std::atomic<int> metadataReads { 0 };
    };

    juce::ThreadPool& worker;
    BrowserSearchIndex::Dispatcher dispatcher;

    std::shared_ptr<Resources> resources = std::make_shared<Resources>();

    std::map<juce::String, int> generations;  // scanId → jüngste Generation [MT]
    std::shared_ptr<std::atomic<bool>> alive
        = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserFileScanner)
};

} // namespace conduit
