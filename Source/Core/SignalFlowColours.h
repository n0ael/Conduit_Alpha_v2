#pragma once

#include <map>
#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

class ChannelNames;

//==============================================================================
/**
    Signal-Flow-Farbvererbung (Kabelfarbe-Initiative, herausgelöst aus dem
    NodeCanvas 19.07.2026): Module OHNE eigene Farbe erben die (gemischte)
    Farbe ihrer Eingänge und geben sie weiter — explizite nodeColour gewinnt
    IMMER, Feedback-Schleifen werden per visiting-Set abgefangen. audio_in
    ist reine Quelle (Farbe pro Kanal aus den ChannelNames, Paar-Partner
    liefern die Farbe des Anker-Kanals).

    Rein abgeleitet, kein Patch-Zustand; read-only auf dem Root-Tree →
    headless testbar [Message Thread]. Farben als 0x00RRGGBB, 0 = keine.

    Konsumenten: NodeCanvas (Kabel/Header-Punkte/Port-Striche, hält die
    ColourMap als Frame-Cache) und die Looper-Quellauswahl (Slot-Farbe =
    Farbe der an den Looper-In-Slot verkabelten Quelle — die Kette
    Eingang → FX → Slot → Waveform → Clip bleibt so durchgängig).
*/
namespace flow_colours
{
    using ColourMap = std::map<juce::String, juce::uint32>;

    /** Effektive Farbe ALLER Nodes (explizit → sonst gemischte Eingänge). */
    [[nodiscard]] ColourMap computeAll (const juce::ValueTree& rootTree,
                                        const ChannelNames* channelNames);

    /** Kabel-/Punktfarbe eines Quell-Kanals aus der fertigen Map:
        audio_in → Kanalfarbe, sonst effektive Farbe des Quellmoduls. */
    [[nodiscard]] juce::uint32 lookupSource (const juce::ValueTree& rootTree,
                                             const ColourMap& colours,
                                             const ChannelNames* channelNames,
                                             const juce::String& sourceUuid,
                                             int sourceChannel);

    /** audio_in-Kanalfarbe (ChannelNames), aufgelöst auf den Paar-Anker. */
    [[nodiscard]] juce::uint32 inputChannelRgb (const ChannelNames* channelNames,
                                                int channel);

    /** Quellfarbe des Kabels an einem Ziel-Eingang (destNodeUuid +
        destChannel) — für die Looper-Combo: rechnet die Vererbung frisch
        (kein Cache nötig, Combo-Rebuilds sind selten). 0 = unverkabelt
        oder farblos. */
    [[nodiscard]] juce::uint32 resolveDestSourceRgb (const juce::ValueTree& rootTree,
                                                     const ChannelNames* channelNames,
                                                     const juce::String& destNodeUuid,
                                                     int destChannel);

    /** Mittelt die Farben (0x00RRGGBB) komponentenweise; leer → 0. */
    [[nodiscard]] juce::uint32 blendRgb (const std::vector<juce::uint32>& colours);

} // namespace flow_colours

} // namespace conduit
