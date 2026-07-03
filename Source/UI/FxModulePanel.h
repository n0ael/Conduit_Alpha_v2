#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Core/LinkSendTaps.h"
#include "Modules/ChassisSchema.h"
#include "UI/CurvedSlider.h"
#include "UI/GainFaderMeter.h"
#include "UI/PortComponent.h"

namespace conduit
{

//==============================================================================
/**
    Pflicht-Oberfläche aller Processor-Nodes (FX-Chassis, CLAUDE.md 4.6) —
    ersetzt das generische ParameterPanel für type == "Processor":

      [IN-Fader+Meter] | P1 | P2 | … | Pn | [OUT-Fader+Meter]

    Links der input_gain-Kanalzug, rechts output_gain (GainFaderMeter),
    dazwischen pro sichtbarem DSP-Parameter (role == "dsp") eine vertikale
    Fader-Spalte: Titel oben, langer Fader, darunter der Attenuverter-Knob
    ({param}_cv_amt, bipolar, MI-Stil) und der CV-Eingangs-Port des
    Parameters (Kanal = firstCvChannel + Spaltenindex, festes Layout 4.6).

    Fader↔Button-Modus (4.6, Patch-Properties uiMode/uiButtons): eine Spalte
    kann statt des Faders benannte Wert-Buttons zeigen (vertikale Stapel à 5,
    max. 10 — die Spalte verbreitert sich ab dem 6.). Im Dev-Modus sind Fader
    UND Buttons sichtbar: der Fader findet den Wert, ein Button-Klick
    speichert ihn (undo-fähig), +/− bestimmen die Anzahl, Doppelklick benennt
    um. Im Normalmodus ruft der Klick den Wert über den Fader-Pfad ab.

    Die CV-Ports sind normale PortComponents — Kabel-Gesten laufen über den
    NodeCanvas (findParentComponentOfClass), die Anker-Delegation übernimmt
    NodeComponent::getPortCentre → cvPortCentre().

    Bindung (5.3): schreibt nur paramValue ohne UndoManager (Muster
    ParameterPanel); externe Änderungen kommen über den ValueTree-Listener.
*/
class FxModulePanel final : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::Timer
{
public:
    FxModulePanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse);
    ~FxModulePanel() override;

    /** Teardown-Hook (Phase 1, 5.3): Interaktion + Meter sofort stoppen. */
    void stopUpdates();

    //==========================================================================
    /** Dev-Modus (4.6, Toggle im Node-Header — transient, kein Patch-/App-
        Zustand): zeigt pro Spalte Min/Max-Editierfelder (User-Regelbereich)
        und den Ausblenden-Toggle; ausgeblendete Spalten erscheinen gedimmt.
        Im Normalmodus verschwinden uiHidden-Spalten komplett (das Bus-Layout
        bleibt IMMER unverändert). */
    void setDevMode (bool shouldBeInDevMode);
    [[nodiscard]] bool isDevMode() const noexcept { return devMode; }

    /** Feuert nach jedem Spalten-Rebuild (Dev-Toggle, uiHidden-Änderung) —
        der NodeComponent zieht darüber seine Kachelgröße nach. */
    std::function<void()> onLayoutChanged;

    //==========================================================================
    // Layout-Konstanten — zentral, damit NodeComponent-Sizing und Tests
    // dieselbe Quelle nutzen
    static constexpr int columnWidth  = 56;
    static constexpr int titleHeight  = 18;
    static constexpr int knobHeight   = 28;
    static constexpr int portRowHeight = 26;
    static constexpr int hideRowHeight = 16;   // Ausblenden-/Kurven-Zeile (Dev-Modus)
    static constexpr int panelHeight  = 248;

    // Fader↔Button-Modus (4.6): ein Button-Stapel ist eine Spaltenbreite,
    // die +/−-Stepper-Zeile sitzt im Dev-Modus unter den Stapeln
    static constexpr int buttonStackWidth = 56;
    static constexpr int stepperRowHeight = 16;

    /** Erster CV-Eingangs-Kanal des Chassis (Audio 0..1, CV 2..N — 4.6). */
    static constexpr int firstCvChannel = 2;

    /** Panel-Breite für n sichtbare DSP-Spalten (plus zwei Gain-Züge) —
        Spezialfall „alle Spalten im Fader-Modus" von getPreferredWidth(). */
    [[nodiscard]] static int widthForColumns (int numDspColumns) noexcept
    {
        return 2 * GainFaderMeter::preferredWidth
             + juce::jmax (0, numDspColumns) * columnWidth + 16;
    }

    /** Breite EINER Spalte: Fader-Modus 56px; Button-Modus (Nicht-Dev)
        1–2 Stapel à buttonStackWidth (5 Buttons pro Stapel); Dev+Buttons:
        Fader-Spalte PLUS Stapel daneben (Fader zum Wert-Finden, 4.6). */
    [[nodiscard]] static int columnWidthFor (bool isButtonMode, bool isInDevMode,
                                             int numButtons) noexcept
    {
        if (! isButtonMode)
            return columnWidth;

        const auto stacks = juce::jmax (1,
            (numButtons + ChassisSchema::maxUiButtonsPerStack - 1)
                / ChassisSchema::maxUiButtonsPerStack);

        return isInDevMode ? columnWidth + stacks * buttonStackWidth
                           : stacks * buttonStackWidth;
    }

    /** Ist-Breite des Panels aus den GEBAUTEN Spalten (variable Breiten —
        Button-Spalten sind breiter); degeneriert ohne Button-Spalten exakt
        zu widthForColumns(getNumColumns()). */
    [[nodiscard]] int getPreferredWidth() const noexcept;

    [[nodiscard]] int getNumColumns() const noexcept { return static_cast<int> (columns.size()); }

    /** Kabel-Anker des CV-Ports für cvChannel (firstCvChannel + Spaltenindex),
        in PANEL-Koordinaten — NodeComponent::getPortCentre rechnet um.
        Nullpunkt, wenn der Kanal keiner Spalte gehört. */
    [[nodiscard]] juce::Point<int> cvPortCentre (int cvChannel) const;

    /** CV-Port einer Spalte (für findPortNear des NodeComponent). */
    [[nodiscard]] const PortComponent* getCvPort (int columnIndex) const noexcept;

    void resized() override;

    //==========================================================================
    /** Wert-Button (Fader↔Button-Modus 4.6): Label-basiert, damit das
        Doppelklick-Rename (Dev-Modus) das bewährte setEditable-Muster nutzt.
        Klick = onClick — Nicht-Dev: gespeicherten Wert abrufen (Fader-Pfad),
        Dev: aktuellen Fader-Wert in den Button speichern (undo-fähig). */
    struct ValueButton final : juce::Label
    {
        std::function<void()> onClick;
        double storedValue = 0.0;   // Cache für die Aktiv-Markierung (kein Re-Parse)
        bool active = false;        // paramValue == storedValue (exactlyEqual über float)

        void mouseUp (const juce::MouseEvent& e) override;
        void paint (juce::Graphics& g) override;
    };

    //==========================================================================
    // Eine DSP-Parameter-Spalte — Controls public, UI-Tests treiben sie direkt
    struct ParameterColumn
    {
        juce::String paramId;
        juce::String cvAmountId;   // "{param}_cv_amt"
        int cvChannel = -1;        // festes Layout: numAudioIns + dsp-Index (4.6)
        bool hidden = false;       // uiHidden-Snapshot beim Build

        // Fader↔Button-Modus (4.6): Snapshots beim Build — bestimmen die
        // Spaltenbreite (columnWidthFor) und das Layout in resized()
        bool buttonMode = false;   // uiMode == "buttons"
        int  numButtons = 0;       // Länge der uiButtons-Liste
        juce::Label  titleLabel;
        CurvedSlider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::Slider cvKnob { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::NoTextBox };
        std::unique_ptr<PortComponent> cvPort;   // fehlt bei uiHidden (Kabel getrennt)

        // Wert-Buttons (Fader↔Button-Modus 4.6) — nur bei buttonMode gebaut
        std::vector<std::unique_ptr<ValueButton>> valueButtons;

        // Dev-Modus-Controls (nur im Dev-Modus erzeugt). Min/Max des
        // User-Regelbereichs leben IM Kurven-Editor (CallOutBox, ~-Button)
        juce::TextButton hideButton;
        juce::TextButton curveButton;   // öffnet Kurve + Range (CurveEditor)
        juce::TextButton modeButton;    // Fader↔Buttons ("btn"/"fdr")
        juce::TextButton addButton;     // "+" — Button anhängen (Dev + buttonMode)
        juce::TextButton removeButton;  // "−" — letzten Button entfernen
    };

    std::vector<std::unique_ptr<ParameterColumn>> columns;

    /** Anzahl der ausgeblendeten dsp-Parameter im Tree (unabhängig vom
        Modus) — für Sizing-Entscheidungen des NodeComponent/Tests. */
    [[nodiscard]] int getNumHiddenParameters() const;

    // Gain-Züge (immer vorhanden — Chassis-Standard)
    std::unique_ptr<GainFaderMeter> inputFader;
    std::unique_ptr<GainFaderMeter> outputFader;

    //==========================================================================
    // Link-Send-Tap am Modul-Ausgang (4.6): Toggle unter dem Output-Zug,
    // schreibt undo-fähig über GraphManager::setLinkSendEnabled; die LED
    // daneben zeigt offline/announced/streaming (Status pro Tick transient
    // aus dem Modul — Muster LinkAudioStatusBadge)
    juce::TextButton linkSendButton { "LINK" };

    // Dev-Modus: sichert die aktuellen dsp-Overrides (Range/Hidden/Kurve)
    // als Modul-Typ-Defaults (ModuleUiDefaults, 4.6) — künftige Neu-Anlagen
    // dieses factoryIds erben sie
    juce::TextButton saveDefaultsButton { "als Standard" };

    /** LED-Status jetzt aus dem Modul ziehen — public für headless Tests. */
    void refreshSendStatusNow();

    [[nodiscard]] LinkSendTaps::Status getShownSendStatus() const noexcept { return shownSendStatus; }

    void paint (juce::Graphics& g) override;

private:
    //==========================================================================
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void timerCallback() override;

    void buildColumns();
    void rebuildColumns();   // clear + build + resized + onLayoutChanged
    void refreshSendButtonState();
    void applyUserRangeToColumn (ParameterColumn& column, const juce::ValueTree& param);
    void layoutValueButtons (ParameterColumn& column, juce::Rectangle<int> stackArea);

    [[nodiscard]] juce::Rectangle<int> sendLedBounds() const;

    [[nodiscard]] juce::ValueTree parametersTree() const;
    [[nodiscard]] juce::ValueTree paramTreeFor (const juce::String& paramId) const;

    //==========================================================================
    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;

    LinkSendTaps::Status shownSendStatus = LinkSendTaps::Status::offline;
    bool devMode = false;   // transient pro Kachel (4.6)

    // Friedhof des Spalten-Rebuilds: der Auslöser kann der hideButton einer
    // der alten Spalten sein — synchrones Zerstören wäre ein Use-after-free
    // im eigenen onClick (Muster TransportBar: Destruktion deferred). Wird
    // asynchron bzw. beim nächsten Rebuild geleert.
    std::vector<std::unique_ptr<ParameterColumn>> retiredColumns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxModulePanel)
};

} // namespace conduit
