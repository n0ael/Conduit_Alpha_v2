#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_osc/juce_osc.h>

#include "TouchLive/LiveSetModel.h"

namespace conduit
{

//==============================================================================
/**
    Ableton-Track-Selector (Block H v2 rev5, Masterplan): klappt beim
    Long-Press auf den Grid-Page-Button als CallOutBox aus und listet alle
    MIDI-Tracks des Live-Sets mit Name + Live-Farbe (tracks-Domain,
    kind == "midi", Live-Reihenfolge); der aktuelle Conduit-Fokus-Track
    (Domain-Key `conduit_focus`) ist gefüllt markiert.

    Tap auf einen Track meldet onTrackChosen (Stable-ID) — der EngineEditor
    sendet /live/song/set/midi_input_focus [trackKey, gridInput,
    masterInput]: der Ziel-Track bekommt Monitor „In" + den Grid-MPE-Port
    als Input, alle anderen All-Ins-MIDI-Tracks wandern STATISCH auf das
    Master-MIDI-Device (Monitor bleibt unangetastet) — Lives eigene Arm-/
    Selektions-Mechanik übernimmt den Rest (User-Entscheidung 11.07.2026
    abends; das frühere Selektions-Following ist entfallen). Zweck:
    Conduit-Grid und Push/Keyboard spielen GLEICHZEITIG verschiedene Tracks.

    Die Zeilen sind ein Snapshot beim Öffnen (die CallOutBox lebt nur einen
    Moment). Stable-IDs sind Laufzeit-IDs der Gegenseite und werden NIE
    serialisiert (CLAUDE.md §6). Message Thread.
*/
class TrackSelectorPanel final : public juce::Component
{
public:
    struct TrackRow
    {
        juce::String key;      // Stable-ID der Gegenseite ("tr:…")
        juce::String name;
        juce::Colour colour;   // Live-Farbe 0x00RRGGBB
        int index = 0;         // Live-Reihenfolge
    };

    explicit TrackSelectorPanel (LiveSetModel& model);

    /** Track angetippt (Stable-ID) — der Besitzer sendet das Command und
        die Box schließt sich selbst. */
    std::function<void (const juce::String& stableKey)> onTrackChosen;

    //==========================================================================
    // Headless-Kernpfade (Catch2)

    /** MIDI-Tracks (kind == "midi") der tracks-Domain, nach index sortiert. */
    [[nodiscard]] static std::vector<TrackRow> midiTrackRowsFrom (LiveSetModel& model);

    /** Stable-ID des aktuellen Conduit-Fokus-Tracks (Domain-Key
        `conduit_focus`, vom Remote Script verwaltet), leer wenn keiner. */
    [[nodiscard]] static juce::String focusKeyFrom (LiveSetModel& model);

    /** /live/song/set/midi_input_focus [trackKey, gridInput, masterInput,
        favourites] — Block-H-v2-rev5-Wire-Format (statische Aufteilung).
        favourites = „;"-Liste der Master-Favoriten: das Script behandelt
        Tracks auf JEDEM dieser Ports als verwaltet (Quick-Switch-Wanderung
        funktioniert damit auch über Live-Neustarts hinweg — Feldtest
        11.07.2026: Session-Set allein kannte historische Master nicht). */
    [[nodiscard]] static juce::OSCMessage
        makeMidiInputFocusCommand (const juce::String& trackKey,
                                   const juce::String& gridInputName,
                                   const juce::String& masterInputName,
                                   const juce::String& favouritesJoined);

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

    static constexpr int kRowHeight    = 44;   // Touch-Zone (CLAUDE.md 10.0)
    static constexpr int kTitleHeight  = 30;
    static constexpr int kPanelWidth   = 280;

private:
    [[nodiscard]] int rowIndexAt (juce::Point<int> position) const noexcept;

    std::vector<TrackRow> rows;
    juce::String focusKey;
    int hoveredRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackSelectorPanel)
};

} // namespace conduit
