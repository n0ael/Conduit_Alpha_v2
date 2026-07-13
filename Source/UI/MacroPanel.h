#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/HardwareCcDatabase.h"
#include "Core/MacroBindings.h"
#include "Core/MidiInBindings.h"
#include "Core/MidiProfileLibrary.h"
#include "Core/MidiTargetBrowserModel.h"
#include "Interfaces/IMidiOutputTarget.h"
#include "CurveEditorTile.h"
#include "HardwareTargetPicker.h"
#include "NumberFieldBracket.h"
#include "PushTiles.h"
#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"

namespace conduit
{

//==============================================================================
/**
    Tab-Inhalt „Macro" des EditorDockPanels der Grid-Page (Block E,
    Masterplan): zeigt die Ziel-Liste des zuletzt lang gehaltenen Controls
    (System-XY/Fader oder DIY-Control, showControl). Bis zu 16 Ziele pro
    Control-Wert; Standard 1 Slot, „+" blendet weitere ein; die Liste
    scrollt vertikal (Viewport). Compact-View: die GEWÄHLTE Ziel-Zeile
    zeigt den grossen Mini-Kurveneditor (CurveEditorTile, Block-C-Optik
    ohne Offset), alle anderen sind auf eine Linie mit Punkt (aktuell
    gesendeter Wert) und Min/Max-Strichen zusammengeklappt -- Tap klappt um.

    Ziel-Typen gemischt (Masterplan final): MIDI (Kanal + CC ueber den
    Grid-MIDI-Ausgang, MidiCcTarget) und Ableton-Parameter direkt
    (AbletonParamTarget ueber TouchLive -- Parameter-Browser als
    Track→Device→Parameter-Dropdowns aus dem LiveSetModel statt
    MIDI-Learn). XY-Controls haben zwei Achsen (X/Y-Umschalter oben).

    Die Wert-Anzeige (Punkte) folgt MacroBindings::lastInput/lastOutput
    per VBlank-Poll (Muster MpeShapingView). Laufzeit-only -- Persistenz
    der Mappings kommt gebuendelt in Block K (Ableton-Ziele brauchen dort
    Namens-Re-Resolve, dvid ist Laufzeit-ID). Message Thread.
*/
class MacroPanel final : public juce::Component
{
public:
    MacroPanel (grid::MacroBindings& bindingsToUse, grid::IMidiOutputTarget& midiTargetToUse,
                LiveSetModel& liveSetModelToUse, TouchLiveClient& touchLiveClientToUse,
                grid::MidiInBindings& midiInBindingsToUse,
                grid::HardwareCcDatabase& hardwareDbToUse,
                MidiProfileLibrary& profileLibraryToUse);
    ~MacroPanel() override;

    /** Long-Press-Ziel setzen: layer/controlId (Achse 0); hasYAxis = true
        bei XY-Controls (X/Y-Umschalter erscheint). */
    void showControl (int layer, int controlId, const juce::String& title, bool hasYAxis);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    /** Eine Ziel-Zeile: eingeklappt = Linie + Punkt + Min/Max-Striche,
        ausgeklappt = Typ-Kacheln + Konfiguration + Kurveneditor. */
    class TargetRow final : public juce::Component
    {
    public:
        TargetRow (MacroPanel& ownerToUse, int indexToUse);

        void setExpanded (bool shouldBeExpanded);
        [[nodiscard]] bool isExpanded() const noexcept { return expanded; }
        [[nodiscard]] int preferredHeight() const noexcept;

        void refreshLiveValues();
        void rebuildFromBinding();

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent& event) override;

        static constexpr int kCollapsedHeight = 30;
        static constexpr int kExpandedHeight  = 236;

    private:
        enum class TargetType { none, midi, live, hardware };

        [[nodiscard]] grid::MacroBinding* binding() const noexcept;
        void applyTargetType (TargetType newType);
        void rebuildMidiTarget();
        void populateTrackCombo();
        void populateDeviceCombo();
        void populateParameterCombo();
        void createAbletonTarget();

        /** MIDI-Rig M3: der semantische Picker (HardwareTargetPicker, ueber
            CallOutBox) ersetzt die frueheren hwDeviceCombo/hwParamCombo-
            Dropdowns. `selection` kommt aus dem Picker-Callback -- CC-
            Zeilen bauen weiter einen normalen MidiCcTarget, NRPN-Zeilen
            (nur CSV-Profile) einen MidiNrpnTarget mit min/max aus dem
            Profil. `hwSelection` wird gemerkt, damit ein reiner Kanal-
            wechsel dieselbe Auswahl mit neuem Kanal neu baut. */
        void openHardwareTargetPicker();
        void createHardwareTarget (const MidiTargetBrowserModel::Row& selection);
        void rebuildHardwareTargetForChannelChange();
        void updateHwSummaryText();

        /** Block K2: die persistierte Live-Zuweisung (LiveParamSpec) in den
            drei Combos spiegeln — ohne createAbletonTarget auszulösen
            (dontSendNotification). Ohne Live-Verbindung bleiben die Combos
            leer, die Zuweisung selbst bleibt erhalten (Re-Resolve). */
        void applySpecToCombos (const grid::LiveParamSpec& spec);

        MacroPanel& owner;
        const int index;
        bool expanded = false;
        TargetType targetType = TargetType::none;

        push::TextTile midiTile     { "MIDI" };
        push::TextTile liveTile     { "Live" };
        push::TextTile hardwareTile { "HW" };
        push::TextTile removeTile { juce::String::fromUTF8 ("\xc3\x97") };   // ×

        NumberFieldBracket channelField { NumberFieldBracket::Config { 1.0, 16.0, 1.0, 1.0, 0, 0.1, "Ch" } };
        NumberFieldBracket ccField      { NumberFieldBracket::Config { 0.0, 127.0, 74.0, 1.0, 0, 0.5, "CC" } };

        juce::ComboBox trackCombo, deviceCombo, parameterCombo;
        juce::StringArray trackKeys, deviceIds;   // parallel zu den Combo-Ids

        // MIDI-Rig M3: Zusammenfassungs-Kachel statt zweier ComboBoxen --
        // Tap oeffnet den HardwareTargetPicker (CallOutBox). hwSelection
        // ist die zuletzt vom Picker gelieferte Auswahl (fuer Kanalwechsel
        // ohne erneutes Durchklicken); hasHwSelection unterscheidet "noch
        // nie ueber den Picker gewaehlt" (z. B. frisch aus einer Session
        // geladen) von einer echten leeren Auswahl.
        push::TextTile hwSummaryTile { juce::String::fromUTF8 ("Ger\xc3\xa4t w\xc3\xa4hlen\xe2\x80\xa6"),
                                       push::colours::ledWhite, true };
        MidiTargetBrowserModel::Row hwSelection;
        bool hasHwSelection = false;

        std::unique_ptr<CurveEditorTile> curveTile;   // gebaut in rebuildFromBinding (Kurve lebt im Binding)

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TargetRow)
    };

    [[nodiscard]] grid::MacroControlKey currentKey() const noexcept;
    void rebuildRows();
    void layoutRows();
    void selectRow (int index);
    void addTargetSlot();
    void removeTargetSlot (int index);
    void tick();

    /** MIDI-In-Zeile (Block G) mit der Bindung des aktuellen Keys
        synchronisieren bzw. Feld-Aenderungen committen. */
    void refreshMidiInRow();
    void commitMidiInBinding();

    grid::MacroBindings& macroBindings;
    grid::IMidiOutputTarget& midiTarget;
    LiveSetModel& liveSetModel;
    TouchLiveClient& touchLiveClient;
    grid::MidiInBindings& midiInBindings;
    grid::HardwareCcDatabase& hardwareDb;   // Block L2 (Klartext-Schnellpfad)
    MidiProfileLibrary& profileLibrary;     // M2: midi.guide-CSV-Profile

    int  currentLayer = 0;
    int  currentControlId = -1;   // -1 = kein Control gewaehlt (Leerzustand)
    int  currentAxis = 0;
    bool currentHasYAxis = false;
    juce::String currentTitle;
    int  selectedRow = 0;

    juce::Label titleLabel;
    push::TextTile axisXTile { "X" };
    push::TextTile axisYTile { "Y" };
    push::TextTile addTile   { "+ Ziel" };

    // MIDI-In-Zeile (Block G): externer CC bewegt dieses Control (Soft-
    // Takeover). Toggle an/aus + Learn (naechster CC bindet) + Kanal/CC.
    push::TextTile midiInTile { "MIDI In" };
    push::TextTile learnTile  { "Learn", push::colours::ledOrange };
    NumberFieldBracket midiInChannelField { NumberFieldBracket::Config { 1.0, 16.0, 1.0, 1.0, 0, 0.1, "Ch" } };
    NumberFieldBracket midiInCcField      { NumberFieldBracket::Config { 0.0, 127.0, 20.0, 1.0, 0, 0.5, "CC" } };

    juce::Viewport viewport;
    juce::Component rowHost;
    std::vector<std::unique_ptr<TargetRow>> rows;

    juce::VBlankAttachment vblank { this, [this] (double) { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroPanel)
};

} // namespace conduit
