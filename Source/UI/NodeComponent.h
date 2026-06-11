#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"

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
*/
class NodeComponent final : public juce::Component,
                            private juce::ValueTree::Listener
{
public:
    NodeComponent (juce::ValueTree nodeTreeToBind,
                   GraphManager& graphManagerToUse,
                   NodeUiRegistry& uiRegistryToUse);
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
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread]
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void beginTeardown();
    void applyTreePosition();
    [[nodiscard]] juce::ValueTree firstParameter() const;

    //==========================================================================
    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;
    NodeUiRegistry& uiRegistry;
    const juce::String nodeUuid;

    juce::TextButton deleteButton;
    juce::Slider parameterSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ComponentDragger dragger;

    std::unique_ptr<juce::VBlankAttachment> teardownVBlank;
    bool tearingDown = false;
    bool teardownNotified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
};

} // namespace conduit
