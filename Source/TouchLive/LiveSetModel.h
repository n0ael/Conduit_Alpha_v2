#pragma once

#include <functional>

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
// ValueTree-Identifier des Live-Set-Spiegels (nur Laufzeit — NIE serialisiert)
namespace touchlive::id
{
    inline const juce::Identifier liveSet    { "LiveSet" };
    inline const juce::Identifier domain     { "Domain" };
    inline const juce::Identifier item       { "Item" };

    // Bewusst kollisionsfrei zu JSON-Feldnamen der Gegenseite gewählt
    // (Snapshots tragen Felder wie "name"/"color"/"index")
    inline const juce::Identifier domainName { "domainName" };
    inline const juce::Identifier itemKey    { "itemKey" };
} // namespace touchlive::id

//==============================================================================
/**
    ValueTree-Spiegel des Live-Sets (docs/TouchLive.md §3) — reiner
    Laufzeit-Zustand auf dem Message Thread.

    Struktur: LiveSet → Domain (domainName) → Objekt-Werte als Item-Kinder
    (itemKey = Stable-ID der Gegenseite, JSON-Felder als Properties),
    Skalar-/Array-Werte als Properties direkt am Domain-Tree (z. B. die
    Transport-Felder oder die "grid:{tid}"-Zeilen der Session-Domain).

    Diff-Semantik der Gegenseite (Tools/Live/ConduitRemote): geänderte
    Top-Level-Keys tragen den KOMPLETTEN neuen Wert (shallow compute_diff),
    entfernte Keys kommen als null. Ein Objekt-Wert ersetzt sein Item daher
    vollständig — Felder, die im Wert fehlen, werden entfernt.

    Reconnect wendet Snapshots als Tree-DIFF an (kein Clear+Rebuild):
    unveränderte Properties werden NICHT gesetzt, damit künftige UI-Listener
    nicht flackern. Achtung var-Falle: juce::var vergleicht DynamicObjects
    nur über Pointer — deshalb eigener Deep-Vergleich VOR jedem setProperty
    (frisch geparstes JSON wäre sonst immer "anders").

    KEIN UndoManager — Undo ist Lives Sache (/live/song/undo als Command).
    Das Modell wird NIE serialisiert (Laufzeit-ID-Regel, docs/DataModel.md).
*/
class LiveSetModel
{
public:
    /** Echo-Suppression-Prädikat (docs/TouchLive.md §5.1): true → Feld/Key
        unangetastet lassen. field ist leer für Skalar-/Array-Keys. */
    using SuppressionCheck = std::function<bool (const juce::String& domainName,
                                                 const juce::String& key,
                                                 const juce::String& field)>;

    LiveSetModel() = default;

    /** Root-Tree für UI-Listener (read/listen-only außerhalb des Modells). */
    [[nodiscard]] juce::ValueTree getState() noexcept { return state; }

    /** Domain-Subtree — legt ihn bei Bedarf an. [Message Thread] */
    [[nodiscard]] juce::ValueTree getDomain (const juce::String& domainName);

    /** Item einer Domain per Stable-ID, ungültig wenn unbekannt. */
    [[nodiscard]] juce::ValueTree findItem (const juce::String& domainName,
                                            const juce::String& key);

    /** Voll-Snapshot als Tree-Diff anwenden: verschwundene Keys entfernen,
        alle übrigen inkrementell aktualisieren. [Message Thread] */
    void applySnapshot (const juce::String& domainName, const juce::var& payload,
                        const SuppressionCheck& shouldSuppress = {});

    /** Diff anwenden: Key → Wert (komplett), Key → null → entfernen.
        [Message Thread] */
    void applyDiff (const juce::String& domainName, const juce::var& payload,
                    const SuppressionCheck& shouldSuppress = {});

    /** Optimistischer lokaler Edit eines Item-Feldes (Feel-Regel §5.1): schreibt
        den Wert SOFORT in den Spiegel, damit ANDERE lokale Controller (z. B. die
        AlphaTrack-Bridge und die Mixer-View gleichzeitig) ihn sehen, bevor Lives
        per Echo-Suppression verworfenes Echo eintrifft. No-op ohne Item; setzt
        nur bei echter Wertänderung (kein Flackern). [Message Thread] */
    void setItemField (const juce::String& domainName, const juce::String& key,
                       const juce::Identifier& field, const juce::var& value);

    /** Wie setItemField, aber ein einzelnes Array-Element (z. B. sends[i]) — baut
        das Array neu, damit der ValueTree-Listener feuert (var vergleicht Arrays
        über den Pointer). No-op bei fehlendem Item/Feld/Index. [Message Thread] */
    void setItemArrayElement (const juce::String& domainName, const juce::String& key,
                              const juce::Identifier& field, int index,
                              const juce::var& value);

private:
    void applyKeyValue (juce::ValueTree domainTree, const juce::String& domainName,
                        const juce::String& key, const juce::var& value,
                        const SuppressionCheck& shouldSuppress);

    juce::ValueTree state { touchlive::id::liveSet };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveSetModel)
};

} // namespace conduit
