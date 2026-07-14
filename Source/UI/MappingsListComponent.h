#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/MidiInBindings.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Mappings-Liste des Map-Tabs (MIDI-Rig M5b): eine Zeile pro
    MIDI-In-Bindung — Control-Name (controlNameFor, GridPage synthetisiert
    "Fader 3" etc.), Adresse inkl. Shift-Ebene (describeBinding), Learn-
    Kachel (Re-Learn der Zeile), „Shift"-Kachel (suppressWhileShift,
    nur bei Note-Bindungen) und ×-Kachel (Löschen).

    Die Liste liest NUR aus MidiInBindings (refresh() baut die Zeilen neu,
    getriggert über MidiInBindings::onBindingsChanged via GridPage) und
    meldet Aktionen über Callbacks — kein eigener Zustand außer den
    Zeilen-Components. 44-px-Zeilen (Touch-Regel). Message Thread.
*/
class MappingsListComponent final : public juce::Component
{
public:
    explicit MappingsListComponent (grid::MidiInBindings& bindingsToUse);
    ~MappingsListComponent() override;

    /** Anzeigename eines Control-Werts ("Fader 3 · Y", GridPage). */
    std::function<juce::String (const grid::MacroControlKey&)> controlNameFor;

    /** Learn-Kachel einer Zeile: Adresse dieser Bindung neu lernen. */
    std::function<void (const grid::MacroControlKey&)> onLearnRequested;

    /** Learn-scharfe Zeile markieren (orange Learn-Kachel); hasArmed=false
        löscht die Markierung. Baut die Zeilen neu. */
    void setArmedKey (bool hasArmedToUse, const grid::MacroControlKey& keyToUse);

    /** Zeilen aus bindings.all() neu aufbauen. */
    void refresh();

    /** Adresse + Shift-Ebene als Anzeigetext, z. B. "CC 20 · Ch 1 + C1, D#1"
        bzw. "Note C1 · Ch 10" — pure, testbar. */
    [[nodiscard]] static juce::String describeBinding (const grid::MidiInBindings::Binding& binding);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void visibilityChanged() override;

private:
    static constexpr int kRowHeight = 44;   // Touch-Target-Regel (CLAUDE.md 10)
    static constexpr int kRowGap    = 4;

    //==========================================================================
    struct Row final : public juce::Component
    {
        Row (MappingsListComponent& ownerToUse, grid::MacroControlKey keyToUse);

        void resized() override;
        void paint (juce::Graphics& g) override;

        MappingsListComponent& owner;
        grid::MacroControlKey key;

        juce::Label nameLabel;
        juce::Label addressLabel;
        push::TextTile learnTile { "Learn" };
        push::TextTile shiftTile { "Shift" };   // suppressWhileShift (nur Noten)
        push::TextTile deleteTile { juce::String::fromUTF8 ("\xc3\x97") };   // ×

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Row)
    };

    grid::MidiInBindings& bindings;

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::Viewport viewport;
    juce::Component rowHost;
    std::vector<std::unique_ptr<Row>> rows;

    bool hasArmed = false;
    grid::MacroControlKey armedKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingsListComponent)
};

} // namespace conduit
