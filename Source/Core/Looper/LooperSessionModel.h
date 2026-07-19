#pragma once

#include "LooperBank.h"

namespace conduit
{

//==============================================================================
/**
    Session-Modell des Looper-Vollausbaus (M4) — reiner Message-Thread-
    Zustand über der LooperBank: Slot-Grid (Looper × Track × Slot),
    Target-Slot und Aktiv-Clip pro Looper, Auto-Advance, Looper-/Track-
    Struktur. UI (M6) und OSC (M8) binden HIER an; die Bank bleibt die
    einzige Besitzerin der Clips (Freigabe nur über deleteClip-Protokoll).

    Bewusst EngineProcessor-frei (module-ready): Commit-Rohdaten
    (CaptureService, aufgelöste Quell-Indizes, BarSampleAnchors) reicht
    der Host pro Aufruf durch — das spätere LooperModule hostet Modell +
    Bank identisch.

    Semantik (Übergabe-Dokument 05.07.2026 + User-Entscheidungen):
      - Pro Looper höchstens EIN Target-Slot (leerer Slot, rot pulsierend);
        Tap auf leeren Slot armt/disarmt, TARGET-Kurzklick zykelt die
        Target-Spalte durch die Tracks (nächster freier Slot des Tracks).
      - Commit schreibt die letzten N Takte in den Target-Slot dieses
        Loopers (kein Target → Fehler mit Statushinweis), startet sofort
        phasenstarr, setzt den Aktiv-Clip und rückt das Target bei
        Auto-Advance auf den nächsten freien Slot UNTERHALB im selben
        Track (keiner frei → kein Target).
      - Ein spielender Clip pro Track (Session-Verhalten) — die Bank
        crossfadet beim Launch; das Modell führt den spielenden Slot.
      - Aktiv-Clip pro Looper: gesetzt durch Commit, normalen Tap
        (Start/Retrigger) oder TARGET-Halten+Tap (nur Auswahl).
      - Struktur: 1–4 Looper, 1–4 Tracks pro Looper. Entfernen nur am
        ENDE (letzter Looper / letzter Track) — Tracks sind positional
        an die Bank-Player gebunden, ein Mittel-Entfernen würde spielende
        Clips still auf fremde Player mappen (M4-Entscheidung; die
        Delete-Geste auf beliebige Header kann später nachziehen, wenn
        ein Track-Umzug in der Bank existiert). Letzten Track entfernen
        nur, wenn er leer und gestoppt ist; Looper-Entfernen verwirft
        dessen Clips (Bestätigungs-Dialog ist UI-Sache, looperHasClips).
*/
class LooperSessionModel
{
public:
    static constexpr int maxSlots = 12;

    explicit LooperSessionModel (LooperBank& bankToUse);

    //==========================================================================
    // Struktur [Message Thread]

    [[nodiscard]] int getNumLoopers() const noexcept { return numLoopers; }
    [[nodiscard]] int getNumTracks (int looperIndex) const noexcept;

    /** Neuen Looper öffnen (max. 4) — 1 Track, leere Slots. */
    bool addLooper() noexcept;

    /** Letzten Looper schließen (min. 1 bleibt) — verwirft dessen Clips.
        Bestätigung bei vorhandenen Clips ist UI-Sache (looperHasClips). */
    [[nodiscard]] juce::Result removeLastLooper();

    [[nodiscard]] bool looperHasClips (int looperIndex) const noexcept;

    [[nodiscard]] bool trackHasClips (int looperIndex, int trackIndex) const noexcept;

    bool addTrack (int looperIndex) noexcept;

    /** Letzten Track entfernen — nur wenn leer und gestoppt. */
    [[nodiscard]] juce::Result removeLastTrack (int looperIndex);

    //==========================================================================
    // Target [Message Thread]

    struct SlotAddress
    {
        int track = -1;
        int slot = -1;
        [[nodiscard]] bool isValid() const noexcept { return track >= 0 && slot >= 0; }
        [[nodiscard]] bool operator== (const SlotAddress& other) const noexcept
        {
            return track == other.track && slot == other.slot;
        }
    };

    [[nodiscard]] SlotAddress getTarget (int looperIndex) const noexcept;

    /** Leeren Slot als Target armen; dasselbe Target erneut = disarm.
        Belegte Slots werden ignoriert (Tap dort = Launch). */
    void armTarget (int looperIndex, int trackIndex, int slotIndex) noexcept;
    void disarmTarget (int looperIndex) noexcept;

    /** TARGET-Kurzklick: Target-Spalte zyklisch zum nächsten Track mit
        freiem Slot (Target = dessen erster freier Slot). */
    void cycleTargetTrack (int looperIndex) noexcept;

    void setAutoAdvance (bool enabled) noexcept { autoAdvance = enabled; }
    [[nodiscard]] bool getAutoAdvance() const noexcept { return autoAdvance; }

    //==========================================================================
    // Clips [Message Thread]

    [[nodiscard]] LooperClip* clipAt (int looperIndex, int trackIndex,
                                      int slotIndex) const noexcept;

    /** Commit der letzten `bars` Takte in den Target-Slot des Loopers.
        Belegtes Target (Auto-Advance aus) wird überschrieben (alter Clip
        wird gelöscht). Startet sofort phasenstarr, setzt den Aktiv-Clip. */
    [[nodiscard]] juce::Result commit (int looperIndex, int bars,
                                       const CaptureService& capture,
                                       int leftIndex, int rightIndex,
                                       const BarSampleAnchors& anchors);

    [[nodiscard]] juce::Result startSlot (int looperIndex, int trackIndex,
                                          int slotIndex, double qBeats);
    [[nodiscard]] juce::Result retriggerSlot (int looperIndex, int trackIndex,
                                              int slotIndex, double qBeats);
    void stopTrack (int looperIndex, int trackIndex, double qBeats) noexcept;

    /** Delete-Geste: Clip des Slots endgültig löschen. */
    [[nodiscard]] juce::Result deleteSlot (int looperIndex, int trackIndex,
                                           int slotIndex);

    /** Papierkorb (Big Out): Clip aus dem Slot LÖSEN ohne bank.deleteClip —
        die Bank bleibt Besitzerin, Restore via attachClip. nullptr = leer. */
    [[nodiscard]] LooperClip* detachSlot (int looperIndex, int trackIndex,
                                          int slotIndex) noexcept;

    /** Papierkorb-Restore: Clip in einen LEEREN Slot hängen (kein
        Auto-Play). false bei ungültiger Adresse oder belegtem Slot. */
    bool attachClip (int looperIndex, int trackIndex, int slotIndex,
                     LooperClip* clip) noexcept;

    /** Spielender Slot des Tracks (−1 = keiner) — Modell-Sicht (Intent). */
    [[nodiscard]] int getPlayingSlot (int looperIndex, int trackIndex) const noexcept;

    /** [Message Thread] Alle Clip-Referenzen verwerfen — PFLICHT nach
        LooperBank::prepare (SampleRate-Wechsel gibt die Clips frei; die
        Slot-Pointer wären sonst dangling — Feld-Fund 05.07.2026:
        Zombie-Zelle „Clip 7209 · 0.00ד nach Device-Restart). */
    void clearAllClips() noexcept;

    //==========================================================================
    // Aktiv-Clip (Ziel der Clip-Controls-Leiste) [Message Thread]

    void setActiveSlot (int looperIndex, int trackIndex, int slotIndex) noexcept;
    [[nodiscard]] SlotAddress getActiveSlot (int looperIndex) const noexcept;
    [[nodiscard]] LooperClip* getActiveClip (int looperIndex) const noexcept;

private:
    [[nodiscard]] bool validLooper (int l) const noexcept
    {
        return l >= 0 && l < numLoopers;
    }
    [[nodiscard]] bool validSlotAddress (int l, int t, int s) const noexcept;
    [[nodiscard]] int firstFreeSlot (int l, int t, int startSlot) const noexcept;
    void clearReferencesTo (int l, const SlotAddress& address) noexcept;

    LooperBank& bank;

    int numLoopers = 1;
    std::array<int, static_cast<std::size_t> (LooperBank::maxLoopers)> numTracks;

    std::array<std::array<std::array<LooperClip*, static_cast<std::size_t> (maxSlots)>,
                          static_cast<std::size_t> (LooperBank::maxTracks)>,
               static_cast<std::size_t> (LooperBank::maxLoopers)> slots {};

    std::array<SlotAddress, static_cast<std::size_t> (LooperBank::maxLoopers)> target;
    std::array<SlotAddress, static_cast<std::size_t> (LooperBank::maxLoopers)> active;
    std::array<std::array<int, static_cast<std::size_t> (LooperBank::maxTracks)>,
               static_cast<std::size_t> (LooperBank::maxLoopers)> playingSlot;

    bool autoAdvance = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperSessionModel)
};

} // namespace conduit
