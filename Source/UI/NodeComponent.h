#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ChannelNames.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "UI/LinkAudioSendPanel.h"
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
    1-Finger-Drag verschiebt den Node (x/y im Tree, ohne Undo-Spam).

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
    /** channelNamesToUse darf nullptr sein (Tests) — dann keine Port-Labels. */
    NodeComponent (juce::ValueTree nodeTreeToBind,
                   GraphManager& graphManagerToUse,
                   NodeUiRegistry& uiRegistryToUse,
                   ChannelNames* channelNamesToUse = nullptr);
    ~NodeComponent() override;

    static constexpr int defaultWidth  = 168;
    static constexpr int defaultHeight = 104;
    static constexpr int touchTarget   = 44;   // minimale Touch-Target-Größe (10)

    [[nodiscard]] const juce::String& getNodeUuid() const noexcept { return nodeUuid; }
    [[nodiscard]] bool isTearingDown() const noexcept              { return tearingDown; }

    /** Canvas-Callback: Teardown abgeschlossen — Component jetzt zerstören.
        Nach dem Aufruf darf die Component nicht mehr angefasst werden. */
    std::function<void (NodeComponent&)> onTeardownFinished;

    /** Schließt den Teardown sofort ab (statt auf den nächsten VBlank zu
        warten) — für headless Tests/CI, wo nie ein Frame gerendert wird. */
    void completeTeardownNow();

    //==========================================================================
    /** Port-Mittelpunkt relativ zu dieser Component — für die Kabel-Pfade
        des Canvas. Inputs links, Outputs rechts. */
    [[nodiscard]] juce::Point<int> getPortCentre (bool isInput, int channel) const;

    /** Nächster Port im Umkreis von maxDistance (Touch-Toleranz beim Drop),
        nullptr wenn keiner. localPoint relativ zu dieser Component. */
    [[nodiscard]] const PortComponent* findPortNear (juce::Point<int> localPoint,
                                                     int maxDistance) const;

    [[nodiscard]] int getNumInputPorts() const noexcept;
    [[nodiscard]] int getNumOutputPorts() const noexcept;

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
    [[nodiscard]] juce::ValueTree firstParameter() const;

    //==========================================================================
    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;
    NodeUiRegistry& uiRegistry;
    ChannelNames* channelNames;  // nullptr außerhalb der App (Tests)
    const juce::String nodeUuid;

    juce::Label titleLabel;  // named_id — Doppelklick benennt um (renameNode)
    juce::TextButton deleteButton;
    juce::Slider parameterSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ComponentDragger dragger;

    std::vector<std::unique_ptr<PortComponent>> inputPorts;
    std::vector<std::unique_ptr<PortComponent>> outputPorts;

    // Nur bei Scope-Nodes (factoryId == "scope") — 30-fps-Waveform
    std::unique_ptr<ScopeDisplay> scopeDisplay;

    // Nur bei Sequencer-Nodes (factoryId == "sequencer") — 4×16-Grid
    // plus Urzwerg-Kontrollleiste (ersetzt den generischen Parameter-Slider)
    std::unique_ptr<StepGridDisplay> stepGrid;
    std::unique_ptr<SequencerControlPanel> sequencerControls;

    // Nur bei Link-Audio-Send-Nodes (factoryId == "link_audio_send") —
    // Bedien-Panel: pro Eingang Attenuator + Name + Status-LED (7.2)
    std::unique_ptr<LinkAudioSendPanel> sendPanel;

    std::unique_ptr<juce::VBlankAttachment> teardownVBlank;
    bool tearingDown = false;
    bool teardownNotified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
};

} // namespace conduit
