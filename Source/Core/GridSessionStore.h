#pragma once

#include <functional>
#include <memory>

#include <juce_data_structures/juce_data_structures.h>

#include "CcControlModel.h"
#include "ChannelStripLayers.h"
#include "ChordMemory.h"
#include "GridVoiceEngine.h"
#include "MacroBindings.h"
#include "MidiInBindings.h"

namespace conduit::grid
{

//==============================================================================
/**
    Block K (Persistenz gebündelt, Masterplan): serialisiert den Grid-
    SESSION-Zustand — alles, was bisher Laufzeit-only war — als ValueTree
    in eine XML-Datei neben den GridPanel-Settings:

      - DIY-Controls (CcControlModel, MIT ihren Ids — die Bindings
        referenzieren sie; restore() erhält Id-Lücken nach remove())
      - Akkord-Slots (ChordMemory, 8 LCD-Slots)
      - MIDI-In-Bindungen (MidiInBindings: Key → Kanal/CC)
      - Macro-Bindings (MacroBindings: pro Key die Slot-Liste mit Kurve
        (Punkte + Krümmungen + OutputRange) und Ziel — Ziele reisen als
        OPAKES MacroTarget::toState()-Tree; den Rückweg baut die
        TargetFactory des Besitzers (GridPage kennt MidiCcTarget/
        AbletonParamTarget; Live-Ziele tragen nur die LiveParamSpec,
        Rule touchlive: dvid ist Laufzeit-ID, Re-Resolve über
        LiveTargetResolver))
      - MPE-Achsen-Zustand (Block B/C: ResponseCurve + offsetBeyondMax
        je Achse Pressure/Slide/PitchBend)

    KEIN Patch-Zustand (der lebt im Root-ValueTree/Preset) — dies ist
    App-Session-Zustand wie GridPanelSettings, nur strukturiert.
    UI-frei, headless testbar. Message Thread.
*/
class GridSessionStore
{
public:
    /** Baut ein Ziel aus seinem toState()-Tree (Typ am Tree-Namen erkennen);
        nullptr = unbekannter Typ, Slot bleibt ohne Ziel. */
    using TargetFactory = std::function<std::unique_ptr<MacroTarget> (const juce::ValueTree&)>;

    struct Refs
    {
        CcControlModel&  diyControls;
        ChordMemory&     chords;
        MidiInBindings&  midiIn;
        MacroBindings&   macros;
        GridVoiceEngine& engine;
        conduit::midirig::ChannelStripLayers& stripLayers;   // M7: aktive Ebene je Spalte
    };

    static constexpr int kStateVersion = 1;

    /** Aktuellen Zustand einfangen (nicht-const: MacroBindings::get). */
    [[nodiscard]] static juce::ValueTree capture (const Refs& refs);

    /** Session-Tree auf frische Objekte anwenden (Laden beim Start).
        Erwartet leere Modelle — vorhandene Einträge werden nicht geräumt
        (ChordMemory überschreibt belegte Slots ohnehin nie). */
    static void apply (const juce::ValueTree& session, const Refs& refs,
                       const TargetFactory& makeTarget);

    //==========================================================================
    // Datei-I/O (XML) — den Ablageort liefert GridPanelSettings::sessionFile()
    // (neben GridPanel.settings, folgt injizierten Test-Options).

    static bool saveToFile (const juce::File& file, const juce::ValueTree& session);
    [[nodiscard]] static juce::ValueTree loadFromFile (const juce::File& file);

    //==========================================================================
    // Kurven-Helfer (auch für Tests): schreibt/liest Punkte, Krümmungen
    // und OutputRange einer ResponseCurve.

    [[nodiscard]] static juce::ValueTree curveToState (const ResponseCurve& curve);
    static void curveFromState (const juce::ValueTree& state, ResponseCurve& curve);
};

} // namespace conduit::grid
