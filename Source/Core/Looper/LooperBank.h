#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "BarSampleAnchors.h"
#include "Core/Capture/CaptureService.h"
#include "Core/Capture/LevelMeter.h"
#include "Interfaces/IClockSource.h"
#include "LooperClip.h"
#include "LooperClipMath.h"
#include "LooperDistance.h"
#include "LooperMath.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/**
    Multi-Looper-Playback des Looper-Vollausbaus (M2/M3) — Nachfolger der
    LooperEngine (Retro-Looper B5, git-Historie), deren Playhead/Wrap-
    Crossfade/Commit-Pfad hier 1:1 weiterleben. Herleitungen (Wall-Clock-
    Jitter-Lektion, Snap-Declick, Duck, Lead-in) stehen in CLAUDE.md 10.0
    „Retro-Looper" und den Kommentaren unten.

    Struktur: bis zu maxLoopers × maxTracks TrackPlayer, jeder mit
    voicesPerTrack Crossfade-Voices. Voices REFERENZIEREN LooperClips
    (right-sized beim Commit, ClipStore auf dem MT) statt eigene
    60-s-Buffer zu tragen — die ~46-MB-Prealloc der LooperEngine entfällt.

    Threading (strikt, CLAUDE.md 3.1):
      - Voices/Playhead sind AUDIO-only-Zustand; der MT fasst sie nie an.
      - MT → Audio: SpscQueue<ClipCommand> (activate/start/retrigger/
        stopTrack/deleteClip) + Atomics (stopAll, anchor, Track-Mix).
        Clips sind beim Push fertig gebaut.
      - Audio → MT: SpscQueue<LooperClip*> (Retire-Quittungen). Ein
        deleteClip wandert IMMER durch den Audio-Thread: erst wenn der die
        Quittung pusht, ist garantiert keine Voice-Referenz mehr da —
        serviceMessageThread() gibt dann frei (free nie im Audio-Thread).
        exportPins (Save-Geste M9) verzögern die Freigabe zusätzlich.
      - Drain-Guard: der Audio-Thread konsumiert Commands nur, solange die
        Retire-Queue Luft für alle denkbaren Quittungen des Blocks hat —
        Überschuss wartet einen Block, nichts geht verloren.

    M3 — Clip-Verhalten & Track-Mix:
      - Quantisierte Aktionen (Start/Retrigger/Stop) parken als Pending-
        Action pro Track; der Audio-Thread erkennt die Grid-Überquerung
        auf dem PLAYHEAD-Beat (Muster 4.5, gridCrossingOffset) und führt
        sample-genau am Intra-Block-Offset aus. qBeats 0 = Blockanfang.
        Start ist phasenstarr (Anker bleibt), Retrigger setzt den Anker
        auf den Grid-Beat (Phase 0 am Launch-Punkt).
      - Clip-Parameter (Rate/Reverse/×2÷2/Fenster/Reset-Sync) laufen über
        das Staged/Active-Protokoll des LooperClip: Audio wendet mit
        seinem exakten Playhead positions-kontinuierlich an; inhärente
        Lese-Sprünge (÷2 „erste Hälfte" aus der zweiten, Reset-Sync)
        duckt ein 5-ms-Splice-Dip pro Clip.
      - Track-Mix: Voices rendern in den preallozierten trackBus des
        Tracks (Big Out liest ihn post-fader); danach Gain (5-ms-Slew) +
        Equal-Power-Pan + effektives Mute (MT-berechnet aus Mute/Solo/
        Solo-Scope) → Post-Fader-LevelMeter → additiv aufs Anker-Paar.
        Meter laufen auch bei OOB-Anker weiter. Send-Busse 1..4 greifen
        pro Track wahlweise pre (vor dem Fader-Zug) oder post ab —
        seit dem Mixer (07/2026) mit stufenlosem LEVEL 0..1 pro Send
        (5-ms-Block-Ramp) statt An/Aus-Maske.

    RAM-Konto: right-sized Clips statt Prealloc; die Summe aller lebenden
    Clips (Store + Graveyard) ist auf ramBudgetBytes begrenzt — ein Commit
    über Budget schlägt sauber fehl (kein OOM im Live-Set). Default 1,5 GB.

    M4 bringt Slot-/Target-Logik (LooperSessionModel) — die Clip-Edits
    adressieren bis dahin den aktiven Clip eines Tracks.
*/
class LooperBank
{
public:
    static constexpr int maxLoopers = 4;
    static constexpr int maxTracks  = 4;
    static constexpr int maxSends   = 4;       // globale Send-Busse (Big Out)
    static constexpr int voicesPerTrack = 3;   // Crossfade + Doppel-Re-Commit

    static constexpr double maxLoopSeconds   = 60.0;
    static constexpr double crossfadeSeconds = 0.005;
    static constexpr double mixSlewSeconds   = 0.005;

    // Playhead-Konstanten — Herleitung siehe CLAUDE.md 10.0 (übernommen)
    static constexpr double maxSlewRatio       = 0.002;
    static constexpr double snapThresholdBeats = 0.15;
    static constexpr int    snapConfirmBlocks  = 2;

    // Lese-Sprünge oberhalb dieser Schwelle laufen hinter dem Splice-Duck
    static constexpr double spliceThresholdSamples = 64.0;

    static constexpr double minRate = 0.25, maxRate = 4.0;   // VARI ±2 Oktaven
    static constexpr std::int64_t defaultRamBudgetBytes = 1'500'000'000;

    LooperBank();

    /** [Message Thread, Audio steht — prepareToPlay] verwirft alle Clips
        und Voices (SampleRate-Wechsel invalidiert Positionen/Inhalte). */
    void prepare (double sampleRate, int maxBlockSamples);

    //==========================================================================
    // Commit & Transport [Message Thread]

    /** Die letzten `bars` kompletten Takte in den Track committen und
        sofort phasenstarr abspielen — ersetzt dessen laufenden Clip
        glitch-frei (Voice-Crossfade). rightIndex < 0 = Mono-Quelle →
        1-Kanal-Clip (halber RAM, Mono-Export; Playback speist beide
        Seiten). Fehlerfälle: keine Quelle, zu wenig Historie,
        Anker-Fenster, > maxLoopSeconds, RAM-Budget, Queue voll. */
    [[nodiscard]] juce::Result commitAndPlay (int looperIndex, int trackIndex,
                                              int bars, const CaptureService& capture,
                                              int leftIndex, int rightIndex,
                                              const BarSampleAnchors& anchors);

    /** Wie commitAndPlay, aber OHNE den bisherigen Track-Clip zu löschen —
        Slot-Modell (M4): der alte Clip gehört einem anderen Slot und
        bleibt liegen (gestoppt). outClip erhält den neuen Clip; der
        Besitz bleibt IMMER bei der Bank (Freigabe nur via deleteClip). */
    [[nodiscard]] juce::Result commitClip (int looperIndex, int trackIndex, int bars,
                                           const CaptureService& capture,
                                           int leftIndex, int rightIndex,
                                           const BarSampleAnchors& anchors,
                                           LooperClip** outClip = nullptr);

    /** [Message Thread] Clip endgültig löschen: Graveyard + deleteClip-
        Kommando — die Freigabe folgt der Audio-Quittung (Klassendoku).
        Slot-Bookkeeping macht das LooperSessionModel. */
    [[nodiscard]] juce::Result deleteClip (LooperClip* clip);

    /** Bestimmten Clip (Slot-Modell) auf einem Track starten — phasenstarr,
        Anker bleibt. Der Clip wird zum (l,t)-Edit-Ziel (mtActiveClip). */
    [[nodiscard]] juce::Result startClip (int looperIndex, int trackIndex,
                                          LooperClip* clip, double qBeats);

    /** Bestimmten Clip neu triggern (Phase 0 am Grid-Punkt). */
    [[nodiscard]] juce::Result retriggerClip (int looperIndex, int trackIndex,
                                              LooperClip* clip, double qBeats);

    /** Clip des Tracks (wieder) starten — phasenstarr, Anker bleibt.
        qBeats > 0: an der nächsten Grid-Grenze (sample-genau). */
    [[nodiscard]] juce::Result startTrack (int looperIndex, int trackIndex,
                                           double qBeats);

    /** Clip des Tracks neu triggern: Phase 0 am Ausführungspunkt
        (Anker = Grid-Beat). */
    [[nodiscard]] juce::Result retriggerTrack (int looperIndex, int trackIndex,
                                               double qBeats);

    /** Track stoppen (5-ms-Fade ab dem Grid-Punkt). */
    void stopTrack (int looperIndex, int trackIndex, double qBeats) noexcept;

    /** Alle Tracks sofort stoppen (Clips bleiben). */
    void stopAll() noexcept;

    //==========================================================================
    // Clip-Edits [Message Thread] — wirken auf den aktiven Clip des Tracks

    /** VARI: Rate 0.25×–4× (geclampt), positions-kontinuierlich. */
    [[nodiscard]] juce::Result setClipRate (int looperIndex, int trackIndex,
                                            double rate);

    /** Reverse-Toggle; atBoundary = erst an der Loop-Grenze anwenden. */
    [[nodiscard]] juce::Result toggleClipReverse (int looperIndex, int trackIndex,
                                                  bool atBoundary);

    /** ×2 (doubleLength=true) / ÷2 — ändert NUR die Länge L (Clamps:
        ≥ 1 Takt, ≤ Content). ÷2-Hälfte nach HalveMode. */
    [[nodiscard]] juce::Result multiplyClipLength (int looperIndex, int trackIndex,
                                                   bool doubleLength,
                                                   looper::HalveMode halveMode);

    /** „Reset mit Sync": Rate 1× und Anker zurück aufs Commit-Taktraster. */
    [[nodiscard]] juce::Result resetClipWithSync (int looperIndex, int trackIndex);

    //==========================================================================
    // Clip-Edits per Pointer [MT] — für die Aktiv-Clip-Auswahl des Modells
    // (TARGET-Halten kann JEDEN Clip wählen, nicht nur den spielenden)

    void setClipRate (LooperClip& clip, double rate) noexcept;
    void toggleClipReverse (LooperClip& clip, bool atBoundary) noexcept;
    void multiplyClipLength (LooperClip& clip, bool doubleLength,
                             looper::HalveMode halveMode) noexcept;
    void resetClipWithSync (LooperClip& clip) noexcept;

    //==========================================================================
    // Track-Mix [Message Thread schreibt, Audio liest]

    void setTrackGain (int looperIndex, int trackIndex, float gain01) noexcept;
    void setTrackPan  (int looperIndex, int trackIndex, float pan) noexcept;
    void setTrackMute (int looperIndex, int trackIndex, bool muted) noexcept;
    void setTrackSolo (int looperIndex, int trackIndex, bool solo) noexcept;

    /** Send-LEVEL des Tracks (0..1, geclampt) auf Bus sendIndex 0..3 —
        Audio slewt mit 5-ms-Block-Ramp dorthin (Mixer 07/2026). */
    void setTrackSendLevel (int looperIndex, int trackIndex, int sendIndex,
                            float level01) noexcept;

    /** Send-Abgriff des Tracks: true = pre (vor Gain/Pan/Mute), false = post. */
    void setTrackSendPre (int looperIndex, int trackIndex, bool pre) noexcept;

    /** Distanz-Ziel des Tracks (XY-Y, 0..1) — Audio slewt mit der
        Smooth-Zeit der Distanz-Globals dorthin (Monolake Distance). */
    void setTrackDistance (int looperIndex, int trackIndex, float distance01) noexcept;

    /** Distanz-Globals + Smooth-Zeit in Sekunden [MT schreibt, Audio liest]. */
    void setDistanceGlobals (const looper::DistanceGlobals& globals,
                             float smoothSeconds) noexcept;

    /** Solo-Scope (Menü-Option): false = pro Looper (Default), true = global. */
    void setSoloScopeGlobal (bool global) noexcept;

    /** Post-Fader-Meter des Tracks (UI liest transient pro Tick). */
    [[nodiscard]] const LevelMeter& getTrackMeter (int looperIndex,
                                                   int trackIndex) const noexcept
    {
        return trackMeters[static_cast<std::size_t> (juce::jlimit (0, maxLoopers - 1, looperIndex))]
                          [static_cast<std::size_t> (juce::jlimit (0, maxTracks - 1, trackIndex))];
    }

    //==========================================================================
    // Service & Routing

    /** [Message Thread] Retire-Quittungen einsammeln und Graveyard-Clips
        freigeben (exportPins beachtet) — vom Editor-Timer und vor jedem
        Commit gerufen. */
    void serviceMessageThread();

    /** Stereo-Anker: Loops auf Kanäle pairIndex*2 / +1 (global, M2).
        −1 = „Kein Master-Out" (Looper-I/O 07/2026): renderBlock läuft
        weiter (Meter, Looper-Out-Busse), mixToOutput schreibt nichts. */
    void setAnchor (int pairIndex) noexcept
    {
        anchor.store (juce::jmax (-1, pairIndex), std::memory_order_relaxed);
    }

    /** [Message Thread schreibt, Audio liest] Looper in die Master-Summe
        aufnehmen (Default true) — „an Master senden" pro Looper. */
    void setLooperToMaster (int looperIndex, bool toMaster) noexcept
    {
        if (looperIndex >= 0 && looperIndex < maxLoopers)
            looperToMaster[static_cast<std::size_t> (looperIndex)]
                .store (toMaster, std::memory_order_relaxed);
    }

    /** [Audio Thread, VOR graph.processBlock] Alle Voices rendern —
        Playback liest nur committete Clips und braucht den Graph-Block
        nicht. Füllt die internen Busse (pro Looper Pre-/Post-Fader-Stereo
        + Master-Mix), die das Looper-Out-Modul im selben Callback über
        getAudioView() liest — sample-aligned, ohne Block-Latenz.
        Zusätzlich pro Track ein Post-Fader-Stereo-Bus und die globalen
        Send-Busse 1..4 (Big Out). blockStartSample = SampleClock-Position
        des Block-Anfangs. */
    void renderBlock (const ClockState& clock, std::uint64_t blockStartSample,
                      const BarSampleAnchors& anchors, int numSamples) noexcept;

    /** [Audio Thread, NACH graph.processBlock] Master-Mix des zuletzt
        gerenderten Blocks additiv aufs Anker-Paar — No-op bei Anker −1
        („Kein Master-Out") oder OOB-Anker. */
    void mixToOutput (juce::AudioBuffer<float>& buffer, int numOutputChannels) noexcept;

    /** [NUR Audio Thread, im selben Callback NACH renderBlock] Sicht auf
        die gerenderten Busse fürs Looper-Out-Modul. numSamples = 0, wenn
        (noch) nichts gerendert wurde — Konsument gibt dann Stille aus. */
    struct AudioView
    {
        const float* master[2] { nullptr, nullptr };
        const float* pre [maxLoopers][2] {};    // vor Gain/Pan/Mute
        const float* post[maxLoopers][2] {};    // wie gehört (post-fader)
        const float* track[maxLoopers][maxTracks][2] {};   // pro Track, post-fader
        const float* send[maxSends][2] {};      // Send-Busse 1..4
        int numSamples = 0;
    };
    [[nodiscard]] AudioView getAudioView() const noexcept;

    /** [Audio Thread] Kompatibilitäts-Wrapper (Tests/Alt-Kontrakt):
        renderBlock + mixToOutput in einem Aufruf. */
    void process (juce::AudioBuffer<float>& buffer, int numOutputChannels,
                  const ClockState& clock, std::uint64_t blockStartSample,
                  const BarSampleAnchors& anchors) noexcept;

    //==========================================================================
    // Status [beliebiger Thread]

    [[nodiscard]] bool isPlaying() const noexcept
    {
        return playingFlag.load (std::memory_order_relaxed);
    }

    [[nodiscard]] bool isTrackPlaying (int looperIndex, int trackIndex) const noexcept;

    /** Takte des zuletzt committeten Loops (0 = keiner) — Paritäts-API. */
    [[nodiscard]] int getLoopBars() const noexcept
    {
        return isPlaying() ? committedBars.load (std::memory_order_relaxed) : 0;
    }

    /** Aktueller (staged) Parametersatz des aktiven Clips fürs UI —
        false, wenn der Track keinen Clip trägt. [Message Thread] */
    struct ClipInfo
    {
        double rate = 1.0;
        double lengthBeats = 0.0;
        bool reversed = false;
        int commitBars = 0;
        std::uint32_t clipId = 0;
    };
    [[nodiscard]] bool getClipInfo (int looperIndex, int trackIndex,
                                    ClipInfo& out) const noexcept;

    /** Diagnose: Playhead-Re-Syncs (Duck-Snaps) seit prepare. */
    [[nodiscard]] std::uint32_t getSnapCount() const noexcept
    {
        return snapCount.load (std::memory_order_relaxed);
    }

    /** RAM-Konto (Dev-Diagnose). */
    [[nodiscard]] std::int64_t getRamBytesUsed() const noexcept
    {
        return ramBytesUsed.load (std::memory_order_relaxed);
    }

    /** [Message Thread] Budget-Grenze (Tests / spätere Settings). */
    void setRamBudgetBytes (std::int64_t bytes) noexcept { ramBudgetBytes = bytes; }

private:
    //==========================================================================
    struct ClipCommand
    {
        enum class Type : int { activate = 0, start, retrigger, stopTrack, deleteClip };

        Type type = Type::activate;
        int looper = 0;
        int track = 0;
        LooperClip* clip = nullptr;
        double qBeats = 0.0;
    };

    // Audio-only: Voice referenziert einen Clip; retireOnEnd = dieser Voice
    // gehört die Retire-Quittung des Clips (deleteClip-Protokoll).
    // startOffset/fadeStartOffset sind BLOCK-lokal (sample-genaue Aktionen).
    struct Voice
    {
        LooperClip* clip = nullptr;
        float gain = 0.0f;
        bool fading = false;
        bool retireOnEnd = false;
        int startOffset = 0;
        int fadeStartOffset = 0;
    };

    // Audio-only: geparkte quantisierte Aktion des Tracks (letzte gewinnt)
    struct PendingAction
    {
        enum class Type : int { none = 0, start, retrigger, stop };
        Type type = Type::none;
        LooperClip* clip = nullptr;
        double qBeats = 0.0;
    };

    struct TrackPlayer
    {
        std::array<Voice, static_cast<std::size_t> (voicesPerTrack)> voices;
        PendingAction pending;

        // Mix-Zustand (Audio): geslewte Ist-Werte
        float currentGain = 1.0f;
        float currentPan = 0.0f;
        float currentMuteGain = 1.0f;
        std::array<float, static_cast<std::size_t> (maxSends)> currentSend {};

        // Distanz-Zug (Audio): geslewte Distanz + Filterzustände (2 Kanäle)
        float currentDistance = 0.0f;
        std::array<looper::BiquadState, 2> distLp {}, distShelf {};
    };

    // MT-only: Graveyard-Eintrag wartet auf die Audio-Quittung + Pins
    struct Grave
    {
        std::unique_ptr<LooperClip> clip;
        bool audioReleased = false;
    };

    //==========================================================================
    // [MT] Helfer
    [[nodiscard]] LooperClip* activeClipFor (int looperIndex, int trackIndex) const noexcept;
    void moveToGraveyard (LooperClip* clip);
    void updateEffectiveMutes() noexcept;
    void refreshPlayingFlag() noexcept;

    /** Kompletten Staged-Satz schreiben + Version bumpen [MT]. */
    static void stageClipParams (LooperClip& clip, double rate, double lengthBeats,
                                 double windowOffsetBeats, bool reversed,
                                 bool followPhase, bool atWrap, bool resetGrid) noexcept;

    /** Kanal chunked in den Clip-Buffer lesen (Export-Halte-Protokoll) [MT]. */
    static void readChannelChunked (const CaptureChannel* channel,
                                    std::uint64_t startPosition,
                                    float* dest, int numSamples);

    //==========================================================================
    // [Audio] Kommandos, Pending-Actions, Parameter-Anwendung
    void drainCommands() noexcept;
    void handleActivate (const ClipCommand& command) noexcept;
    void handleDelete (const ClipCommand& command) noexcept;
    [[nodiscard]] bool anyVoiceReferences (const LooperClip* clip) const noexcept;
    bool transferOrRetire (LooperClip* clip) noexcept;

    /** Pending-Action des Tracks am Grid ausführen (sample-genau). */
    void executePending (TrackPlayer& player, double blockStartBeat,
                         double beatStep, int numSamples) noexcept;

    /** Voice für einen (Re-)Start beschaffen (Crossfade/Steal) — Anker
        des Clips wird hier NICHT angefasst. */
    void launchVoice (TrackPlayer& player, LooperClip* clip, int startOffset) noexcept;

    /** Staged-Version des Clips anwenden (einmal pro Block, exakter
        Playhead, Splice-Duck bei Lese-Sprüngen). */
    void applyClipParams (LooperClip& clip, double blockStartBeat,
                          double beatStep, int numSamples) noexcept;

    /** Kandidaten-Parametersatz aus dem Staged-Satz berechnen —
        positions-kontinuierlich, wo möglich. */
    struct CandidateParams
    {
        double anchorBeat = 0.0, rate = 1.0, lengthBeats = 0.0, windowOffsetBeats = 0.0;
        bool reversed = false;
        std::uint32_t version = 0;
    };
    [[nodiscard]] static CandidateParams computeCandidate (const LooperClip& clip,
                                                           double playheadBeat) noexcept;

    [[nodiscard]] static float renderClipSample (const LooperClip& clip, int channel,
                                                 double contentPosition) noexcept;

    //==========================================================================
    // Audio-only: Player-Matrix + sample-kontinuierlicher Beat-Playhead
    std::array<std::array<TrackPlayer, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> players;

    double playheadBeat  = 0.0;
    bool   playheadValid = false;
    int    snapPendingCount = 0;
    bool   snapDucking = false;
    float  duckGain = 1.0f;
    std::uint64_t blockCounter = 0;

    // Looper-I/O-Busse (prealloziert in prepare, Audio-only): pro Track
    // Post-Fader-Stereo (zugleich Render-Ziel statt geteiltem Scratch),
    // pro Looper Pre-/Post-Fader-Stereo, Send-Busse + Master-Mix des
    // Blocks — renderBlock füllt, getAudioView()/mixToOutput lesen im
    // selben Callback
    std::array<std::array<std::array<std::vector<float>, 2>,
                          static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> trackBus;
    std::array<std::array<std::vector<float>, 2>,
               static_cast<std::size_t> (maxLoopers)> preBus, postBus;
    std::array<std::array<std::vector<float>, 2>,
               static_cast<std::size_t> (maxSends)> sendBus;
    std::array<std::vector<float>, 2> masterBus;
    int busCapacity = 0;       // Samples pro Bus-Kanal (prepare)
    int renderedSamples = 0;   // 0 = noch nichts gerendert (View liefert Stille)

    // MT → Audio / Audio → MT
    SpscQueue<ClipCommand> commands { 1024 };
    SpscQueue<LooperClip*> retired  { 1024 };

    std::atomic<int>  anchor { 0 };

    // „an Master senden" pro Looper (Default true) — MT schreibt, Audio liest
    std::array<std::atomic<bool>, static_cast<std::size_t> (maxLoopers)> looperToMaster;

    std::atomic<bool> stopAllRequested { false };
    std::atomic<bool> playingFlag { false };
    std::atomic<int>  committedBars { 0 };
    std::atomic<std::uint32_t> snapCount { 0 };
    std::atomic<std::int64_t>  ramBytesUsed { 0 };

    // Track-Mix-Ziele [MT schreibt, Audio slewt dorthin]
    std::array<std::array<std::atomic<float>, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> targetGain;
    std::array<std::array<std::atomic<float>, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> targetPan;
    std::array<std::array<std::atomic<bool>, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> effectiveMute;

    // Send-Level [MT schreibt, Audio slewt dorthin]: 0..1 pro Bus + Abgriff
    std::array<std::array<std::array<std::atomic<float>, static_cast<std::size_t> (maxSends)>,
                          static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> targetSendLevel;

    // Distanz [MT schreibt, Audio liest]: Ziel pro Track + Globals
    std::array<std::array<std::atomic<float>, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> targetDistance;
    std::atomic<float> distHiDumpDb { 9.0f };
    std::atomic<float> distHiCutHz { 8000.0f };
    std::atomic<float> distBaseFreqHz { 2000.0f };
    std::atomic<float> distWidth01 { 0.5f };
    std::atomic<bool>  distVolDumpOn { true };
    std::atomic<float> distVolDumpDb { 12.0f };
    std::atomic<float> distSmoothSeconds { 0.02f };
    std::array<std::array<std::atomic<bool>, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> sendTapPre;

    // Post-Fader-Meter pro Track
    std::array<std::array<LevelMeter, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> trackMeters;

    // MT-only: Mute/Solo-Rohzustand (effektives Mute wird daraus berechnet)
    std::array<std::array<bool, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> mtMute {};
    std::array<std::array<bool, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> mtSolo {};
    bool soloScopeGlobal = false;

    // MT-only: Clip-Besitz + Buchführung
    std::vector<std::unique_ptr<LooperClip>> store;
    std::vector<Grave> graveyard;
    std::array<std::array<LooperClip*, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> mtActiveClip {};
    std::array<std::array<bool, static_cast<std::size_t> (maxTracks)>,
               static_cast<std::size_t> (maxLoopers)> mtTrackPlaying {};
    std::uint32_t nextClipId = 0;
    std::int64_t ramBudgetBytes = defaultRamBudgetBytes;

    // MT-only (prepare)
    double preparedSampleRate = 0.0;
    int crossfadeSamples = 0;
    int maxLoopSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperBank)
};

} // namespace conduit
