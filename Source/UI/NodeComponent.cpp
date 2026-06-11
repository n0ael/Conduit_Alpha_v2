#include "NodeComponent.h"

#include "Modules/ConduitModule.h"

namespace conduit
{

NodeComponent::NodeComponent (juce::ValueTree nodeTreeToBind,
                              GraphManager& graphManagerToUse,
                              NodeUiRegistry& uiRegistryToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse),
      uiRegistry (uiRegistryToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    uiRegistry.acquire (nodeUuid);
    nodeTree.addListener (this);

    deleteButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));  // ×
    deleteButton.onClick = [this]
    {
        const auto requested = graphManager.requestNodeDelete (nodeUuid);
        jassertquiet (requested);  // Node muss existieren, solange die UI lebt
    };
    addAndMakeVisible (deleteButton);

    if (const auto parameter = firstParameter(); parameter.isValid())
    {
        parameterSlider.setRange ((double) parameter.getProperty (id::paramMin, 0.0),
                                  (double) parameter.getProperty (id::paramMax, 1.0), 0.0);
        parameterSlider.setValue ((double) parameter.getProperty (id::paramValue, 0.0),
                                  juce::dontSendNotification);

        // Schreibt NUR in den Tree — der GraphManager spiegelt auf das
        // Echtzeit-Atomic. Bewusst ohne UndoManager: Parameter-Sweeps sind
        // keine patchbaren Aktionen (gleiches Verhalten wie der OSC-Pfad 6.1).
        parameterSlider.onValueChange = [this]
        {
            if (auto parameter = firstParameter(); parameter.isValid())
                parameter.setProperty (id::paramValue, parameterSlider.getValue(), nullptr);
        };
        addAndMakeVisible (parameterSlider);
    }

    setSize (defaultWidth, defaultHeight);
    applyTreePosition();
}

NodeComponent::~NodeComponent()
{
    nodeTree.removeListener (this);
    uiRegistry.release (nodeUuid);  // gibt eine wartende Phase 2 frei (5.3)
}

//==============================================================================
void NodeComponent::beginTeardown()
{
    if (tearingDown)
        return;

    // Phase 1 (5.3): Rendering-Updates stoppen, Listener deregistrieren,
    // Interaktion abschalten — die Registry-Freigabe folgt erst nach dem
    // letzten Render-Zyklus.
    tearingDown = true;
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);
    deleteButton.setEnabled (false);
    parameterSlider.setEnabled (false);
    repaint();

    teardownVBlank = std::make_unique<juce::VBlankAttachment> (this, [this] (double)
    {
        // Nicht im VBlank-Callback zerstören — einen Message-Loop-Schritt
        // entkoppeln. Der VBlank feuert pro Frame: nur einmal dispatchen.
        if (teardownNotified)
            return;

        teardownNotified = true;

        juce::Component::SafePointer<NodeComponent> self (this);
        juce::MessageManager::callAsync ([self]
        {
            if (self != nullptr)
                self->completeTeardownNow();
        });
    });
}

void NodeComponent::completeTeardownNow()
{
    if (! tearingDown)
        return;

    teardownVBlank.reset();

    if (onTeardownFinished != nullptr)
        onTeardownFinished (*this);  // zerstört uns — danach nichts mehr anfassen
}

//==============================================================================
void NodeComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == nodeTree)
    {
        if (property == id::positionX || property == id::positionY)
            applyTreePosition();
        else if (property == id::nodeState
                 && tree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
            beginTeardown();
        else if (property == id::nodeError)
            repaint();

        return;
    }

    // Slider folgt externen Quellen (OSC-Nachzug 6.1, Undo, Preset-Load) —
    // dontSendNotification verhindert die Rückkopplungsschleife.
    if (tree.hasType (id::parameter) && property == id::paramValue
        && tree == firstParameter())
        parameterSlider.setValue ((double) tree.getProperty (id::paramValue),
                                  juce::dontSendNotification);
}

void NodeComponent::applyTreePosition()
{
    setTopLeftPosition ((int) nodeTree.getProperty (id::positionX, 0),
                        (int) nodeTree.getProperty (id::positionY, 0));
}

juce::ValueTree NodeComponent::firstParameter() const
{
    return nodeTree.getChildWithName (id::parameters).getChild (0);
}

//==============================================================================
void NodeComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto error  = nodeTree.getProperty (id::nodeError).toString();

    auto fill = juce::Colour (0xff2d3138);

    if (tearingDown)
        fill = fill.withAlpha (0.4f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (error.isNotEmpty() ? juce::Colours::orangered
                                    : juce::Colour (0xff4a5160));
    g.drawRoundedRectangle (bounds, 8.0f, error.isNotEmpty() ? 2.0f : 1.0f);

    g.setColour (juce::Colours::white.withAlpha (tearingDown ? 0.4f : 0.9f));
    g.setFont (juce::Font (juce::FontOptions (15.0f)));
    g.drawText (nodeTree.getProperty (id::moduleId).toString(),
                getLocalBounds().removeFromTop (touchTarget).withTrimmedLeft (12)
                    .withTrimmedRight (touchTarget),
                juce::Justification::centredLeft);

    if (error.isNotEmpty())
    {
        g.setColour (juce::Colours::orangered);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (error, getLocalBounds().reduced (8).removeFromBottom (16),
                    juce::Justification::centredLeft);
    }
}

void NodeComponent::resized()
{
    auto bounds = getLocalBounds();

    deleteButton.setBounds (bounds.removeFromTop (touchTarget).removeFromRight (touchTarget));
    parameterSlider.setBounds (bounds.removeFromBottom (touchTarget).reduced (8, 0));
}

void NodeComponent::mouseDown (const juce::MouseEvent& event)
{
    dragger.startDraggingComponent (this, event);
}

void NodeComponent::mouseDrag (const juce::MouseEvent& event)
{
    // ≤ 1-Frame-Feedback: Component bewegt sich sofort, der Tree zieht nach.
    // Ohne UndoManager — ein Drag erzeugt sonst hunderte Undo-Schritte.
    dragger.dragComponent (this, event, nullptr);
    nodeTree.setProperty (id::positionX, getX(), nullptr);
    nodeTree.setProperty (id::positionY, getY(), nullptr);
}

} // namespace conduit
