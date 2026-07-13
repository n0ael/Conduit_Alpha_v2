#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AnimatedValue.h"
#include "Browser/TouchKeyboard.h"
#include "Core/MidiTargetBrowserModel.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Drill-down-Picker fuer MIDI-Hardware-Ziele (MIDI-Rig M3, ADR 006 --
    Analogie Ableton-Parameter-Browser Track -> Device -> Parameter).
    CallOutBox-Inhalt (Muster `TrackSelectorPanel`): feuert `onTargetChosen`
    bei Tap auf eine Parameter-Zeile und schliesst danach SELBST die
    umschliessende CallOutBox (`findParentComponentOfClass<CallOutBox>()`,
    genau wie `TrackSelectorPanel::mouseUp`) -- der Aufrufer baut daraus nur
    noch das MacroTarget, ohne die CallOutBox selbst verwalten zu muessen.

    Aufbau (oben -> unten, Muster `Browser/BrowserPanel`): Breadcrumb-
    Kopfzeile (Zurueck-Chevron + Pfad, 44 px) -- Zeilenliste (custom-paint,
    in einem `juce::Viewport` scrollbar, KEINE ListBox-Virtualisierung
    noetig bei den hier realistischen Zeilenzahlen) -- Suchfeld (durchsucht
    rekursiv alle Parameter unterhalb der aktuellen Ebene) -- TouchKeyboard,
    klappt beim Fokussieren des Suchfelds von unten auf (Fokus-Falle
    beachtet: Tasten greifen nie den Fokus). Gesamthoehe ist FIX (die
    Tastatur verkleinert nur den Listenbereich) -- KEIN Resize der
    CallOutBox waehrend der Navigation noetig.
*/
class HardwareTargetPicker final : public juce::Component,
                                   private juce::FocusChangeListener
{
public:
    explicit HardwareTargetPicker (MidiTargetBrowserModel modelToUse);
    ~HardwareTargetPicker() override;

    /** Tap auf eine Parameter-Zeile -- der Aufrufer baut daraus das
        MacroTarget; der Picker schliesst danach selbst seine CallOutBox
        (Muster TrackSelectorPanel::mouseUp). */
    std::function<void (const MidiTargetBrowserModel::Row& selected)> onTargetChosen;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int kPanelWidth   = 320;
    static constexpr int kPanelHeight  = 480;
    static constexpr int kHeaderHeight = 44;
    static constexpr int kSearchHeight = 44;
    static constexpr int kRowHeight    = 44;   // Touch-Zone (CLAUDE.md 10.0)

private:
    //==========================================================================
    class RowListContent final : public juce::Component
    {
    public:
        explicit RowListContent (HardwareTargetPicker& ownerToUse);

        void setRows (std::vector<MidiTargetBrowserModel::Row> newRows);

        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& event) override;

    private:
        [[nodiscard]] int rowIndexAt (juce::Point<int> position) const noexcept;

        HardwareTargetPicker& owner;
        std::vector<MidiTargetBrowserModel::Row> rows;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RowListContent)
    };

    void refresh();
    void chooseRow (int rowIndex);
    void setKeyboardVisible (bool shouldShow, bool animate = true);

    // juce::FocusChangeListener -- klappt die Tastatur auf/zu
    void globalFocusChanged (juce::Component* focusedComponent) override;

    MidiTargetBrowserModel model;

    push::IconTile backTile { push::Icon::chevronLeft, "hwPickerBack" };
    juce::String breadcrumb;

    juce::Viewport viewport;
    RowListContent listContent { *this };

    juce::TextEditor searchField;
    TouchKeyboard keyboard;
    AnimatedValue keyboardSlide { *this };
    bool keyboardVisible = false;

    static constexpr int kKeyboardAnimMs = 180;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareTargetPicker)
};

} // namespace conduit
