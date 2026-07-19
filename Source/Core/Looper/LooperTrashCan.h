#pragma once

#include <functional>
#include <vector>

#include "LooperBank.h"
#include "Modules/LooperBigOutModule.h"

namespace conduit
{

//==============================================================================
/**
    Papierkorb für erzwungene Looper-/Track-Löschungen (Big Out 07/2026) —
    MT-only. Ein erzwungenes Löschen (OK im Bestätigungs-Dialog) DETACHT
    die Clips nur aus dem SessionModel (kein bank.deleteClip — die Bank
    bleibt Besitzerin, ramBytesUsed zählt sie bewusst weiter) und parkt
    sie hier zusammen mit den Spec-relativen Kabel-Referenzen. Innerhalb
    des Zeitfensters (expirySeconds) stellt restore alles wieder her;
    danach löscht der Timer-Tick endgültig über das Audio-Quittungs-
    Protokoll der Bank.

    NICHT der UndoManager: Clips leben engine-seitig außerhalb des
    Undo-Trees, die Looper-Struktur in den LooperSettings (App-Zustand).

    Lebensdauer: im EngineProcessor NACH der looperBank deklarieren
    (wird zuerst zerstört); der Dtor ist ein No-op — den Speicher besitzt
    die Bank. prepareToPlay ruft clearWithoutDelete() VOR bank.prepare()
    (prepare verwirft den Store — die Pointer hier wären sonst dangling;
    kein Double-Free, da kein deleteClip aussteht).
*/
class LooperTrashCan final : private juce::Timer
{
public:
    static constexpr double expirySeconds = 180.0;
    static constexpr double warnSeconds   = 30.0;   // Rot-Fade-Fenster der Kachel

    explicit LooperTrashCan (LooperBank& bankToUse);
    ~LooperTrashCan() override;

    struct ClipRef
    {
        int track = 0;
        int slot = 0;
        LooperClip* clip = nullptr;
        std::uint32_t clipId = 0;
    };

    struct Entry
    {
        enum class Kind : int { track = 0, looper };

        Kind kind = Kind::track;
        int looperIndex = 0;
        int trackIndex = 0;          // bei kind == track
        int numTracksSnapshot = 1;   // bei kind == looper (VOR removeLastLooper —
                                     // das resettet numTracks auf 1)
        std::vector<ClipRef> clips;
        std::vector<LooperBigOutModule::BigOutCableRef> cables;
        double expiresAt = 0.0;      // Sekunden (Time::getMillisecondCounterHiRes/1000)
    };

    /** [MT] Eintrag parken; expiresAt setzt push selbst (now + expiry). */
    void push (Entry entry);

    [[nodiscard]] bool hasEntries() const noexcept { return ! entries.empty(); }

    /** [MT] Jüngsten Eintrag entnehmen (Restore) — hasEntries() vorher! */
    [[nodiscard]] Entry popLatest();

    /** Restzeit des am FRÜHESTEN ablaufenden Eintrags (0 = leer). */
    [[nodiscard]] double secondsRemaining() const noexcept;

    /** [MT, prepareToPlay VOR bank.prepare] Einträge verwerfen OHNE
        deleteClip — bank.prepare gibt den Store selbst frei. */
    void clearWithoutDelete() noexcept;

    /** Test-Seam: alle Einträge sofort endgültig löschen (Timer-Pfad). */
    void expireNow();

    /** [MT] Feuert bei jeder Bestandsänderung (Push/Pop/Expiry) — UI-Kachel. */
    std::function<void()> onChanged;

    /** [MT] Feuert, wenn ein Eintrag durch ABLAUF endgültig gelöscht
        wurde (Kachel-Flackern vor dem Verschwinden). */
    std::function<void()> onExpired;

private:
    void timerCallback() override;
    void expireDue (double nowSeconds);

    /** deleteClip je Clip (Result-geprüft — Queue voll → ClipRef bleibt
        für den nächsten Tick). true = Eintrag komplett geleert. */
    bool deleteEntryClips (Entry& entry);

    LooperBank& bank;
    std::vector<Entry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperTrashCan)
};

} // namespace conduit
