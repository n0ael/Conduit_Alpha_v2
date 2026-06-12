#include "CaptureService.h"

#include <algorithm>
#include <limits>

namespace conduit
{

CaptureService::CaptureService (CaptureSettings& settingsToUse)
    : settings (settingsToUse)
{
    startTimer (guardIntervalMs);
}

CaptureService::~CaptureService()
{
    stopTimer();
    // ownedSets räumt alle Sätze ab — beim Teardown steht das Audio-Device
    // bereits (EngineProcessor-Lebenszyklus), kein Handoff mehr nötig
}

//==============================================================================
void CaptureService::prepare (double sampleRate, int samplesPerBlock, int numInputChannels)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize  = samplesPerBlock;
    preparedChannels   = juce::jlimit (0, MAX_CAPTURE_CHANNELS, numInputChannels);

    sampleClock.reset();  // Samplerate-Wechsel invalidiert alle Positionen
    inputMeter.prepare (sampleRate, numInputChannels);

    // Audio steht (prepareToPlay-Kontrakt) → Handoff synchron abwickeln:
    // ungelesene Mailbox leeren, Quittungen einsammeln, direkt installieren
    if (auto* unclaimed = pendingSet.exchange (nullptr, std::memory_order_acq_rel))
        destroySet (unclaimed);
    drainRetiredSets();
    audioSet = nullptr;

    auto* fresh = buildSet();

    // Alle älteren Sätze sind jetzt unreferenziert
    ownedSets.erase (std::remove_if (ownedSets.begin(), ownedSets.end(),
                                     [fresh] (const std::unique_ptr<BufferSet>& set)
                                     { return set.get() != fresh; }),
                     ownedSets.end());

    audioSet = fresh;
}

//==============================================================================
int CaptureService::computeRingCapacitySamples (int bufferMinutes, double sampleRate,
                                                int numChannels, int ramLimitGb) noexcept
{
    if (bufferMinutes <= 0 || sampleRate <= 0.0 || numChannels <= 0 || ramLimitGb <= 0)
        return 0;

    const auto wantedSamples = static_cast<juce::int64> (bufferMinutes) * 60
                             * static_cast<juce::int64> (sampleRate);

    const auto ramLimitBytes    = static_cast<juce::int64> (ramLimitGb) * (1024LL * 1024 * 1024);
    const auto samplesWithinRam = ramLimitBytes / (static_cast<juce::int64> (numChannels)
                                                   * static_cast<juce::int64> (sizeof (float)));

    return static_cast<int> (juce::jmin (wantedSamples, samplesWithinRam,
                                         static_cast<juce::int64> (std::numeric_limits<int>::max())));
}

int CaptureService::computePreRollCapacitySamples (int preRollSeconds, double sampleRate) noexcept
{
    if (preRollSeconds <= 0 || sampleRate <= 0.0)
        return 0;

    const auto samples = static_cast<juce::int64> (preRollSeconds)
                       * static_cast<juce::int64> (sampleRate);
    return static_cast<int> (juce::jmin (samples,
                                         static_cast<juce::int64> (std::numeric_limits<int>::max())));
}

int CaptureService::computeMaxSegments (int ramLimitGb, std::int64_t segmentBytes,
                                        std::int64_t preRollBytesTotal, int numChannels) noexcept
{
    if (ramLimitGb <= 0 || segmentBytes <= 0 || numChannels <= 0)
        return 0;

    // std::int64_t statt 1024LL: auf Linux ist int64_t "long", das Literal
    // "long long" — gemischte Typen finden keine jmin/jmax-Überladung
    const auto limitBytes = static_cast<std::int64_t> (ramLimitGb)
                          * (std::int64_t { 1024 } * 1024 * 1024);
    const auto available  = limitBytes - juce::jmax (std::int64_t { 0 }, preRollBytesTotal);
    if (available < segmentBytes)
        return 0;

    return static_cast<int> (juce::jmin (available / segmentBytes,
                                         static_cast<std::int64_t> (numChannels)));
}

//==============================================================================
CaptureService::BufferSet* CaptureService::buildSet()
{
    auto newSet = std::make_unique<BufferSet>();
    auto* fresh = newSet.get();

    const auto channels = preparedChannels;
    const auto ringCapacity = computeRingCapacitySamples (settings.getBufferMinutes(),
                                                          preparedSampleRate,
                                                          juce::jmax (1, channels),
                                                          settings.getRamLimitGb());
    const auto preRollCapacity = computePreRollCapacitySamples (settings.getPreRollSeconds(),
                                                                preparedSampleRate);

    if (channels > 0 && ringCapacity > 0)
    {
        fresh->numChannels     = channels;
        fresh->ringCapacity    = ringCapacity;
        fresh->preRollCapacity = preRollCapacity;

        const auto segmentBytes = static_cast<std::int64_t> (ringCapacity)
                                * static_cast<std::int64_t> (sizeof (float));
        const auto preRollBytes = static_cast<std::int64_t> (channels)
                                * static_cast<std::int64_t> (preRollCapacity)
                                * static_cast<std::int64_t> (sizeof (float));
        const auto maxSegments = computeMaxSegments (settings.getRamLimitGb(), segmentBytes,
                                                     preRollBytes, channels);

        // Ein Segment im Vorhalteziel: das erste Gate-Open wird im nächsten
        // Guard-Tick ohne frische Allokation bedient (HeapBlock allokiert
        // uninitialisiert — nur virtueller Speicher, bis geschrieben wird)
        fresh->pool.prepare (ringCapacity, maxSegments, juce::jmin (1, maxSegments));

        // Übernahme-Budget: 4× samplesPerBlock zusätzliche Kopier-Samples pro
        // Callback. Der Überschreibschutz der Übernahme braucht ≥ 2× (der
        // Pre-Roll verdrängt pro Block genau Blockgröße an ältesten Samples,
        // kopiert wird VOR dem Schreiben); 4× halbiert die Übernahmedauer
        // (60 s Pre-Roll in ≤ 15 s Echtzeit) bei weiterhin vernachlässigbaren
        // Kosten — vier zusätzliche memcpy-Blöcke gegen das DSP-Budget.
        const auto takeoverBudget = 4 * juce::jmax (1, preparedBlockSize);

        fresh->preRolls.reserve (static_cast<size_t> (channels));
        fresh->channels.reserve (static_cast<size_t> (channels));
        for (int ch = 0; ch < channels; ++ch)
        {
            auto preRoll = std::make_unique<PreRollBuffer>();
            preRoll->prepare (preRollCapacity);

            auto channel = std::make_unique<CaptureChannel>();
            channel->prepare (*preRoll, fresh->pool, takeoverBudget);

            fresh->preRolls.push_back (std::move (preRoll));
            fresh->channels.push_back (std::move (channel));
        }
    }

    ownedSets.push_back (std::move (newSet));
    currentSet = fresh;
    return fresh;
}

void CaptureService::destroySet (BufferSet* set)
{
    jassert (set != currentSet);
    ownedSets.erase (std::remove_if (ownedSets.begin(), ownedSets.end(),
                                     [set] (const std::unique_ptr<BufferSet>& owned)
                                     { return owned.get() == set; }),
                     ownedSets.end());
}

void CaptureService::drainRetiredSets()
{
    BufferSet* retired = nullptr;
    while (retiredSets.pop (retired))
        destroySet (retired);
}

//==============================================================================
void CaptureService::processInputTap (const juce::AudioBuffer<float>& buffer,
                                      int numInputChannels) noexcept
{
    // Puffersatz-Handoff: neuen Satz aus der Mailbox übernehmen, den alten
    // an den Message Thread zurückquittieren (Zerstörung dort)
    if (auto* incoming = pendingSet.exchange (nullptr, std::memory_order_acq_rel))
    {
        if (audioSet != nullptr)
            retiredSets.push (audioSet);
        audioSet = incoming;
    }

    // -- Metering: Peak / RMS / Noise-Floor pro Kanal -------------------------
    inputMeter.process (buffer, numInputChannels);

    const auto numSamples = buffer.getNumSamples();
    const auto blockStart = sampleClock.now();

    if (auto* set = audioSet)
    {
        // Resize-Policy: User hat den Verlust bestätigt — Gates schließen,
        // Material verwerfen, bewusst KEIN Auto-Export
        if (invalidateRequested.exchange (false, std::memory_order_acq_rel))
        {
            for (int ch = 0; ch < set->numChannels; ++ch)
            {
                set->channels[static_cast<size_t> (ch)]->releaseStorage();
                set->preRolls[static_cast<size_t> (ch)]->clear();
            }
        }

        const auto available = juce::jmin (juce::jmax (0, numInputChannels),
                                           buffer.getNumChannels(),
                                           set->numChannels);

        bool anyActive = false;
        for (int ch = 0; ch < set->numChannels; ++ch)
        {
            const auto index = static_cast<size_t> (ch);
            const float* src = ch < available ? buffer.getReadPointer (ch) : nullptr;

            // Reihenfolge entscheidend: erst die Kanal-Verarbeitung (die
            // Übernahme liest die ältesten Pre-Roll-Inhalte), DANN den
            // aktuellen Block in den Pre-Roll schreiben
            set->channels[index]->process (src, numSamples, blockStart);
            if (src != nullptr)
                set->preRolls[index]->write (src, numSamples, blockStart);

            anyActive = anyActive
                     || set->channels[index]->getState() != CaptureChannel::State::idle;
        }

        anyChannelActive.store (anyActive, std::memory_order_relaxed);
    }

    // -- [Capture-Baustein 4] Gate: Signal-über-Noise-Floor-Detektion ---------
    //    (nutzt inputMeter.getNoiseFloor() + Settings-Atomics und ruft
    //     openGate/closeGate — bis dahin Test-Seam)

    // -- [Capture-Baustein 5] Capture-Trigger / Export -------------------------

    // SampleClock zuletzt: erst wenn alle Bausteine die Samples dieses Blocks
    // verarbeitet haben, wird die neue Position publiziert (release) — Leser,
    // die bis now() konsumieren, sehen garantiert vollständige Daten.
    sampleClock.advance (numSamples);
}

//==============================================================================
void CaptureService::openGate (int channel) noexcept
{
    if (audioSet == nullptr || channel < 0 || channel >= audioSet->numChannels)
        return;

    // Pre-Roll-Fenster ≤ halbe Ring-Kapazität: garantiert, dass der Live-
    // Strom den Übernahme-Bereich nicht per Wrap verdrängt, bevor die
    // Übernahme fertig ist (Budget 4×Block → Dauer ≤ Fenster/4 Echtzeit)
    const auto window = juce::jmin (audioSet->preRollCapacity,
                                    audioSet->ringCapacity / 2);
    audioSet->channels[static_cast<size_t> (channel)]->openGate (sampleClock.now(), window);
}

void CaptureService::closeGate (int channel) noexcept
{
    if (audioSet == nullptr || channel < 0 || channel >= audioSet->numChannels)
        return;

    audioSet->channels[static_cast<size_t> (channel)]->closeGate (sampleClock.now());
}

//==============================================================================
bool CaptureService::isAnyChannelActive() const
{
    return anyChannelActive.load (std::memory_order_relaxed);
}

void CaptureService::invalidateAllBuffers()
{
    // Der Audio Thread quittiert im nächsten Block (Gates zu, Segmente an
    // den Pool zurück, Pre-Roll-Historie verworfen). Die Policy-Sicht ist
    // sofort inaktiv — ein laufender Block kann das Flag um einen Block
    // verzögert spiegeln, der nächste Tap korrigiert es.
    invalidateRequested.store (true, std::memory_order_release);
    anyChannelActive.store (false, std::memory_order_relaxed);
}

void CaptureService::reallocateBuffers()
{
    // [Message Thread] Erst nach prepare() sinnvoll — vorher fehlen
    // Samplerate und Kanalzahl
    if (preparedSampleRate <= 0.0)
        return;

    auto* fresh = buildSet();

    // Mailbox-Übergabe: ein dort noch liegender, nie abgeholter Satz wird
    // sofort zerstört — Audio hat ihn nie gesehen, kein Pile-up möglich.
    // Läuft kein Audio, holt das nächste prepare() den Satz aus der Mailbox.
    if (auto* unclaimed = pendingSet.exchange (fresh, std::memory_order_acq_rel))
        destroySet (unclaimed);

    drainRetiredSets();
}

//==============================================================================
void CaptureService::runRamGuard()
{
    drainRetiredSets();

    if (currentSet == nullptr)
        return;

    auto& pool = currentSet->pool;
    pool.service();

    const auto limitBytes = static_cast<std::int64_t> (settings.getRamLimitGb())
                          * (1024LL * 1024 * 1024);
    const auto overLimit = getCommittedBytes() > limitBytes;
    const auto starving  = pool.isExhausted();

    if (overLimit || starving)
    {
        // Schrittweise: ein Kanal pro Tick — die Freigabe läuft asynchron
        // (Audio quittiert, der nächste Tick sammelt das Segment ein und
        // bedient damit wartende Anfragen)
        CaptureChannel* oldestHeld = nullptr;
        for (auto& channel : currentSet->channels)
        {
            if (channel->getState() != CaptureChannel::State::held)
                continue;
            if (oldestHeld == nullptr
                || channel->getHeldSincePosition() < oldestHeld->getHeldSincePosition())
                oldestHeld = channel.get();
        }

        if (oldestHeld != nullptr)
            oldestHeld->requestRelease();
    }

    const auto warn = overLimit || starving;
    if (warn != ramWarningActive.exchange (warn, std::memory_order_relaxed))
        sendChangeMessage();  // UI-Warnung bei jedem Zustandswechsel
}

std::int64_t CaptureService::getCommittedBytes() const noexcept
{
    if (currentSet == nullptr)
        return 0;

    const auto preRollBytes = static_cast<std::int64_t> (currentSet->numChannels)
                            * static_cast<std::int64_t> (currentSet->preRollCapacity)
                            * static_cast<std::int64_t> (sizeof (float));
    return currentSet->pool.getAllocatedBytes() + preRollBytes;
}

//==============================================================================
int CaptureService::getRingCapacitySamples() const noexcept
{
    return currentSet != nullptr ? currentSet->ringCapacity : 0;
}

int CaptureService::getRingNumChannels() const noexcept
{
    return currentSet != nullptr ? currentSet->numChannels : 0;
}

const CaptureChannel* CaptureService::getChannel (int channel) const noexcept
{
    if (currentSet == nullptr || channel < 0 || channel >= currentSet->numChannels)
        return nullptr;

    return currentSet->channels[static_cast<size_t> (channel)].get();
}

} // namespace conduit
