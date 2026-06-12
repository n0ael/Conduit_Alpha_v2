#include "CaptureService.h"

#include <algorithm>
#include <limits>

namespace conduit
{

CaptureService::CaptureService (CaptureSettings& settingsToUse)
    : settings (settingsToUse)
{
    // Vor dem ersten enqueueJob setzen (CaptureWriter-Kontrakt): Reports
    // landen in der Mailbox und werden per AsyncUpdater auf den MT gehoben
    writer.onJobFinished = [this] (const CaptureWriter::Report& report)
    {
        {
            const juce::ScopedLock lock (reportLock);
            pendingReports.push_back (report);
        }
        triggerAsyncUpdate();
    };

    startTimer (guardIntervalMs);
}

CaptureService::~CaptureService()
{
    // Writer ZUERST stoppen: laufende Jobs brechen ab und lösen ihre Pins
    // (releaseResources läuft IMMER) — erst danach darf ownedSets die
    // Puffersätze gefahrlos zerstören
    writer.stopThread (10'000);
    cancelPendingUpdate();
    stopTimer();
    // ownedSets räumt alle Sätze ab — beim Teardown steht das Audio-Device
    // bereits (EngineProcessor-Lebenszyklus), kein Handoff mehr nötig
}

//==============================================================================
void CaptureService::prepare (double sampleRate, int samplesPerBlock, int numInputChannels)
{
    // Sicherheitsnetz Device-/Samplerate-Wechsel: prepare() invalidiert
    // ALLES (SampleClock-Reset + frischer Puffersatz) — aktives Material
    // (recording/held) wäre unwiederbringlich verloren. Deshalb VOR der
    // Invalidierung ein Auto-Export mit der ALTEN Samplerate (mit ihr wurde
    // aufgenommen, deshalb muss er vor dem Überschreiben der prepared-Werte
    // laufen); die Export-Pins halten den alten Satz am Leben, bis der
    // Writer fertig ist. Bewusst die EINZIGE Ausnahme von "Verwerfen ohne
    // Auto-Export" (Resize-Policy, CaptureSettings): den Resize bestätigt
    // der User explizit per Dialog, der Device-Wechsel kommt von außen —
    // ohne Gelegenheit zur Rückfrage. Ohne aktive Kanäle ist der Aufruf
    // ein No-op (enqueueExport prüft die Kanal-Zustände).
    if (preparedSampleRate > 0.0)
        enqueueExport (-1);

    preparedSampleRate = sampleRate;
    preparedBlockSize  = samplesPerBlock;
    preparedChannels   = juce::jlimit (0, MAX_CAPTURE_CHANNELS, numInputChannels);

    sampleClock.reset();  // Samplerate-Wechsel invalidiert alle Positionen

    // Meter großzügig über die maximal möglichen Tap-Indizes preparen:
    // eine spätere Slot-Erweiterung (reallocateBuffers) re-prepared das
    // Metering NICHT — ungeschriebene Kanäle liefern schlicht 0
    inputMeter.prepare (sampleRate,
                        preparedChannels + clampedVirtualSlots (preparedChannels,
                                                                MAX_VIRTUAL_CHANNELS));

    audioSampleRate = sampleRate;  // Audio steht — der Tap liest den Wert später

    const auto initialThresholdDb = settings.getThresholdDb();
    for (auto& gate : gates)
        gate.prepare (initialThresholdDb);

    // Audio steht (prepareToPlay-Kontrakt) → Handoff synchron abwickeln:
    // ungelesene Mailbox leeren, Quittungen einsammeln, direkt installieren
    if (auto* unclaimed = pendingSet.exchange (nullptr, std::memory_order_acq_rel))
        retireSet (unclaimed);
    audioSet = nullptr;

    auto* fresh = buildSet();

    // Alle älteren Sätze sind unreferenziert vom Audio — zerstört werden
    // sie aber erst, wenn kein Export-Job mehr aus ihnen liest (exportPins)
    for (auto& owned : ownedSets)
        if (owned.get() != fresh)
            retireSet (owned.get());
    drainRetiredSets();

    audioSet = fresh;

    // Device-Wechsel kann die Kanalzahl ändern — die UI (CapturePanel) baut
    // ihre Kanal-Zeilen auf diesen Broadcast hin neu auf (async, MT)
    sendChangeMessage();
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

std::int64_t CaptureService::computeHoldSamples (int holdMinutes, double sampleRate) noexcept
{
    if (holdMinutes <= 0 || sampleRate <= 0.0)
        return 0;

    return static_cast<std::int64_t> (holdMinutes) * 60
         * static_cast<std::int64_t> (sampleRate);
}

float CaptureService::computeEffectiveThresholdDb (float manualThresholdDb,
                                                   float noiseFloorLinear,
                                                   bool autoCalibrate) noexcept
{
    if (! autoCalibrate)
        return manualThresholdDb;

    // Schwelle über das Grundrauschen heben — der manuelle Threshold bleibt
    // als Untergrenze (Override) bestehen; Stille fällt auf ihn zurück
    const auto floorDb = juce::Decibels::gainToDecibels (noiseFloorLinear, silenceFloorDb);
    return juce::jmax (manualThresholdDb, floorDb + autoCalibrateHeadroomDb);
}

//==============================================================================
CaptureService::BufferSet* CaptureService::buildSet()
{
    auto newSet = std::make_unique<BufferSet>();
    auto* fresh = newSet.get();

    const auto channels = preparedChannels;

    // Virtuelle Slots nur für tatsächlich registrierte Taps bauen — ohne
    // Taps bleibt der Satz (und damit das RAM-Budget pro Kanal) identisch
    // zum reinen Hardware-Betrieb
    const auto virtualSlotCount = clampedVirtualSlots (channels, registeredVirtualSlotCount());
    const auto totalEntries = channels + virtualSlotCount;

    const auto ringCapacity = computeRingCapacitySamples (settings.getBufferMinutes(),
                                                          preparedSampleRate,
                                                          juce::jmax (1, totalEntries),
                                                          settings.getRamLimitGb());
    const auto preRollCapacity = computePreRollCapacitySamples (settings.getPreRollSeconds(),
                                                                preparedSampleRate);

    if (totalEntries > 0 && ringCapacity > 0)
    {
        fresh->numChannels     = channels;
        fresh->numVirtualSlots = virtualSlotCount;
        fresh->ringCapacity    = ringCapacity;
        fresh->preRollCapacity = preRollCapacity;

        const auto segmentBytes = static_cast<std::int64_t> (ringCapacity)
                                * static_cast<std::int64_t> (sizeof (float));
        const auto preRollBytes = static_cast<std::int64_t> (totalEntries)
                                * static_cast<std::int64_t> (preRollCapacity)
                                * static_cast<std::int64_t> (sizeof (float));
        const auto maxSegments = computeMaxSegments (settings.getRamLimitGb(), segmentBytes,
                                                     preRollBytes, totalEntries);

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

        fresh->preRolls.reserve (static_cast<size_t> (totalEntries));
        fresh->channels.reserve (static_cast<size_t> (totalEntries));
        for (int ch = 0; ch < totalEntries; ++ch)
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
    jassert (set->exportPins.load (std::memory_order_acquire) == 0);
    ownedSets.erase (std::remove_if (ownedSets.begin(), ownedSets.end(),
                                     [set] (const std::unique_ptr<BufferSet>& owned)
                                     { return owned.get() == set; }),
                     ownedSets.end());
}

void CaptureService::retireSet (BufferSet* set)
{
    if (std::find (retiredAwaitingDestroy.begin(), retiredAwaitingDestroy.end(), set)
        == retiredAwaitingDestroy.end())
        retiredAwaitingDestroy.push_back (set);
}

void CaptureService::drainRetiredSets()
{
    BufferSet* retired = nullptr;
    while (retiredSets.pop (retired))
        retireSet (retired);

    // Sweep: nur Sätze ohne aktive Export-Leser zerstören — der Rest wartet
    // auf den nächsten Tick (der Writer löst seine Pins in releaseResources)
    retiredAwaitingDestroy.erase (
        std::remove_if (retiredAwaitingDestroy.begin(), retiredAwaitingDestroy.end(),
                        [this] (BufferSet* set)
                        {
                            if (set->exportPins.load (std::memory_order_acquire) > 0)
                                return false;
                            destroySet (set);
                            return true;
                        }),
        retiredAwaitingDestroy.end());
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

        // Frischer Satz: alle Kanäle starten idle — der Detektionszustand
        // zieht mit, sonst lieferte ein offenes Gate nie wieder ein Open-Event
        for (auto& gate : gates)
            gate.reset();
    }

    // -- Metering: Peak / RMS / Noise-Floor pro Kanal -------------------------
    inputMeter.process (buffer, numInputChannels);

    const auto numSamples = buffer.getNumSamples();
    const auto blockStart = sampleClock.now();

    if (auto* set = audioSet)
    {
        // Resize-Policy: User hat den Verlust bestätigt — Gates schließen,
        // Material verwerfen, bewusst KEIN Auto-Export. Liegt weiterhin
        // Signal über der Schwelle an, öffnet die Detektion unten sofort
        // wieder — neues Material landet dann in frischen Puffern
        if (invalidateRequested.exchange (false, std::memory_order_acq_rel))
        {
            for (int ch = 0; ch < set->totalEntries(); ++ch)
            {
                set->channels[static_cast<size_t> (ch)]->releaseStorage();
                set->preRolls[static_cast<size_t> (ch)]->clear();
                gates[static_cast<size_t> (ch)].reset();
            }
        }

        const auto available = juce::jmin (juce::jmax (0, numInputChannels),
                                           buffer.getNumChannels(),
                                           set->numChannels);

        const auto holdSamples = computeHoldSamples (settings.getHoldMinutes(),
                                                     audioSampleRate);

        bool anyActive = false;
        for (int ch = 0; ch < set->numChannels; ++ch)
        {
            const auto index = static_cast<size_t> (ch);
            const float* src = ch < available ? buffer.getReadPointer (ch) : nullptr;

            // -- Gate [Baustein 4]: Block-RMS gegen die effektive Schwelle.
            //    Das Meter hat diesen Block bereits gemessen — Open wirkt ab
            //    dem Block, der die Schwelle reißt; den Attack davor liefert
            //    die Pre-Roll-Übernahme
            switch (gates[index].process (inputMeter.getRms (ch), numSamples, holdSamples))
            {
                case CaptureGate::Event::opened: openGate (ch);  break;
                case CaptureGate::Event::closed: closeGate (ch); break;
                case CaptureGate::Event::none:                   break;
            }

            // Reihenfolge entscheidend: erst die Kanal-Verarbeitung (die
            // Übernahme liest die ältesten Pre-Roll-Inhalte), DANN den
            // aktuellen Block in den Pre-Roll schreiben
            set->channels[index]->process (src, numSamples, blockStart);
            if (src != nullptr)
                set->preRolls[index]->write (src, numSamples, blockStart);

            // RAM-Wächter oder Invalidate haben gehaltenes Material gelöst —
            // der publizierte Gate-Status folgt dem Kanal zurück auf idle
            const auto channelState = set->channels[index]->getState();
            if (channelState == CaptureChannel::State::idle)
                gates[index].notifyContentDiscarded();

            anyActive = anyActive || channelState != CaptureChannel::State::idle;
        }

        // -- Virtuelle Slots (Capture-Taps): Pflege der Zustandsmaschinen ------
        // Aktive Slots treibt das Modul selbst über writeVirtualChannel()
        // (läuft im Graph SPÄTER in diesem Callback); hier laufen nur
        // Deregistrierungs-Quittung und verwaiste Slots — sonst stürben
        // Release-Quittungen und Übernahme-Restkopien mit dem Modul.
        for (int s = 0; s < set->numVirtualSlots; ++s)
        {
            const auto index = set->numChannels + s;
            const auto idx = static_cast<size_t> (index);
            auto& slot = virtualSlots[static_cast<size_t> (s)];

            // Phase-1-Quittung (5.3): Kanal sofort schließen — Material →
            // held. Detektions-Gate UND Kanal unabhängig voneinander, denn
            // das Channel-Gate kann auch über die Test-Seam offen sein.
            if (slot.detachRequested.exchange (false, std::memory_order_acq_rel))
            {
                gates[idx].close();  // falls die Detektion offen war
                closeGate (index);   // recording → held; awaiting → idle
            }

            if (! slot.writerActive.load (std::memory_order_acquire))
                set->channels[idx]->process (nullptr, numSamples, blockStart);

            const auto channelState = set->channels[idx]->getState();
            if (channelState == CaptureChannel::State::idle)
                gates[idx].notifyContentDiscarded();

            anyActive = anyActive || channelState != CaptureChannel::State::idle;
        }

        anyChannelActive.store (anyActive, std::memory_order_relaxed);
    }

    // Export (Baustein 5) braucht hier nichts: exportAll() friert Snapshots
    // auf dem Message Thread ein, der CaptureWriter liest hinter dem
    // Schreib-Cursor — der Tap schreibt einfach weiter.

    // SampleClock zuletzt: erst wenn alle Bausteine die Samples dieses Blocks
    // verarbeitet haben, wird die neue Position publiziert (release) — Leser,
    // die bis now() konsumieren, sehen garantiert vollständige Daten.
    sampleClock.advance (numSamples);
}

//==============================================================================
void CaptureService::openGate (int channel) noexcept
{
    if (audioSet == nullptr || channel < 0 || channel >= audioSet->totalEntries())
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
    if (audioSet == nullptr || channel < 0 || channel >= audioSet->totalEntries())
        return;

    audioSet->channels[static_cast<size_t> (channel)]->closeGate (sampleClock.now());
}

//==============================================================================
int CaptureService::clampedVirtualSlots (int hardwareChannels, int wanted) noexcept
{
    return juce::jlimit (0, juce::jmax (0, MAX_CAPTURE_CHANNELS - hardwareChannels),
                         juce::jmin (wanted, MAX_VIRTUAL_CHANNELS));
}

int CaptureService::registeredVirtualSlotCount() const noexcept
{
    for (int s = MAX_VIRTUAL_CHANNELS; --s >= 0;)
        if (virtualSlots[static_cast<size_t> (s)].occupied)
            return s + 1;

    return 0;
}

bool CaptureService::needsVirtualExpansion() const noexcept
{
    if (preparedSampleRate <= 0.0 || currentSet == nullptr)
        return false;

    return clampedVirtualSlots (preparedChannels, registeredVirtualSlotCount())
           > currentSet->numVirtualSlots;
}

CaptureService::VirtualChannelHandle CaptureService::registerVirtualChannel (const juce::String& name)
{
    for (int s = 0; s < MAX_VIRTUAL_CHANNELS; ++s)
    {
        auto& slot = virtualSlots[static_cast<size_t> (s)];
        if (slot.occupied)
            continue;

        // Slot mit noch gebundenem Material (held nach Deregistrierung)
        // nicht wiedervergeben — der Puffer gehört bis Export/Reclaim dem
        // alten Tap
        if (currentSet != nullptr && s < currentSet->numVirtualSlots)
        {
            const auto idx = static_cast<size_t> (currentSet->numChannels + s);
            if (currentSet->channels[idx]->getState() != CaptureChannel::State::idle)
                continue;
        }

        slot.occupied = true;
        slot.name = name;
        slot.detachRequested.store (false, std::memory_order_relaxed);
        slot.writerActive.store (true, std::memory_order_release);

        // Satz-Erweiterung: verlustfrei nur bei inaktiven Kanälen — sonst
        // holt der Guard-Tick sie nach, sobald nichts mehr aufnimmt/hält
        if (needsVirtualExpansion() && ! isAnyChannelActive())
            reallocateBuffers();

        sendChangeMessage();  // CapturePanel baut die Tap-Zeilen neu
        return { s };
    }

    return {};
}

void CaptureService::unregisterVirtualChannel (VirtualChannelHandle& handle)
{
    if (! juce::isPositiveAndBelow (handle.slot, MAX_VIRTUAL_CHANNELS))
    {
        handle = {};
        return;
    }

    auto& slot = virtualSlots[static_cast<size_t> (handle.slot)];
    handle = {};

    if (! slot.occupied)
        return;

    // Phase-1-Pattern (5.3): Schreibpfad SOFORT trennen; die Gate-Schließung
    // (Material → held) quittiert der Audio Thread im nächsten Block.
    // slot.name bleibt stehen — der Export gehaltenen Materials braucht ihn.
    slot.writerActive.store (false, std::memory_order_release);
    slot.detachRequested.store (true, std::memory_order_release);
    slot.occupied = false;

    sendChangeMessage();
}

void CaptureService::setVirtualChannelName (VirtualChannelHandle handle, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (handle.slot, MAX_VIRTUAL_CHANNELS))
        return;

    auto& slot = virtualSlots[static_cast<size_t> (handle.slot)];
    if (! slot.occupied || slot.name == name)
        return;

    slot.name = name;
    sendChangeMessage();
}

void CaptureService::writeVirtualChannel (VirtualChannelHandle handle,
                                          const float* data, int numSamples) noexcept
{
    auto* set = audioSet;

    if (set == nullptr || data == nullptr || numSamples <= 0
        || ! juce::isPositiveAndBelow (handle.slot, set->numVirtualSlots))
        return;

    auto& slot = virtualSlots[static_cast<size_t> (handle.slot)];
    if (! slot.writerActive.load (std::memory_order_acquire))
        return;  // Phase 1 lief — höchstens dieser eine Block geht noch verloren

    // Die SampleClock hat am Tap-Ende bereits weitergetickt — der Graph-Block
    // gehört zum selben Callback, sein Start liegt also numSamples zurück.
    const auto clockNow = sampleClock.now();
    if (clockNow < static_cast<std::uint64_t> (numSamples))
        return;  // defensiv: noch kein Input-Tap gelaufen

    const auto blockStart = clockNow - static_cast<std::uint64_t> (numSamples);
    const auto index = set->numChannels + handle.slot;
    const auto idx = static_cast<size_t> (index);

    // Identischer Ablauf wie der Hardware-Pfad im Tap: Meter → Gate →
    // Kanal-Verarbeitung → Pre-Roll-Write (Reihenfolge entscheidend)
    inputMeter.processChannel (index, data, numSamples);

    const auto holdSamples = computeHoldSamples (settings.getHoldMinutes(), audioSampleRate);
    switch (gates[idx].process (inputMeter.getRms (index), numSamples, holdSamples))
    {
        case CaptureGate::Event::opened: openGate (index);  break;
        case CaptureGate::Event::closed: closeGate (index); break;
        case CaptureGate::Event::none:                      break;
    }

    set->channels[idx]->process (data, numSamples, blockStart);
    set->preRolls[idx]->write (data, numSamples, blockStart);

    if (set->channels[idx]->getState() == CaptureChannel::State::idle)
        gates[idx].notifyContentDiscarded();
}

CaptureService::VirtualChannelUiInfo CaptureService::getVirtualChannelUiInfo (int slot) const
{
    VirtualChannelUiInfo info;

    if (! juce::isPositiveAndBelow (slot, MAX_VIRTUAL_CHANNELS))
        return info;

    const auto& virtualSlot = virtualSlots[static_cast<size_t> (slot)];
    info.name  = virtualSlot.name;
    info.inUse = virtualSlot.occupied;

    if (currentSet != nullptr && slot < currentSet->numVirtualSlots)
    {
        info.captureIndex = currentSet->numChannels + slot;

        // Deregistrierter Slot mit gehaltenem Material bleibt sichtbar
        if (! info.inUse)
            info.inUse = currentSet->channels[static_cast<size_t> (info.captureIndex)]
                             ->getState() != CaptureChannel::State::idle;
    }

    return info;
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
    // ausgemustert — Audio hat ihn nie gesehen, kein Pile-up möglich; der
    // Sweep zerstört ihn, sobald keine Export-Pins mehr darauf liegen.
    // Läuft kein Audio, holt das nächste prepare() den Satz aus der Mailbox.
    if (auto* unclaimed = pendingSet.exchange (fresh, std::memory_order_acq_rel))
        retireSet (unclaimed);

    drainRetiredSets();
}

//==============================================================================
int CaptureService::exportAll()
{
    return enqueueExport (-1);
}

int CaptureService::exportChannel (int channelIndex)
{
    return channelIndex >= 0 ? enqueueExport (channelIndex) : 0;
}

int CaptureService::enqueueExport (int onlyChannel)
{
    if (currentSet == nullptr || preparedSampleRate <= 0.0)
        return 0;

    auto* set = currentSet;

    CaptureWriter::Job job;
    std::vector<CaptureChannel*> pinnedChannels;

    for (int ch = 0; ch < set->totalEntries(); ++ch)
    {
        if (onlyChannel >= 0 && ch != onlyChannel)
            continue;

        auto* channel = set->channels[static_cast<size_t> (ch)].get();

        // Leser anmelden, DANN Zustand prüfen (Dekker-Protokoll): schlägt
        // tryBeginExportRead fehl, läuft gerade eine Freigabe — überspringen
        if (! channel->tryBeginExportRead())
            continue;

        const auto state = channel->getState();
        const auto range = channel->getReadableRange();

        if ((state != CaptureChannel::State::recording
             && state != CaptureChannel::State::held)
            || range.to <= range.from)
        {
            channel->endExportRead();
            continue;
        }

        CaptureWriter::Task task;
        task.channelIndex  = ch;
        task.startPosition = range.from;
        task.endPosition   = range.to;

        // Hardware: "inN" (Strip-Namen mit dem Mixer); virtuelle Kanäle
        // tragen den registrierten Namen (== moduleId des Tap-Moduls)
        if (ch < set->numChannels)
            task.trackName = "in" + juce::String (ch + 1);
        else
        {
            const auto& slotName = virtualSlots[static_cast<size_t> (ch - set->numChannels)].name;
            task.trackName = slotName.isNotEmpty()
                           ? slotName
                           : "tap" + juce::String (ch - set->numChannels + 1);
        }

        // Überholschutz-Vorsorge: Läuft die Aufnahme noch (liveEnd wandert),
        // startet der Leser bei VOLLEM Ring exakt Kapazität hinter dem
        // Schreib-Cursor — der erste Margin-Check des Writers würde sofort
        // abbrechen. Deshalb den Snapshot auf Kapazität − 2×Marge kürzen:
        // 1×Marge bleibt als Writer-Abbruchgrenze, 1×Marge als echter
        // Vorsprung für die weiterlaufende Aufnahme (bei 15 min ≈ 2 min
        // Echtzeit, der Writer streamt in Sekunden). held-Kanäle behalten
        // den vollen Bereich — ihr Ende steht, nichts überholt den Leser.
        if (state == CaptureChannel::State::recording && set->ringCapacity > 0)
        {
            const auto capacity = static_cast<std::uint64_t> (set->ringCapacity);
            const auto margin   = capacity
                                / static_cast<std::uint64_t> (CaptureWriter::overrunMarginDivisor);
            const auto maxLength = capacity > 2 * margin ? capacity - 2 * margin : capacity;

            if (range.to - range.from > maxLength)
                task.startPosition = range.to - maxLength;
        }
        task.source.read = [channel] (std::uint64_t position, float* dest, int numSamples)
        { return channel->read (position, dest, numSamples); };
        task.source.getCurrentEnd = [channel] { return channel->getEndPosition(); };
        task.source.ringCapacitySamples = set->ringCapacity;

        job.tasks.push_back (std::move (task));
        pinnedChannels.push_back (channel);
    }

    if (job.tasks.empty())
        return 0;

    set->exportPins.fetch_add (1, std::memory_order_acq_rel);

    job.sampleRate = preparedSampleRate;
    job.bitDepth   = settings.getExportBitDepth();
    job.directory  = settings.getExportDirectory();
    job.takeNumber = nextTakeNumber++;

    // Läuft IMMER auf dem Writer-Thread: erst die Kanal-Leser lösen, dann
    // den Satz-Pin — danach darf der Sweep den Satz zerstören
    job.releaseResources = [set, channels = std::move (pinnedChannels)]
    {
        for (auto* channel : channels)
            channel->endExportRead();
        set->exportPins.fetch_sub (1, std::memory_order_acq_rel);
    };

    const auto numTracks = static_cast<int> (job.tasks.size());
    writer.enqueueJob (std::move (job));
    return numTracks;
}

void CaptureService::releaseExportedHeldChannels (const std::vector<int>& channelIndices)
{
    if (currentSet == nullptr)
        return;

    for (const auto ch : channelIndices)
    {
        if (ch < 0 || ch >= currentSet->totalEntries())
            continue;

        auto& channel = *currentSet->channels[static_cast<size_t> (ch)];
        if (channel.getState() == CaptureChannel::State::held)
            channel.requestRelease();
    }
}

CaptureService::UiStatus CaptureService::getUiStatus() const
{
    UiStatus status;
    status.exporting = writer.isBusy();

    if (currentSet == nullptr)
        return status;

    for (int ch = 0; ch < currentSet->totalEntries(); ++ch)
    {
        const auto& channel = *currentSet->channels[static_cast<size_t> (ch)];
        const auto state = channel.getState();

        if (state == CaptureChannel::State::recording
            || state == CaptureChannel::State::awaitingSegment)
            status.anyRecording = true;
        else if (state == CaptureChannel::State::held)
            status.anyHeld = true;

        if (state != CaptureChannel::State::idle && currentSet->ringCapacity > 0)
        {
            const auto range = channel.getReadableRange();
            const auto fill = static_cast<float> (range.to - range.from)
                            / static_cast<float> (currentSet->ringCapacity);
            status.fillNorm = juce::jmax (status.fillNorm, juce::jmin (1.0f, fill));
        }
    }

    return status;
}

void CaptureService::handleAsyncUpdate()
{
    std::vector<CaptureWriter::Report> reports;
    {
        const juce::ScopedLock lock (reportLock);
        reports.swap (pendingReports);
    }

    for (const auto& report : reports)
        if (onExportFinished)
            onExportFinished (report);
}

//==============================================================================
void CaptureService::timerCallback()
{
    runRamGuard();

    if (++guardTicksSinceCalibration >= guardTicksPerCalibration)
    {
        guardTicksSinceCalibration = 0;
        runAutoCalibration();
    }
}

void CaptureService::runAutoCalibration()
{
    const auto manualDb = settings.getThresholdDb();
    const auto autoCal  = settings.getAutoCalibrate();

    // Hardware + virtuelle Slots — ungeschriebene Taps haben Floor 0 und
    // fallen damit auf den manuellen Threshold zurück
    const auto totalChannels = preparedChannels
                             + (currentSet != nullptr ? currentSet->numVirtualSlots : 0);

    for (int ch = 0; ch < totalChannels; ++ch)
        gates[static_cast<size_t> (ch)].setEffectiveThresholdDb (
            computeEffectiveThresholdDb (manualDb, inputMeter.getNoiseFloor (ch), autoCal));
}

//==============================================================================
void CaptureService::runRamGuard()
{
    drainRetiredSets();

    if (currentSet == nullptr)
        return;

    // Aufgeschobene Tap-Slot-Erweiterung nachholen, sobald kein Kanal mehr
    // aktiv ist — laufende Aufnahmen werden NIE für einen Tap verworfen
    if (needsVirtualExpansion() && ! isAnyChannelActive())
        reallocateBuffers();

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

    const auto preRollBytes = static_cast<std::int64_t> (currentSet->totalEntries())
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
    if (currentSet == nullptr || channel < 0 || channel >= currentSet->totalEntries())
        return nullptr;

    return currentSet->channels[static_cast<size_t> (channel)].get();
}

const CaptureGate* CaptureService::getGate (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, MAX_CAPTURE_CHANNELS)
         ? &gates[static_cast<size_t> (channel)]
         : nullptr;
}

} // namespace conduit
