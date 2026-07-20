#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Modules/LinkAudioSendModule.h"

namespace conduit
{

//==============================================================================
/**
    Bedien-Panel eines Multi-Input LinkAudioSendModule — lebt im NodeComponent
    (Muster ParameterPanel/ScopeDisplay).

    Pro Eingang eine Zeile: Status-LED (offline/announced/streaming), Name-
    Editor (Doppelklick → userName; leer = zurück zum Auto-Namen), Mono/Stereo-
    Badge, Attenuator-Slider (schreibt in{n}_gain in den Tree). Footer: ein
    Knopf „Auto-Namen" (GraphManager::refreshAutoNames).

    Bindung an den ValueTree-Subtree (5.3): Namen schreiben inputUserName,
    Attenuatoren paramValue — beide ohne Undo (Parameter-/Namens-Pflege wie
    Slider/OSC, 6.1). Externe Änderungen (Auto-Naming-Snapshot, Undo, Preset-
    Load) kommen über den ValueTree-Listener zurück.

    Per-Slot-Status wie LinkAudioStatusBadge: transiente Modul-Auflösung pro
    Timer-Tick (kein Processor-Pointer, Zombie-UI-Schutz).
*/
class LinkAudioSendPanel final : public juce::Component,
                                 private juce::ValueTree::Listener,
                                 private juce::Timer
{
public:
    LinkAudioSendPanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse);
    ~LinkAudioSendPanel() override;

    /** Teardown-Hook (Phase 1, 5.3): Updates + Interaktion sofort stoppen. */
    void stopUpdates();

    /** Per-Slot-Status jetzt aus dem Modul ziehen — vom Timer gerufen,
        public für headless Tests. */
    void refreshNow();

    /** Höhe der Kachel-Innenfläche für n Eingänge (NodeComponent-Sizing). */
    [[nodiscard]] static int heightForInputs (int numInputs) noexcept
    {
        return topPadding + juce::jmax (1, numInputs) * rowHeight + footerHeight;
    }

    [[nodiscard]] int getNumRows() const noexcept { return static_cast<int> (rows.size()); }

    void resized() override;
    void paint (juce::Graphics& g) override;

    //==========================================================================
    // Zeile eines Eingangs — Controls public, die UI-Tests treiben sie direkt
    struct InputRow
    {
        juce::String  inputId;
        juce::String  gainParamId;
        int           index = 0;
        juce::Label   nameLabel;
        juce::Slider  gainSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        LinkAudioSendModule::SendStatus status = LinkAudioSendModule::SendStatus::offline;
    };

    std::vector<std::unique_ptr<InputRow>> rows;
    juce::TextButton refreshButton { juce::String::fromUTF8 ("Auto-Namen") };

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread]
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void timerCallback() override;

    void buildRows();
    void writeUserName (int rowIndex, const juce::String& text);
    void refreshNameLabel (int rowIndex);

    [[nodiscard]] juce::ValueTree inputsTree() const;
    [[nodiscard]] juce::ValueTree gainParamFor (const juce::String& paramId) const;
    [[nodiscard]] juce::Rectangle<int> rowBounds (int index) const;

    static constexpr int rowHeight    = 30;
    static constexpr int footerHeight = 34;
    static constexpr int topPadding   = 6;

    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;
    const juce::String nodeUuid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkAudioSendPanel)
};

} // namespace conduit
