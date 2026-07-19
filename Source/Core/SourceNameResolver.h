#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

class ChannelNames;

//==============================================================================
/**
    Leitet den Anzeige-Namen der Quelle ab, die an einen bestimmten Eingang
    (destNodeUuid + destChannel) eines Nodes angeschlossen ist — für das
    Auto-Naming der Multi-Input Link Audio Send-Kanäle (CLAUDE.md 7.2 Schritt 3).

    Rein funktional (read-only auf dem Root-Tree, kein Modul-Zugriff) → ohne
    Link/Audio/Device unit-testbar. Message Thread (ValueTree-Lesen).

    Auflösung:
      - keine Verbindung an diesem Eingang         → leerer String
      - Quelle ist audio_input-Endpunkt            → ChannelNames-Label des
                                                      Quell-Kanals (Fallback
                                                      "In N", wenn channelNames
                                                      nullptr)
      - sonst (reguläres Modul)                    → moduleId der Quelle,
                                                      bei Multi-Output-Quellen
                                                      mit Kanal-Suffix ":{n}"
*/
[[nodiscard]] juce::String resolveSourceLabel (const juce::ValueTree& rootTree,
                                               const juce::String& destNodeUuid,
                                               int destChannel,
                                               const ChannelNames* channelNames);

/** Signalketten-Label für die Looper-In-Slots (User-Regel 19.07.2026):
    verfolgt die Kette vom Ziel-Eingang RÜCKWÄRTS bis zur Wurzel und
    listet die Stationen in Signalrichtung — Klangquelle zuerst, dann die
    FX-Module: "mopho · galactic_1 · verbtiny_1". Wurzel = audio_input
    (ChannelNames-Label des Kanals) oder ein Modul ohne verkabelten
    Eingang (moduleId). Multi-Input-Stationen folgen ihrem ersten
    verbundenen Eingang; Zyklen brechen ab. Leer, wenn am Ziel-Eingang
    kein Kabel hängt. */
[[nodiscard]] juce::String resolveSourceChainLabel (const juce::ValueTree& rootTree,
                                                    const juce::String& destNodeUuid,
                                                    int destChannel,
                                                    const ChannelNames* channelNames);

} // namespace conduit
