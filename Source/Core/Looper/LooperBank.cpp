#include "LooperBank.h"

#include <cmath>

namespace conduit
{

LooperBank::LooperBank()
{
    // Vektor-Kapazität vorab (MT-Pfad — trotzdem keine Realloc-Kaskaden
    // beim Live-Hämmern von Commits)
    store.reserve (256);
    graveyard.reserve (256);

    for (auto& looper : targetGain)
        for (auto& gain : looper)
            gain.store (1.0f, std::memory_order_relaxed);
    for (auto& looper : targetPan)
        for (auto& pan : looper)
            pan.store (0.0f, std::memory_order_relaxed);
    for (auto& looper : effectiveMute)
        for (auto& mute : looper)
            mute.store (false, std::memory_order_relaxed);
    for (auto& toMaster : looperToMaster)
        toMaster.store (true, std::memory_order_relaxed);
}

//==============================================================================
void LooperBank::prepare (double sampleRate, int maxBlockSamples)
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    crossfadeSamples = juce::jmax (1, (int) std::lround (crossfadeSeconds * preparedSampleRate));
    maxLoopSamples   = (int) std::lround (maxLoopSeconds * preparedSampleRate);

    const auto scratchSize = static_cast<std::size_t> (juce::jmax (16, maxBlockSamples));
    scratchLeft.assign (scratchSize, 0.0f);
    scratchRight.assign (scratchSize, 0.0f);

    // Looper-I/O-Busse (Looper-Out-Modul + Master-Mix) — Audio steht
    for (auto& looper : preBus)
        for (auto& channel : looper)
            channel.assign (scratchSize, 0.0f);
    for (auto& looper : postBus)
        for (auto& channel : looper)
            channel.assign (scratchSize, 0.0f);
    for (auto& channel : masterBus)
        channel.assign (scratchSize, 0.0f);
    renderedSamples = 0;

    // Audio steht (prepareToPlay) — die SPSC-Rollen dürfen hier einmalig
    // vom MT übernommen werden: Queues leeren, Voices direkt zurücksetzen
    ClipCommand dropped;
    while (commands.pop (dropped)) {}

    LooperClip* released = nullptr;
    while (retired.pop (released))
        for (auto& grave : graveyard)
            if (grave.clip.get() == released)
                grave.audioReleased = true;

    for (auto& looper : players)
        for (auto& track : looper)
        {
            for (auto& voice : track.voices)
                voice = Voice {};
            track.pending = PendingAction {};
            track.currentGain = 1.0f;
            track.currentPan = 0.0f;
            track.currentMuteGain = 1.0f;
        }

    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t t = 0; t < static_cast<std::size_t> (maxTracks); ++t)
            trackMeters[l][t].prepare (preparedSampleRate, 2);

    // Alle Clips verwerfen (SampleRate-Wechsel invalidiert Inhalte);
    // export-gepinnte bleiben im Graveyard, bis der Writer fertig ist
    for (auto& clip : store)
        if (clip->exportPins.load (std::memory_order_acquire) > 0)
            graveyard.push_back ({ std::move (clip), true });
    store.clear();

    std::erase_if (graveyard, [] (const Grave& grave)
    {
        return grave.clip == nullptr
            || grave.clip->exportPins.load (std::memory_order_acquire) <= 0;
    });
    for (auto& grave : graveyard)
        grave.audioReleased = true;

    std::int64_t remaining = 0;
    for (const auto& grave : graveyard)
        remaining += grave.clip->allocatedBytes();
    ramBytesUsed.store (remaining, std::memory_order_relaxed);

    for (auto& looper : mtActiveClip)
        looper.fill (nullptr);
    for (auto& looper : mtTrackPlaying)
        looper.fill (false);

    playingFlag.store (false, std::memory_order_relaxed);
    committedBars.store (0, std::memory_order_relaxed);
    stopAllRequested.store (false, std::memory_order_relaxed);
    snapCount.store (0, std::memory_order_relaxed);

    playheadValid = false;
    snapPendingCount = 0;
    snapDucking = false;
    duckGain = 1.0f;
    blockCounter = 0;
}

//==============================================================================
juce::Result LooperBank::commitAndPlay (int looperIndex, int trackIndex, int bars,
                                        const CaptureService& capture,
                                        int leftIndex, int rightIndex,
                                        const BarSampleAnchors& anchors)
{
    // Paritäts-Semantik (M2/altes UI): der neue Commit ERSETZT den
    // bisherigen Track-Clip — im Slot-Modell übernimmt das Modell die
    // Overwrite-Entscheidung und ruft commitClip direkt
    auto* old = activeClipFor (looperIndex, trackIndex);

    const auto result = commitClip (looperIndex, trackIndex, bars, capture,
                                    leftIndex, rightIndex, anchors, nullptr);
    if (result.failed())
        return result;

    if (old != nullptr)
    {
        const auto deleted = deleteClip (old);
        jassert (deleted.wasOk());   // getNumFree ≥ 2 wurde im Commit geprüft
        juce::ignoreUnused (deleted);
    }

    return result;
}

juce::Result LooperBank::commitClip (int looperIndex, int trackIndex, int bars,
                                     const CaptureService& capture,
                                     int leftIndex, int rightIndex,
                                     const BarSampleAnchors& anchors,
                                     LooperClip** outClip)
{
    // Erst Quittungen einsammeln — gibt RAM frei, bevor das Budget prüft
    serviceMessageThread();

    if (preparedSampleRate <= 0.0)
        return juce::Result::fail ("Looper nicht vorbereitet");

    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return juce::Result::fail ("Ungültiger Looper/Track");

    if (leftIndex < 0)
        return juce::Result::fail ("Keine Looper-Quelle gewählt");

    const auto range = looper::commitRangeForBars (anchors.latestBoundaryBar(), bars);
    if (! range.valid)
        return juce::Result::fail ("Noch keine " + juce::String (bars)
                                   + " kompletten Takte in der Session");

    std::uint64_t startSample = 0, endSample = 0;
    if (! anchors.lookup (range.startBar, startSample)
        || ! anchors.lookup (range.endBar, endSample)
        || endSample <= startSample)
        return juce::Result::fail ("Taktgrenzen nicht mehr adressierbar");

    const auto numLoopSamples = endSample - startSample;
    if (numLoopSamples > static_cast<std::uint64_t> (maxLoopSamples))
        return juce::Result::fail ("Loop länger als "
                                   + juce::String ((int) maxLoopSeconds) + " s");

    // Mono-Quelle (rightIndex < 0) → 1-Kanal-Clip: halber RAM, und der
    // Export schreibt eine echte Mono-Datei (Looper-I/O 07/2026)
    const auto clipChannels = rightIndex >= 0 ? 2 : 1;

    const auto totalSamples = static_cast<int> (numLoopSamples) + crossfadeSamples;
    const auto newBytes = static_cast<std::int64_t> (totalSamples) * clipChannels
                            * static_cast<std::int64_t> (sizeof (float))
                        + static_cast<std::int64_t> (sizeof (LooperClip));
    if (ramBytesUsed.load (std::memory_order_relaxed) + newBytes > ramBudgetBytes)
        return juce::Result::fail ("Looper-RAM-Budget erschöpft ("
                                   + juce::String (ramBudgetBytes / 1'000'000)
                                   + " MB) — Clips löschen");

    // Headroom für activate + ggf. das Replace-Delete des Parity-Wrappers
    if (commands.getNumFree() < 2)
        return juce::Result::fail ("Looper-Kommando-Queue voll");

    auto clip = std::make_unique<LooperClip>();
    clip->buffer.setSize (clipChannels, totalSamples);
    clip->buffer.clear();

    // Lead-in beginnt crossfadeSamples VOR dem Loop-Start; am Session-
    // Anfang fehlt er ggf. → der unlesbare Teil bleibt Stille
    const auto leadIn = static_cast<std::uint64_t> (crossfadeSamples);
    const auto readStart = startSample >= leadIn ? startSample - leadIn : 0;
    const auto padMissing = static_cast<int> (leadIn - (startSample - readStart));

    const int indices[] = { leftIndex, rightIndex };
    for (int channel = 0; channel < clipChannels; ++channel)
        readChannelChunked (capture.getChannel (indices[channel]), readStart,
                            clip->buffer.getWritePointer (channel) + padMissing,
                            totalSamples - padMissing);

    clip->numContentSamples = static_cast<int> (numLoopSamples);
    clip->crossfadeSamples  = crossfadeSamples;
    clip->contentBeats = range.lengthBeats();
    clip->samplesPerBeatRecorded = static_cast<double> (numLoopSamples) / range.lengthBeats();
    clip->commitStartSample = startSample;
    clip->commitEndBeat = range.endBeat();
    clip->clipId = ++nextClipId;
    clip->commitBars = bars;

    // Staged UND Active identisch initialisieren (Audio kennt den Clip
    // noch nicht — die einzige legale MT-Schreibstelle der Active-Felder)
    clip->stagedRate.store (1.0, std::memory_order_relaxed);
    clip->stagedLengthBeats.store (range.lengthBeats(), std::memory_order_relaxed);
    clip->stagedWindowOffsetBeats.store (0.0, std::memory_order_relaxed);
    clip->stagedReversed.store (false, std::memory_order_relaxed);
    clip->activeAnchorBeat = range.endBeat();
    clip->activeRate = 1.0;
    clip->activeLengthBeats = range.lengthBeats();
    clip->activeWindowOffsetBeats = 0.0;
    clip->activeReversed = false;

    auto* raw = clip.get();
    const auto l = static_cast<std::size_t> (looperIndex);
    const auto t = static_cast<std::size_t> (trackIndex);

    store.push_back (std::move (clip));
    mtActiveClip[l][t] = raw;
    ramBytesUsed.fetch_add (newBytes, std::memory_order_relaxed);

    commands.push ({ ClipCommand::Type::activate, looperIndex, trackIndex, raw, 0.0 });

    committedBars.store (bars, std::memory_order_relaxed);
    mtTrackPlaying[l][t] = true;
    refreshPlayingFlag();

    if (outClip != nullptr)
        *outClip = raw;
    return juce::Result::ok();
}

juce::Result LooperBank::deleteClip (LooperClip* clip)
{
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip");

    if (commands.getNumFree() < 1)
        return juce::Result::fail ("Looper-Kommando-Queue voll");

    moveToGraveyard (clip);
    commands.push ({ ClipCommand::Type::deleteClip, 0, 0, clip, 0.0 });
    return juce::Result::ok();
}

juce::Result LooperBank::startClip (int looperIndex, int trackIndex,
                                    LooperClip* clip, double qBeats)
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks || clip == nullptr)
        return juce::Result::fail ("Ungültiger Looper/Track/Clip");

    if (! commands.push ({ ClipCommand::Type::start, looperIndex, trackIndex, clip, qBeats }))
        return juce::Result::fail ("Looper-Kommando-Queue voll");

    mtActiveClip[static_cast<std::size_t> (looperIndex)]
                [static_cast<std::size_t> (trackIndex)] = clip;
    mtTrackPlaying[static_cast<std::size_t> (looperIndex)]
                  [static_cast<std::size_t> (trackIndex)] = true;
    refreshPlayingFlag();
    return juce::Result::ok();
}

juce::Result LooperBank::retriggerClip (int looperIndex, int trackIndex,
                                        LooperClip* clip, double qBeats)
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks || clip == nullptr)
        return juce::Result::fail ("Ungültiger Looper/Track/Clip");

    if (! commands.push ({ ClipCommand::Type::retrigger, looperIndex, trackIndex, clip, qBeats }))
        return juce::Result::fail ("Looper-Kommando-Queue voll");

    mtActiveClip[static_cast<std::size_t> (looperIndex)]
                [static_cast<std::size_t> (trackIndex)] = clip;
    mtTrackPlaying[static_cast<std::size_t> (looperIndex)]
                  [static_cast<std::size_t> (trackIndex)] = true;
    refreshPlayingFlag();
    return juce::Result::ok();
}

//==============================================================================
LooperClip* LooperBank::activeClipFor (int looperIndex, int trackIndex) const noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return nullptr;

    return mtActiveClip[static_cast<std::size_t> (looperIndex)]
                       [static_cast<std::size_t> (trackIndex)];
}

juce::Result LooperBank::startTrack (int looperIndex, int trackIndex, double qBeats)
{
    auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip auf diesem Track");

    if (! commands.push ({ ClipCommand::Type::start, looperIndex, trackIndex, clip, qBeats }))
        return juce::Result::fail ("Looper-Kommando-Queue voll");

    mtTrackPlaying[static_cast<std::size_t> (looperIndex)]
                  [static_cast<std::size_t> (trackIndex)] = true;
    refreshPlayingFlag();
    return juce::Result::ok();
}

juce::Result LooperBank::retriggerTrack (int looperIndex, int trackIndex, double qBeats)
{
    auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip auf diesem Track");

    if (! commands.push ({ ClipCommand::Type::retrigger, looperIndex, trackIndex, clip, qBeats }))
        return juce::Result::fail ("Looper-Kommando-Queue voll");

    mtTrackPlaying[static_cast<std::size_t> (looperIndex)]
                  [static_cast<std::size_t> (trackIndex)] = true;
    refreshPlayingFlag();
    return juce::Result::ok();
}

void LooperBank::stopTrack (int looperIndex, int trackIndex, double qBeats) noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return;

    commands.push ({ ClipCommand::Type::stopTrack, looperIndex, trackIndex, nullptr, qBeats });
    mtTrackPlaying[static_cast<std::size_t> (looperIndex)]
                  [static_cast<std::size_t> (trackIndex)] = false;
    refreshPlayingFlag();
}

void LooperBank::stopAll() noexcept
{
    stopAllRequested.store (true, std::memory_order_release);
    for (auto& looper : mtTrackPlaying)
        looper.fill (false);
    playingFlag.store (false, std::memory_order_relaxed);
}

void LooperBank::refreshPlayingFlag() noexcept
{
    bool any = false;
    for (const auto& looper : mtTrackPlaying)
        for (const auto playing : looper)
            any = any || playing;
    playingFlag.store (any, std::memory_order_relaxed);
}

bool LooperBank::isTrackPlaying (int looperIndex, int trackIndex) const noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return false;

    return mtTrackPlaying[static_cast<std::size_t> (looperIndex)]
                         [static_cast<std::size_t> (trackIndex)];
}

bool LooperBank::getClipInfo (int looperIndex, int trackIndex, ClipInfo& out) const noexcept
{
    const auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return false;

    out.rate = clip->stagedRate.load (std::memory_order_relaxed);
    out.lengthBeats = clip->stagedLengthBeats.load (std::memory_order_relaxed);
    out.reversed = clip->stagedReversed.load (std::memory_order_relaxed);
    out.commitBars = clip->commitBars;
    out.clipId = clip->clipId;
    return true;
}

//==============================================================================
// Clip-Edits [MT] — jede Operation schreibt den KOMPLETTEN Staged-Satz

void LooperBank::stageClipParams (LooperClip& clip, double rate, double lengthBeats,
                                  double windowOffsetBeats, bool reversed,
                                  bool followPhase, bool atWrap, bool resetGrid) noexcept
{
    clip.stagedRate.store (rate, std::memory_order_relaxed);
    clip.stagedLengthBeats.store (lengthBeats, std::memory_order_relaxed);
    clip.stagedWindowOffsetBeats.store (windowOffsetBeats, std::memory_order_relaxed);
    clip.stagedReversed.store (reversed, std::memory_order_relaxed);
    clip.windowFollowsPhase.store (followPhase, std::memory_order_relaxed);
    clip.applyAtWrap.store (atWrap, std::memory_order_relaxed);
    clip.resetAnchorToGrid.store (resetGrid, std::memory_order_relaxed);
    clip.paramVersion.fetch_add (1, std::memory_order_release);
}

void LooperBank::setClipRate (LooperClip& clip, double rate) noexcept
{
    stageClipParams (clip,
                     juce::jlimit (minRate, maxRate, rate),
                     clip.stagedLengthBeats.load (std::memory_order_relaxed),
                     clip.stagedWindowOffsetBeats.load (std::memory_order_relaxed),
                     clip.stagedReversed.load (std::memory_order_relaxed),
                     false, false, false);
}

void LooperBank::toggleClipReverse (LooperClip& clip, bool atBoundary) noexcept
{
    stageClipParams (clip,
                     clip.stagedRate.load (std::memory_order_relaxed),
                     clip.stagedLengthBeats.load (std::memory_order_relaxed),
                     clip.stagedWindowOffsetBeats.load (std::memory_order_relaxed),
                     ! clip.stagedReversed.load (std::memory_order_relaxed),
                     false, atBoundary, false);
}

void LooperBank::multiplyClipLength (LooperClip& clip, bool doubleLength,
                                     looper::HalveMode halveMode) noexcept
{
    const auto currentLength = clip.stagedLengthBeats.load (std::memory_order_relaxed);
    auto window = clip.stagedWindowOffsetBeats.load (std::memory_order_relaxed);

    // Clamps: ≥ 1 Takt, ≤ Content (×2 liest weiter in den committeten
    // Content — Inhalt wächst NICHT, User-Entscheidung „nur L ändern")
    const auto newLength = juce::jlimit (looper::quantumBeats, clip.contentBeats,
                                         doubleLength ? currentLength * 2.0
                                                      : currentLength * 0.5);
    if (juce::exactlyEqual (newLength, currentLength))
        return;

    bool followPhase = false;
    if (doubleLength)
    {
        // Fenster ggf. nach vorn schieben, damit window + L in den Content passt
        window = juce::jlimit (0.0, juce::jmax (0.0, clip.contentBeats - newLength), window);
    }
    else if (halveMode == looper::HalveMode::currentHalf)
    {
        // Fenster folgt der Apply-Phase — der Audio-Thread berechnet den
        // Offset mit SEINEM Playhead (windowFollowsPhase, LooperClip-Doku)
        followPhase = true;
    }

    stageClipParams (clip,
                     clip.stagedRate.load (std::memory_order_relaxed),
                     newLength, window,
                     clip.stagedReversed.load (std::memory_order_relaxed),
                     followPhase, false, false);
}

void LooperBank::resetClipWithSync (LooperClip& clip) noexcept
{
    stageClipParams (clip,
                     1.0,
                     clip.stagedLengthBeats.load (std::memory_order_relaxed),
                     clip.stagedWindowOffsetBeats.load (std::memory_order_relaxed),
                     clip.stagedReversed.load (std::memory_order_relaxed),
                     false, false, true);
}

juce::Result LooperBank::setClipRate (int looperIndex, int trackIndex, double rate)
{
    auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip auf diesem Track");

    setClipRate (*clip, rate);
    return juce::Result::ok();
}

juce::Result LooperBank::toggleClipReverse (int looperIndex, int trackIndex, bool atBoundary)
{
    auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip auf diesem Track");

    toggleClipReverse (*clip, atBoundary);
    return juce::Result::ok();
}

juce::Result LooperBank::multiplyClipLength (int looperIndex, int trackIndex,
                                             bool doubleLength, looper::HalveMode halveMode)
{
    auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip auf diesem Track");

    multiplyClipLength (*clip, doubleLength, halveMode);
    return juce::Result::ok();
}

juce::Result LooperBank::resetClipWithSync (int looperIndex, int trackIndex)
{
    auto* clip = activeClipFor (looperIndex, trackIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Kein Clip auf diesem Track");

    resetClipWithSync (*clip);
    return juce::Result::ok();
}

//==============================================================================
// Track-Mix [MT]

void LooperBank::setTrackGain (int looperIndex, int trackIndex, float gain01) noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return;

    targetGain[static_cast<std::size_t> (looperIndex)][static_cast<std::size_t> (trackIndex)]
        .store (juce::jlimit (0.0f, 2.0f, gain01), std::memory_order_relaxed);
}

void LooperBank::setTrackPan (int looperIndex, int trackIndex, float pan) noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return;

    targetPan[static_cast<std::size_t> (looperIndex)][static_cast<std::size_t> (trackIndex)]
        .store (juce::jlimit (-1.0f, 1.0f, pan), std::memory_order_relaxed);
}

void LooperBank::setTrackMute (int looperIndex, int trackIndex, bool muted) noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return;

    mtMute[static_cast<std::size_t> (looperIndex)][static_cast<std::size_t> (trackIndex)] = muted;
    updateEffectiveMutes();
}

void LooperBank::setTrackSolo (int looperIndex, int trackIndex, bool solo) noexcept
{
    if (looperIndex < 0 || looperIndex >= maxLoopers
        || trackIndex < 0 || trackIndex >= maxTracks)
        return;

    mtSolo[static_cast<std::size_t> (looperIndex)][static_cast<std::size_t> (trackIndex)] = solo;
    updateEffectiveMutes();
}

void LooperBank::setSoloScopeGlobal (bool global) noexcept
{
    soloScopeGlobal = global;
    updateEffectiveMutes();
}

void LooperBank::updateEffectiveMutes() noexcept
{
    bool anySoloGlobal = false;
    std::array<bool, static_cast<std::size_t> (maxLoopers)> anySoloInLooper {};

    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t t = 0; t < static_cast<std::size_t> (maxTracks); ++t)
            if (mtSolo[l][t])
            {
                anySoloGlobal = true;
                anySoloInLooper[l] = true;
            }

    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t t = 0; t < static_cast<std::size_t> (maxTracks); ++t)
        {
            const auto soloActive = soloScopeGlobal ? anySoloGlobal : anySoloInLooper[l];
            const auto muted = mtMute[l][t] || (soloActive && ! mtSolo[l][t]);
            effectiveMute[l][t].store (muted, std::memory_order_relaxed);
        }
}

//==============================================================================
void LooperBank::moveToGraveyard (LooperClip* clip)
{
    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t t = 0; t < static_cast<std::size_t> (maxTracks); ++t)
            if (mtActiveClip[l][t] == clip)
            {
                mtActiveClip[l][t] = nullptr;
                mtTrackPlaying[l][t] = false;
            }
    refreshPlayingFlag();

    for (auto it = store.begin(); it != store.end(); ++it)
    {
        if (it->get() == clip)
        {
            graveyard.push_back ({ std::move (*it), false });
            store.erase (it);
            return;
        }
    }

    jassertfalse;  // Clip war nicht im Store — Buchführungsfehler
}

void LooperBank::serviceMessageThread()
{
    LooperClip* released = nullptr;
    while (retired.pop (released))
    {
        bool found = false;
        for (auto& grave : graveyard)
        {
            if (grave.clip.get() == released)
            {
                grave.audioReleased = true;
                found = true;
                break;
            }
        }

        jassert (found);  // Quittung ohne Graveyard-Eintrag = Protokollfehler
        juce::ignoreUnused (found);
    }

    std::erase_if (graveyard, [this] (const Grave& grave)
    {
        if (! grave.audioReleased
            || grave.clip->exportPins.load (std::memory_order_acquire) > 0)
            return false;

        ramBytesUsed.fetch_sub (grave.clip->allocatedBytes(), std::memory_order_relaxed);
        return true;
    });
}

void LooperBank::readChannelChunked (const CaptureChannel* channel,
                                     std::uint64_t startPosition,
                                     float* dest, int numSamples)
{
    if (channel == nullptr || numSamples <= 0)
        return;

    // Export-Halte-Protokoll: parallel zum CaptureWriter legal (Zähler);
    // schlägt die Anmeldung fehl (Freigabe läuft), bleibt der Loop Stille
    auto* mutableChannel = const_cast<CaptureChannel*> (channel);
    if (! mutableChannel->tryBeginExportRead())
        return;

    constexpr int chunkSamples = 65536;
    for (int offset = 0; offset < numSamples; offset += chunkSamples)
    {
        const auto thisChunk = juce::jmin (chunkSamples, numSamples - offset);
        if (! channel->read (startPosition + static_cast<std::uint64_t> (offset),
                             dest + offset, thisChunk))
        {
            // Loch → Stille (buffer ist bereits geleert)
        }
    }

    mutableChannel->endExportRead();
}

//==============================================================================
// [Audio] Kommandos & Pending-Actions

bool LooperBank::anyVoiceReferences (const LooperClip* clip) const noexcept
{
    for (const auto& looper : players)
        for (const auto& track : looper)
            for (const auto& voice : track.voices)
                if (voice.clip == clip)
                    return true;

    return false;
}

bool LooperBank::transferOrRetire (LooperClip* clip) noexcept
{
    for (auto& looper : players)
        for (auto& track : looper)
            for (auto& voice : track.voices)
                if (voice.clip == clip)
                {
                    voice.retireOnEnd = true;
                    return true;
                }

    return retired.push (clip);
}

void LooperBank::launchVoice (TrackPlayer& player, LooperClip* clip, int startOffset) noexcept
{
    // Laufende Voices des Tracks ausblenden (Crossfade ab startOffset)
    for (auto& voice : player.voices)
        if (voice.clip != nullptr && ! voice.fading)
        {
            voice.fading = true;
            voice.fadeStartOffset = startOffset;
        }

    // Freie Voice nehmen; sind alle belegt (Doppel-Re-Commit im Fade-
    // Fenster), fällt die leiseste — deren Retire-Pflicht wandert sofort
    // weiter (Drain-Guard hat den Queue-Platz reserviert)
    Voice* slot = nullptr;
    for (auto& voice : player.voices)
    {
        if (voice.clip == nullptr)
        {
            slot = &voice;
            break;
        }

        if (slot == nullptr || voice.gain < slot->gain)
            slot = &voice;
    }

    if (slot->clip != nullptr && slot->retireOnEnd)
    {
        auto* stolen = slot->clip;
        slot->clip = nullptr;
        transferOrRetire (stolen);
    }

    slot->clip = clip;
    slot->gain = 0.0f;
    slot->fading = false;
    slot->retireOnEnd = false;
    slot->startOffset = startOffset;
    slot->fadeStartOffset = 0;
}

void LooperBank::handleActivate (const ClipCommand& command) noexcept
{
    auto& player = players[static_cast<std::size_t> (command.looper)]
                          [static_cast<std::size_t> (command.track)];

    // Ein Commit ersetzt den Track-Inhalt — geparkte Aktionen verfallen
    player.pending = PendingAction {};
    launchVoice (player, command.clip, 0);
}

void LooperBank::handleDelete (const ClipCommand& command) noexcept
{
    // Geparkte Aktionen auf den gelöschten Clip verfallen (sonst würde
    // eine Pending-Start-Aktion einen Graveyard-Clip wiederbeleben — UAF)
    for (auto& looper : players)
        for (auto& track : looper)
            if (track.pending.clip == command.clip)
                track.pending = PendingAction {};

    // Über die ganze Matrix suchen (Retrigger kann denselben Clip in
    // mehreren Voices halten); genau EINE Voice erbt die Retire-Pflicht
    bool owner = false;
    for (auto& looper : players)
        for (auto& track : looper)
            for (auto& voice : track.voices)
            {
                if (voice.clip != command.clip)
                    continue;

                voice.fading = true;
                voice.fadeStartOffset = 0;
                if (! owner)
                {
                    voice.retireOnEnd = true;
                    owner = true;
                }
            }

    if (! owner)
        retired.push (command.clip);  // nirgends referenziert → sofort quittieren
}

void LooperBank::drainCommands() noexcept
{
    // Drain-Guard: nur konsumieren, solange die Retire-Queue Luft für alle
    // denkbaren Quittungen hat — Überschuss wartet einen Block
    const auto guard = maxLoopers * maxTracks * voicesPerTrack + 2;

    ClipCommand command;
    while (retired.getNumFree() > guard && commands.pop (command))
    {
        if (command.looper < 0 || command.looper >= maxLoopers
            || command.track < 0 || command.track >= maxTracks)
            continue;

        auto& player = players[static_cast<std::size_t> (command.looper)]
                              [static_cast<std::size_t> (command.track)];

        switch (command.type)
        {
            case ClipCommand::Type::activate:   handleActivate (command); break;
            case ClipCommand::Type::deleteClip: handleDelete (command); break;

            // Quantisierte Aktionen parken pro Track (letzte gewinnt);
            // Ausführung sample-genau in executePending
            case ClipCommand::Type::start:
                player.pending = { PendingAction::Type::start, command.clip, command.qBeats };
                break;

            case ClipCommand::Type::retrigger:
                player.pending = { PendingAction::Type::retrigger, command.clip, command.qBeats };
                break;

            case ClipCommand::Type::stopTrack:
                player.pending = { PendingAction::Type::stop, nullptr, command.qBeats };
                break;
        }
    }
}

void LooperBank::executePending (TrackPlayer& player, double blockStartBeat,
                                 double beatStep, int numSamples) noexcept
{
    if (player.pending.type == PendingAction::Type::none)
        return;

    const auto qBeats = player.pending.qBeats;
    const auto offset = qBeats <= 0.0
                      ? 0
                      : looper::gridCrossingOffset (blockStartBeat, beatStep,
                                                    numSamples, qBeats);
    if (offset < 0)
        return;  // Grid-Grenze liegt hinter diesem Block — weiter warten

    switch (player.pending.type)
    {
        case PendingAction::Type::start:
            launchVoice (player, player.pending.clip, offset);
            break;

        case PendingAction::Type::retrigger:
            if (auto* clip = player.pending.clip)
            {
                // Anker = exakter Grid-Beat (nicht der sample-gerundete) —
                // Phase 0 liegt damit mathematisch AUF der Grenze
                clip->activeAnchorBeat = qBeats > 0.0
                    ? std::ceil (blockStartBeat / qBeats - 1.0e-9) * qBeats
                    : blockStartBeat + beatStep * offset;
                launchVoice (player, clip, offset);
            }
            break;

        case PendingAction::Type::stop:
            for (auto& voice : player.voices)
                if (voice.clip != nullptr && ! voice.fading)
                {
                    voice.fading = true;
                    voice.fadeStartOffset = offset;
                }
            break;

        case PendingAction::Type::none:
            break;
    }

    player.pending = PendingAction {};
}

//==============================================================================
// [Audio] Clip-Parameter-Anwendung (Staged → Active)

LooperBank::CandidateParams LooperBank::computeCandidate (const LooperClip& clip,
                                                          double playheadBeat) noexcept
{
    CandidateParams candidate;
    candidate.version = clip.paramVersion.load (std::memory_order_acquire);
    candidate.rate = juce::jlimit (0.01, 16.0,
                                   clip.stagedRate.load (std::memory_order_relaxed));
    candidate.reversed = clip.stagedReversed.load (std::memory_order_relaxed);

    const auto stagedLength = clip.stagedLengthBeats.load (std::memory_order_relaxed);
    candidate.lengthBeats = stagedLength > 0.0 ? stagedLength : clip.activeLengthBeats;

    // Aktuelle Lese-Position (Beats im Content) unter den ACTIVE-Parametern
    const auto phaseOld = looper::clipPhaseBeats (playheadBeat, clip.activeAnchorBeat,
                                                  clip.activeRate, clip.activeLengthBeats);
    const auto readOld = looper::clipReadBeat (phaseOld, clip.activeLengthBeats,
                                               clip.activeReversed,
                                               clip.activeWindowOffsetBeats);

    auto window = clip.stagedWindowOffsetBeats.load (std::memory_order_relaxed);
    if (clip.windowFollowsPhase.load (std::memory_order_relaxed))
    {
        // ÷2 „aktuelle Hälfte": Fenster springt auf die gerade spielende
        // Hälfte — berechnet aus der Apply-Phase (Lese-Kontinuität)
        const auto rel = readOld - window;
        if (rel > 0.0 && candidate.lengthBeats > 0.0)
            window += std::floor (rel / candidate.lengthBeats) * candidate.lengthBeats;
    }
    window = juce::jlimit (0.0, juce::jmax (0.0, clip.contentBeats - candidate.lengthBeats),
                           window);
    candidate.windowOffsetBeats = window;

    if (clip.resetAnchorToGrid.load (std::memory_order_relaxed))
    {
        // „Reset mit Sync": zurück aufs Commit-Taktraster — Positions-
        // Sprung gewollt, der Splice-Duck deckt ihn
        candidate.anchorBeat = clip.commitEndBeat;
        return candidate;
    }

    // Positions-Kontinuität: Anker so wählen, dass die Lese-Position im
    // Apply-Moment exakt stehen bleibt (geschlossene Form — subsumiert
    // Rate-Wechsel, Reverse-Spiegelung und Fenster-Verschiebungen)
    auto directed = readOld - window;
    if (! (directed >= 0.0 && directed < candidate.lengthBeats))
        directed = looper::wrapBeats (directed, candidate.lengthBeats);

    const auto phase = candidate.reversed
                     ? looper::wrapBeats (candidate.lengthBeats - directed, candidate.lengthBeats)
                     : directed;
    candidate.anchorBeat = playheadBeat - phase / candidate.rate;
    return candidate;
}

void LooperBank::applyClipParams (LooperClip& clip, double blockStartBeat,
                                  double beatStep, int numSamples) noexcept
{
    const auto gainStep = 1.0f / static_cast<float> (juce::jmax (1, clip.crossfadeSamples));

    const auto apply = [&] (const CandidateParams& candidate) noexcept
    {
        clip.activeAnchorBeat = candidate.anchorBeat;
        clip.activeRate = candidate.rate;
        clip.activeLengthBeats = candidate.lengthBeats;
        clip.activeWindowOffsetBeats = candidate.windowOffsetBeats;
        clip.activeReversed = candidate.reversed;
        clip.appliedVersion = candidate.version;
    };

    if (clip.splicePending)
    {
        // Duck unten angekommen → unter der Stille anwenden (frisch
        // berechnet — Staged kann sich seit dem Einleiten geändert haben)
        if (clip.spliceGain <= 0.0f)
        {
            apply (computeCandidate (clip, blockStartBeat));
            clip.splicePending = false;
        }
    }
    else if (clip.paramVersion.load (std::memory_order_acquire) != clip.appliedVersion)
    {
        bool defer = false;
        if (clip.applyAtWrap.load (std::memory_order_relaxed))
        {
            // Reverse-Modus „an der Loop-Grenze": erst im Wrap-Block anwenden
            const auto phaseStart = looper::clipPhaseBeats (blockStartBeat,
                                                            clip.activeAnchorBeat,
                                                            clip.activeRate,
                                                            clip.activeLengthBeats);
            const auto phaseEnd = looper::clipPhaseBeats (
                blockStartBeat + beatStep * numSamples, clip.activeAnchorBeat,
                clip.activeRate, clip.activeLengthBeats);
            defer = phaseEnd >= phaseStart;
        }

        if (! defer)
        {
            const auto candidate = computeCandidate (clip, blockStartBeat);

            const auto posOld = looper::clipReadPosition (
                blockStartBeat, clip.activeAnchorBeat, clip.activeRate,
                clip.activeLengthBeats, clip.activeReversed,
                clip.activeWindowOffsetBeats, clip.samplesPerBeatRecorded);
            const auto posNew = looper::clipReadPosition (
                blockStartBeat, candidate.anchorBeat, candidate.rate,
                candidate.lengthBeats, candidate.reversed,
                candidate.windowOffsetBeats, clip.samplesPerBeatRecorded);

            if (std::abs (posNew - posOld) <= spliceThresholdSamples)
                apply (candidate);
            else
                clip.splicePending = true;   // Lese-Sprung → hinter den Duck
        }
    }

    // Anzeige-Phase fürs UI (Progress-Sweep) — einmal pro Block
    if (clip.activeLengthBeats > 0.0)
        clip.displayPhase01.store (
            static_cast<float> (looper::clipPhaseBeats (blockStartBeat,
                                                        clip.activeAnchorBeat,
                                                        clip.activeRate,
                                                        clip.activeLengthBeats)
                                / clip.activeLengthBeats),
            std::memory_order_relaxed);

    // Block-Snapshot der Splice-Rampe fürs Rendering
    clip.spliceStartGain = clip.spliceGain;
    clip.spliceStep = (clip.splicePending ? -1.0f : 1.0f) * gainStep;
    clip.spliceGain = juce::jlimit (0.0f, 1.0f,
                                    clip.spliceGain
                                        + clip.spliceStep * static_cast<float> (numSamples));
}

//==============================================================================
float LooperBank::renderClipSample (const LooperClip& clip, int channel,
                                    double contentPosition) noexcept
{
    // Buffer-Layout: [0, F) Lead-in, [F, F+N) Content — Position ∈ [0, N)
    const auto* data = clip.buffer.getReadPointer (channel);
    const auto fade = clip.crossfadeSamples;
    const auto lastIndex = clip.numContentSamples + fade - 1;

    const auto readLinear = [&] (double position) noexcept -> float
    {
        const auto clamped = juce::jlimit (0.0, static_cast<double> (lastIndex), position);
        const auto index = static_cast<int> (clamped);
        const auto frac = static_cast<float> (clamped - index);
        const auto next = juce::jmin (index + 1, lastIndex);
        return data[index] + (data[next] - data[index]) * frac;
    };

    const auto zoneStart = static_cast<double> (clip.numContentSamples - fade);

    if (contentPosition < zoneStart || clip.numContentSamples <= fade)
        return readLinear (static_cast<double> (fade) + contentPosition);

    // Wrap-Zone [N−F, N): equal-power vom Content-Ende auf den Lead-in
    const auto zonePosition = contentPosition - zoneStart;          // [0, F)
    const auto alpha = zonePosition / static_cast<double> (fade);   // 0..1

    const auto endSample  = readLinear (static_cast<double> (fade) + contentPosition);
    const auto leadSample = readLinear (zonePosition);              // Lead-in → Loop-Start

    const auto angle = alpha * juce::MathConstants<double>::halfPi;
    return endSample  * static_cast<float> (std::cos (angle))
         + leadSample * static_cast<float> (std::sin (angle));
}

void LooperBank::renderBlock (const ClockState& clock, std::uint64_t blockStartSample,
                              const BarSampleAnchors& anchors, int numSamples) noexcept
{
    ++blockCounter;
    renderedSamples = 0;   // erst nach vollständigem Rendern gültig (AudioView)

    // Stop: alle Voices ausblenden (Clips bleiben im Store)
    if (stopAllRequested.exchange (false, std::memory_order_acq_rel))
        for (auto& looper : players)
            for (auto& track : looper)
            {
                track.pending = PendingAction {};
                for (auto& voice : track.voices)
                    if (voice.clip != nullptr)
                        voice.fading = true;
            }

    drainCommands();

    const auto beatsPerSample = clock.beatsPerSample();
    if (numSamples <= 0 || beatsPerSample <= 0.0
        || numSamples > static_cast<int> (scratchLeft.size()))
        return;

    // Beat-Messung jitter-frei aus der SampleClock (Herleitung CLAUDE.md
    // 10.0: Anker-Beat + Sample-Abstand statt Wall-Clock)
    auto measuredBeat = clock.beatAtBlockStart;
    {
        const auto latestBar = anchors.latestBoundaryBar();
        std::uint64_t anchorSample = 0;
        if (latestBar >= 0 && anchors.lookup (latestBar, anchorSample))
            measuredBeat = static_cast<double> (latestBar) * looper::quantumBeats
                         + static_cast<double> (static_cast<std::int64_t> (
                               blockStartSample - anchorSample))
                               * beatsPerSample;
    }

    if (! playheadValid)
    {
        playheadBeat  = measuredBeat;
        playheadValid = true;
        snapPendingCount = 0;
        snapDucking = false;
        duckGain = 1.0f;
    }
    else if (std::abs (measuredBeat - playheadBeat) > snapThresholdBeats)
    {
        if (++snapPendingCount >= snapConfirmBlocks)
            snapDucking = true;
    }
    else
    {
        snapPendingCount = 0;
    }

    if (snapDucking && duckGain <= 0.0f)
    {
        playheadBeat = measuredBeat;
        snapDucking = false;
        snapPendingCount = 0;
        snapCount.fetch_add (1, std::memory_order_relaxed);
    }

    const auto maxCorrection = maxSlewRatio * beatsPerSample
                             * static_cast<double> (numSamples);
    const auto correction = juce::jlimit (-maxCorrection, maxCorrection,
                                          measuredBeat - playheadBeat);
    const auto beatStep = beatsPerSample
                        + correction / static_cast<double> (numSamples);
    const auto blockStartBeat = playheadBeat;
    playheadBeat += beatStep * static_cast<double> (numSamples);

    // Looper-I/O-Busse dieses Blocks nullen — Looper ohne Voices bleiben
    // Stille, das Looper-Out-Modul liest immer definierte Daten
    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t c = 0; c < 2; ++c)
        {
            juce::FloatVectorOperations::clear (preBus[l][c].data(),  numSamples);
            juce::FloatVectorOperations::clear (postBus[l][c].data(), numSamples);
        }
    for (auto& channel : masterBus)
        juce::FloatVectorOperations::clear (channel.data(), numSamples);

    const auto gainStep = 1.0f / static_cast<float> (juce::jmax (1, crossfadeSamples));
    const auto mixStep = preparedSampleRate > 0.0
                       ? static_cast<float> (1.0 / (mixSlewSeconds * preparedSampleRate))
                       : gainStep;

    // Duck-Rampe des Blocks (Snap-Declick)
    const auto duckStartGain = duckGain;
    const auto duckStep = (snapDucking ? -1.0f : 1.0f) * gainStep;
    duckGain = juce::jlimit (0.0f, 1.0f,
                             duckGain + duckStep * static_cast<float> (numSamples));

    auto* scratch0 = scratchLeft.data();
    auto* scratch1 = scratchRight.data();

    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t t = 0; t < static_cast<std::size_t> (maxTracks); ++t)
        {
            auto& player = players[l][t];

            executePending (player, blockStartBeat, beatStep, numSamples);

            bool anyVoice = false;
            for (const auto& voice : player.voices)
                anyVoice = anyVoice || voice.clip != nullptr;
            if (! anyVoice)
                continue;

            for (int i = 0; i < numSamples; ++i)
            {
                scratch0[i] = 0.0f;
                scratch1[i] = 0.0f;
            }

            //==============================================================
            // Voices → Scratch (pre-fader)
            for (auto& voice : player.voices)
            {
                auto* clip = voice.clip;
                if (clip == nullptr)
                    continue;

                if (clip->numContentSamples <= 0 || clip->contentBeats <= 0.0)
                {
                    voice.clip = nullptr;
                    continue;
                }

                // Staged → Active genau einmal pro Block (exakter Playhead)
                if (clip->lastSeenBlock != blockCounter)
                {
                    clip->lastSeenBlock = blockCounter;
                    applyClipParams (*clip, blockStartBeat, beatStep, numSamples);
                }

                if (clip->activeLengthBeats <= 0.0)
                {
                    voice.clip = nullptr;
                    continue;
                }

                const auto maxPosition = static_cast<double> (clip->numContentSamples) - 1.0e-9;
                const auto spliceStart = clip->spliceStartGain;
                const auto spliceStep  = clip->spliceStep;
                const auto stereoClip  = clip->buffer.getNumChannels() > 1;

                for (int i = voice.startOffset; i < numSamples; ++i)
                {
                    const auto fadingNow = voice.fading && i >= voice.fadeStartOffset;

                    voice.gain = fadingNow ? juce::jmax (0.0f, voice.gain - gainStep)
                                           : juce::jmin (1.0f, voice.gain + gainStep);

                    if (voice.gain <= 0.0f && fadingNow)
                        break;

                    const auto beat = blockStartBeat + beatStep * i;
                    const auto position = juce::jlimit (
                        0.0, maxPosition,
                        looper::clipReadPosition (beat, clip->activeAnchorBeat,
                                                  clip->activeRate,
                                                  clip->activeLengthBeats,
                                                  clip->activeReversed,
                                                  clip->activeWindowOffsetBeats,
                                                  clip->samplesPerBeatRecorded));

                    const auto duck = juce::jlimit (0.0f, 1.0f,
                                                    duckStartGain + duckStep * static_cast<float> (i));
                    const auto splice = juce::jlimit (0.0f, 1.0f,
                                                      spliceStart + spliceStep * static_cast<float> (i));
                    const auto factor = voice.gain * duck * splice;

                    // Mono-Clip: Kanal 0 speist beide Seiten (Pan wirkt weiter)
                    const auto sample0 = renderClipSample (*clip, 0, position);
                    const auto sample1 = stereoClip ? renderClipSample (*clip, 1, position)
                                                    : sample0;
                    scratch0[i] += sample0 * factor;
                    scratch1[i] += sample1 * factor;
                }

                voice.startOffset = 0;

                // Voice-Ende: Retire-Pflicht quittieren, sobald keine andere
                // Voice den Clip mehr referenziert (Retrigger-Schutz)
                if (voice.fading && voice.gain <= 0.0f)
                {
                    voice.fadeStartOffset = 0;

                    if (voice.retireOnEnd)
                    {
                        auto* toRetire = voice.clip;
                        voice.clip = nullptr;

                        if (! transferOrRetire (toRetire))
                        {
                            voice.clip = toRetire;      // Queue voll → nächster Block
                            continue;
                        }
                    }
                    else
                    {
                        voice.clip = nullptr;
                    }

                    voice.gain = 0.0f;
                    voice.fading = false;
                    voice.retireOnEnd = false;
                }
                else
                {
                    voice.fadeStartOffset = 0;
                }
            }

            //==============================================================
            // Pre-Fader-Bus des Loopers (Looper-Out-Abgriff „Pre")
            juce::FloatVectorOperations::add (preBus[l][0].data(), scratch0, numSamples);
            juce::FloatVectorOperations::add (preBus[l][1].data(), scratch1, numSamples);

            //==============================================================
            // Fader-Zug: Gain (5-ms-Slew) + Equal-Power-Pan + Mute →
            // Post-Fader-Meter → additiv auf den Post-Bus des Loopers
            const auto gainTarget = targetGain[l][t].load (std::memory_order_relaxed);
            const auto panTarget  = targetPan[l][t].load (std::memory_order_relaxed);
            const auto muteTarget = effectiveMute[l][t].load (std::memory_order_relaxed)
                                  ? 0.0f : 1.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                player.currentGain += juce::jlimit (-mixStep, mixStep,
                                                    gainTarget - player.currentGain);
                player.currentPan  += juce::jlimit (-mixStep, mixStep,
                                                    panTarget - player.currentPan);
                player.currentMuteGain += juce::jlimit (-mixStep, mixStep,
                                                        muteTarget - player.currentMuteGain);

                // Balance-Pan (Stereo-Clips): Mitte = Unity, die abgewandte
                // Seite fällt equal-power — kein −3-dB-Loch in der Mitte
                const auto pan = player.currentPan;
                const auto halfPi = juce::MathConstants<float>::halfPi;
                const auto gainLeft  = pan > 0.0f ? std::cos (pan * halfPi) : 1.0f;
                const auto gainRight = pan < 0.0f ? std::cos (-pan * halfPi) : 1.0f;
                const auto amp = player.currentGain * player.currentMuteGain;

                scratch0[i] *= amp * gainLeft;
                scratch1[i] *= amp * gainRight;
            }

            {
                float* channels[] = { scratch0, scratch1 };
                const juce::AudioBuffer<float> view (channels, 2, numSamples);
                trackMeters[l][t].process (view, 2);
            }

            juce::FloatVectorOperations::add (postBus[l][0].data(), scratch0, numSamples);
            juce::FloatVectorOperations::add (postBus[l][1].data(), scratch1, numSamples);
        }

    // Master-Mix = Summe der Post-Fader-Busse aller Looper mit
    // „an Master senden" (Looper-I/O 07/2026)
    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        if (looperToMaster[l].load (std::memory_order_relaxed))
            for (std::size_t c = 0; c < 2; ++c)
                juce::FloatVectorOperations::add (masterBus[c].data(),
                                                  postBus[l][c].data(), numSamples);

    renderedSamples = numSamples;
}

void LooperBank::mixToOutput (juce::AudioBuffer<float>& buffer, int numOutputChannels) noexcept
{
    const auto pair = anchor.load (std::memory_order_relaxed);
    if (pair < 0 || renderedSamples <= 0)
        return;   // „Kein Master-Out" bzw. noch nichts gerendert

    const auto numSamples = juce::jmin (renderedSamples, buffer.getNumSamples());
    const auto channelA = pair * 2;
    const auto usableChannels = juce::jmin (numOutputChannels, buffer.getNumChannels());

    if (channelA < usableChannels)
        juce::FloatVectorOperations::add (buffer.getWritePointer (channelA),
                                          masterBus[0].data(), numSamples);
    if (channelA + 1 < usableChannels)
        juce::FloatVectorOperations::add (buffer.getWritePointer (channelA + 1),
                                          masterBus[1].data(), numSamples);
}

LooperBank::AudioView LooperBank::getAudioView() const noexcept
{
    AudioView view;
    view.numSamples = renderedSamples;

    if (renderedSamples <= 0)
        return view;   // Pointer bleiben null — Konsument gibt Stille aus

    view.master[0] = masterBus[0].data();
    view.master[1] = masterBus[1].data();

    for (std::size_t l = 0; l < static_cast<std::size_t> (maxLoopers); ++l)
        for (std::size_t c = 0; c < 2; ++c)
        {
            view.pre [l][c] = preBus [l][c].data();
            view.post[l][c] = postBus[l][c].data();
        }

    return view;
}

void LooperBank::process (juce::AudioBuffer<float>& buffer, int numOutputChannels,
                          const ClockState& clock, std::uint64_t blockStartSample,
                          const BarSampleAnchors& anchors) noexcept
{
    renderBlock (clock, blockStartSample, anchors, buffer.getNumSamples());
    mixToOutput (buffer, numOutputChannels);
}

} // namespace conduit
