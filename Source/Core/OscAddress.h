#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "Modules/ConduitModule.h"

namespace conduit::osc
{

//==============================================================================
// Zentraler OSC-Adressbau (CLAUDE.md 7) — eine Quelle für Receive-Registry
// (OscController::rebuildEndpoints) und Send-Pfad (OscSendService), damit
// beide Seiten garantiert dieselben Adressen sprechen.

/** Voll-Dump-Anfrage eines Clients (7.3). */
inline constexpr const char* syncAddress = "/conduit/sync";

/** Announce eines Max4Live-Devices (7.4):
    s:remoteId s:factoryKey s:trackName i:trackColour */
inline constexpr const char* announceAddress = "/conduit/announce";

/** Kanonische Parameter-Adresse eines Tree-Nodes:
    /conduit/{type}/{moduleId}/{paramId} — Schema 7. */
[[nodiscard]] inline juce::String parameterAddress (const juce::ValueTree& nodeTree,
                                                    const juce::String& parameterId)
{
    return "/conduit/"
           + nodeTree.getProperty (id::type).toString().toLowerCase()
           + "/" + nodeTree.getProperty (id::moduleId).toString()
           + "/" + parameterId;
}

/** Receive-Alias für announce-gebundene Nodes (7.4):
    /conduit/remote/{remoteId}/{paramId} — unabhängig von moduleId-Renames
    und Kollisions-Suffixen; der Send-Pfad bleibt kanonisch. */
[[nodiscard]] inline juce::String remoteAliasAddress (const juce::String& remoteId,
                                                      const juce::String& parameterId)
{
    return "/conduit/remote/" + remoteId + "/" + parameterId;
}

//==============================================================================
/** Validierte Nutzlast eines /conduit/announce (7.4) — vom Netzwerk-Thread
    geparst, auf dem Message Thread konsumiert (RemoteModuleBinder). */
struct AnnounceInfo
{
    juce::String remoteId;    // beidseitig persistent (Live-Set + Patch)
    juce::String factoryKey;  // muss in der ModuleFactory registriert sein
    juce::String trackName;   // Wunsch-Name (wird saniert, nur Erst-Anlage)
    int tintArgb = 0;         // Track-Farbe (0x00RRGGBB aus der Live API)
};

/** remoteIds werden Teil von OSC-Adressen — deshalb hartes Zeichen-Limit
    ([A-Za-z0-9_-], max. 64) statt Sanitizing: ein umgeschriebenes remoteId
    fände sein Gegenstück im Live-Set nie wieder. */
[[nodiscard]] inline bool isValidRemoteId (const juce::String& remoteId)
{
    if (remoteId.isEmpty() || remoteId.length() > 64)
        return false;

    return remoteId.containsOnly (
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");
}

//==============================================================================
/** Looper-Aktions-Adressen (M8) — Push-Pads/Fußschalter steuern Commit,
    Stop und Target der Looper-Page („für Live das mit Abstand wertvollste",
    User 05.07.2026). Indizes in der ADRESSE sind 1-basiert (Musiker-Sicht),
    im Struct 0-basiert. Muster /conduit/sync: Erkennung VOR dem
    Endpoint-Lookup [Netzwerk-Thread], Ausführung via AsyncUpdater [MT].

      /conduit/looper/stop                       — alle Tracks stoppen
      /conduit/looper/{1-4}/commit   i:bars      — letzte N Takte → Target-Slot
      /conduit/looper/{1-4}/stop                 — alle Tracks des Loopers
      /conduit/looper/{1-4}/track/{1-4}/stop     — einen Track stoppen
      /conduit/looper/{1-4}/target   i:track i:slot — Target-Slot armen
*/
inline constexpr const char* looperAddressPrefix = "/conduit/looper";

struct LooperOscAction
{
    enum class Type { none = 0, commit, stopLooper, stopTrack, stopAll, target };

    Type type = Type::none;
    int looperIndex = -1;   // 0-basiert
    int trackIndex = -1;
    int slotIndex = -1;
    int bars = 0;
};

/** Adress-Teil des Parsers (pur, testbar) — Argumente (bars/track/slot)
    füllt der Controller aus der OSCMessage nach. Unbekanntes → Type::none. */
[[nodiscard]] inline LooperOscAction parseLooperActionAddress (const juce::String& address)
{
    LooperOscAction action;

    if (! address.startsWith (looperAddressPrefix))
        return action;

    juce::StringArray parts;
    parts.addTokens (address, "/", {});
    parts.removeEmptyStrings();
    // parts: conduit, looper, ...

    if (parts.size() == 3 && parts[2] == "stop")
    {
        action.type = LooperOscAction::Type::stopAll;
        return action;
    }

    if (parts.size() < 4)
        return action;

    const auto looperNumber = parts[2].getIntValue();
    if (looperNumber < 1 || looperNumber > 4
        || parts[2] != juce::String (looperNumber))
        return action;

    action.looperIndex = looperNumber - 1;

    if (parts.size() == 4 && parts[3] == "commit")
        action.type = LooperOscAction::Type::commit;
    else if (parts.size() == 4 && parts[3] == "stop")
        action.type = LooperOscAction::Type::stopLooper;
    else if (parts.size() == 4 && parts[3] == "target")
        action.type = LooperOscAction::Type::target;
    else if (parts.size() == 6 && parts[3] == "track" && parts[5] == "stop")
    {
        const auto trackNumber = parts[4].getIntValue();
        if (trackNumber >= 1 && trackNumber <= 4
            && parts[4] == juce::String (trackNumber))
        {
            action.type = LooperOscAction::Type::stopTrack;
            action.trackIndex = trackNumber - 1;
        }
    }

    return action;
}

} // namespace conduit::osc
