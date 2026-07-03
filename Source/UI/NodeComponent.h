#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ChannelNames.h"
#include "Core/GraphManager.h"
#include "Core/InputLinkSend.h"
#include "Core/NodeUiRegistry.h"
#include "UI/FxModulePanel.h"
#include "UI/InputSendButton.h"
#include "UI/LevelMeterBar.h"
#include "UI/LinkAudioSendPanel.h"
#include "UI/ParameterPanel.h"
#include "UI/PortComponent.h"
#include "UI/ScopeDisplay.h"
#include "UI/SequencerControlPanel.h"
#include "UI/StepGridDisplay.h"

namespace conduit
{

//==============================================================================
/**
    UI-Kachel eines einzelnen Nodes — bindet sich AUSSCHLIESSLICH an den
    ValueTree-Subtree, nie an den Processor (Zombie-UI-Schutz, CLAUDE.md 5.3).

    Lifecycle:
      - ctor: NodeUiRegistry::acquire() — blockiert Phase 2 des Deletes
      - nodeState → Deleting (Phase 1): beginTeardown() — Listener weg,
        Interaktion aus, ausgegraut; die Freigabe folgt erst NACH dem
        abgeschlossenen Render-Zyklus via juce::VBlankAttachment →
        onTeardownFinished → Canvas zerstört die Component
      - dtor: NodeUiRegistry::release() — gibt Phase 2 frei

    Touch-first (CLAUDE.md 10): Delete-Button 44×44px, Slider-Höhe 44px,
    1-Finger-Drag verschiebt den Node (x/y im Tree, ohne Undo-Spam). Griff
    ist die gesamte Kachelfläche inkl. Kopfzeile (das Titel-Label leitet
    seine Drags weiter, Doppelklick-Rename bleibt); die Bewegung ist
    pixelgenau, nur nahe den Kanten anderer Kacheln rastet sie zur
    Ausrichtung ein (snapToSiblings).

    Kanal-Labels (nur I/O-Endpunkte audio_input/audio_output): die Ports
    zeigen das effektive ChannelNames-Label — gemalt neben dem Port
    (Touch-sichtbar) und als Tooltip (Maus-Hover). Das audio_input-Node
    trägt OUTPUT-Ports → Input-Labels, audio_output umgekehrt.
*/
class NodeComponent final : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::ChangeListener
{
public:
    /** channelNamesToUse darf nullptr sein (Tests) — dann keine Port-Labels.
        inputLevels/outputLevels darf nullptr sein (Tests) — dann keine Meter;
        die I/O-Endpunkte lesen daraus Peak/RMS/Clip pro Kanal (Ableton-Style).
        inputSendToUse (nullptr in Tests): Status-Quelle der Send-LEDs an den
        audio_in-Zeilen — die Buttons existieren mit ChannelNames auch ohne. */
    NodeComponent (juce::ValueTree nodeTreeToBind,
                   GraphManager& graphManagerToUse,
                   NodeUiRegistry& uiRegistryToUse,
                   ChannelNames* channelNamesToUse = nullptr,
                   LevelMeter* inputLevelsToUse = nullptr,
                   LevelMeter* outputLevelsToUse = nullptr,
                   InputLinkSend* inputSendToUse = nullptr);
    ~NodeComponent() override;

    static constexpr int defaultWidth  = 168;
    static constexpr int defaultHeight = 104;
    static constexpr int touchTarget   = 44;   // minimale Touch-Target-Größe (10)
    static constexpr int siblingSnapRange = 10;  // Fangbereich der Kanten-Ausrichtung (px)

    [[nodiscard]] const juce::String& getNodeUuid() const noexcept { return nodeUuid; }
    [[nodiscard]] bool isTearingDown() const noexcept              { return tearingDown; }

    /** Canvas-Callback: Teardown abgeschlossen — Component jetzt zerstören.
        Nach dem Aufruf darf die Component nicht mehr angefasst werden. */
    std::function<void (NodeComponent&)> onTeardownFinished;

    /** Schließt den Teardown sofort ab (statt auf den nächsten VBlank zu
        warten) — für headless Tests/CI, wo nie ein Frame gerendert wird. */
    void completeTeardownNow();

    //==========================================================================
    /** Eine Port-Zeile: channel = erster Kanal, span = 1 (mono) / 2 (Stereo-
        Paar, EIN Port für channel und channel+1). */
    struct PortRow
    {
        int channel = 0;
        int span = 1;
    };

    /** Pure: Kanäle → Port-Zeilen. isPairStart(ch) meldet, ob (ch, ch+1) ein
        Paar ankert — gepaarte Kanäle verschmelzen zu einer Zeile mit span 2.
        Ein Paar am letzten Kanal (ohne Partner) bleibt eine Mono-Zeile. */
    [[nodiscard]] static std::vector<PortRow> buildPortRows (
        int numChannels, const std::function<bool (int)>& isPairStart);

    /** Kabel-Ankerpunkt relativ zu dieser Component — für die Kabel-Pfade
        des Canvas. Inputs links, Outputs rechts. Kanäle eines Stereo-Paars
        liefern denselben Port mit ∓3px Versatz (Doppel-Linien-Optik). */
    [[nodiscard]] juce::Point<int> getPortCentre (bool isInput, int channel) const;

    /** Anker-Kanal des Stereo-Paars, zu dem channel gehört; nullopt wenn
        der Kanal nicht gepaart ist. Für den Doppel-Kabel-Pfad des Canvas. */
    [[nodiscard]] std::optional<int> pairAnchorForPort (bool isInput, int channel) const;

    /** Nächster Port im Umkreis von maxDistance (Touch-Toleranz beim Drop),
        nullptr wenn keiner. localPoint relativ zu dieser Component. */
    [[nodiscard]] const PortComponent* findPortNear (juce::Point<int> localPoint,
                                                     int maxDistance) const;

    [[nodiscard]] int getNumInputPorts() const noexcept;
    [[nodiscard]] int getNumOutputPorts() const noexcept;
    [[nodiscard]] bool hasFxPanel() const noexcept { return fxPanel != nullptr; }
    [[nodiscard]] FxModulePanel* getFxPanel() noexcept { return fxPanel.get(); }
    [[nodiscard]] int getNumMeterBars() const noexcept;  // Pegelanzeigen (I/O-Endpunkte)
    [[nodiscard]] int getNumSendButtons() const noexcept;  // Link-Send-Toggles (audio_in)

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread]
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    // juce::ChangeListener [Message Thread] — ChannelNames-Labels
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** Richtung der Kanal-Labels dieses Endpunkts; nullopt = kein Endpunkt
        oder keine ChannelNames-Quelle. */
    [[nodiscard]] std::optional<ChannelNames::Direction> portLabelDirection() const;
    void refreshPortTooltips();

    void beginTeardown();
    void applyTreePosition();

    /** Rastet die Position an den Kanten der Geschwister-Kacheln ein:
        Oberkanten (gleiche Höhe) und linke Kanten (bündig untereinander),
        je Achse unabhängig innerhalb von siblingSnapRange — außerhalb des
        Fangbereichs bleibt die Bewegung pixelgenau. */
    [[nodiscard]] juce::Point<int> snapToSiblings (juce::Point<int> position) const;

    /** (Neu-)Baut die Port-Components aus numInputChannels/numOutputChannels
        (Schema 6.2) und zieht die Kanal-Labels nach. Am audio_in-Endpunkt
        verschmelzen ChannelNames-Stereo-Paare zu einem Port (span 2), plus
        Koppel-Toggles zwischen den Kanal-Zeilen. Aufgerufen im Konstruktor,
        bei Kanalzahl-Änderung (Schritt B) und bei ChannelNames-Änderungen. */
    void rebuildPorts();

    /** true, wenn dieser Endpunkt Pairing-UI trägt (audio_in + ChannelNames). */
    [[nodiscard]] bool hasPairingUi() const noexcept;

    /** Zeilen-Mitte eines KANALS (feste 30px-Rasterung nach Kanalzahl) —
        Meter und Labels bleiben eine Zeile pro Kanal, auch bei Pairing. */
    [[nodiscard]] int channelRowY (bool isInputBank, int channel) const;

    /** Kachelhöhe der I/O-Endpunkte folgt der Portzahl (Hardware-Kanäle).
        Andere Module haben feste Busse und setzen ihre Größe selbst. */
    void updateEndpointSize();

    /** (Neu-)Baut die Pegelanzeigen der I/O-Endpunkte (eine pro Kanal), wenn
        ein LevelMeter-Provider vorhanden ist. */
    void rebuildMeters();

    /** true, wenn dieser Endpunkt Meter zeigt (Provider + externer Endpunkt). */
    [[nodiscard]] bool hasMeters() const noexcept;

    /** Balken-Rechteck eines Kanals (isInputEndpoint = audio_in). */
    [[nodiscard]] juce::Rectangle<int> meterBoundsFor (bool isInputEndpoint, int channel) const;

    //==========================================================================
    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;
    NodeUiRegistry& uiRegistry;
    ChannelNames* channelNames;  // nullptr außerhalb der App (Tests)
    LevelMeter* inputLevels;     // Sicht-Metering audio_in (nullptr in Tests)
    LevelMeter* outputLevels;    // Sicht-Metering audio_out (nullptr in Tests)
    InputLinkSend* inputSend;    // Status-Quelle der Send-LEDs (nullptr in Tests)
    const juce::String nodeUuid;

    juce::Label titleLabel;  // named_id — Doppelklick benennt um (renameNode)
    juce::TextButton deleteButton;
    juce::ComponentDragger dragger;

    std::vector<std::unique_ptr<PortComponent>> inputPorts;
    std::vector<std::unique_ptr<PortComponent>> outputPorts;

    // Port-Zeilen der beiden Bänke (Pairing verschmilzt Kanäle zu span-2-
    // Zeilen); Kanalzahlen für die feste Zeilen-Rasterung der Meter/Labels
    std::vector<PortRow> inputRows, outputRows;
    int inputChannelCount = 0, outputChannelCount = 0;

    // Koppel-Toggles zwischen benachbarten Kanal-Zeilen (nur audio_in).
    // Hit-Zone 24px wie die Ports — bewusst unter dem 44px-Touch-Ziel (10),
    // gleiche Ausnahme wie die Port-Hit-Zonen.
    std::vector<std::unique_ptr<juce::TextButton>> pairToggles;

    // Link-Send-Toggles: einer pro Port-ZEILE (Paar = ein Send am Anker),
    // nur audio_in (7.2) — mit den Ports neu gebaut
    std::vector<std::unique_ptr<InputSendButton>> sendButtons;

    // Pegelanzeigen der I/O-Endpunkte — eine pro Kanal der aktiven Bank
    std::vector<std::unique_ptr<LevelMeterBar>> meterBars;

    // audio_input/audio_output — Grundausstattung, Höhe folgt der Hardware-
    // Kanalzahl (Schritt B); im Konstruktor gesetzt.
    bool isExternalEndpoint = false;
    bool endpointIsInput = false;  // true = audio_in (Meter-Layout/Provider)

    // Nur bei Scope-Nodes (factoryId == "scope") — 30-fps-Waveform
    std::unique_ptr<ScopeDisplay> scopeDisplay;

    // Nur bei Sequencer-Nodes (factoryId == "sequencer") — 4×16-Grid
    // plus Urzwerg-Kontrollleiste (ersetzt den generischen Parameter-Slider)
    std::unique_ptr<StepGridDisplay> stepGrid;
    std::unique_ptr<SequencerControlPanel> sequencerControls;

    // Nur bei Link-Audio-Send-Nodes (factoryId == "link_audio_send") —
    // Bedien-Panel: pro Eingang Attenuator + Name + Status-LED (7.2)
    std::unique_ptr<LinkAudioSendPanel> sendPanel;

    // Alle anderen Module mit >= 1 Parameter (nicht Scope/Sequencer/Send/
    // Processor, die eigene Bedienoberflächen haben) — eine Zeile pro
    // Parameter, Label = paramId. nullptr bei 0 Parametern (I/O-Endpunkte).
    std::unique_ptr<ParameterPanel> parameterPanel;

    // Processor-Nodes (FX-Chassis, 4.6): Gain-Züge + vertikale Fader-Reihe
    std::unique_ptr<FxModulePanel> fxPanel;

    std::unique_ptr<juce::VBlankAttachment> teardownVBlank;
    bool tearingDown = false;
    bool teardownNotified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
};

} // namespace conduit
