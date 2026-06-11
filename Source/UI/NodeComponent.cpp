#include "NodeComponent.h"

#include "Modules/ConduitModule.h"
#include "Modules/ScopeModule.h"

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

    // Externe I/O-Endpunkte sind Grundausstattung — nicht löschbar
    deleteButton.setVisible (! graphManager.isExternalEndpoint (
        nodeTree.getProperty (id::moduleId).toString()));

    // Ports aus den persistierten Kanalzahlen (Schema 6.2)
    const auto makePorts = [this] (bool isInput, int count,
                                   std::vector<std::unique_ptr<PortComponent>>& ports)
    {
        for (int channel = 0; channel < count; ++channel)
        {
            auto port = std::make_unique<PortComponent> (PortInfo { nodeUuid, isInput, channel });
            addAndMakeVisible (*port);
            ports.push_back (std::move (port));
        }
    };

    makePorts (true,  (int) nodeTree.getProperty (id::numInputChannels,  0), inputPorts);
    makePorts (false, (int) nodeTree.getProperty (id::numOutputChannels, 0), outputPorts);

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
            // eigener Name — verschattet sonst das 'parameter' des umgebenden
            // if-Init (Clang -Wshadow-uncaptured-local unter -Werror)
            if (auto liveParameter = firstParameter(); liveParameter.isValid())
                liveParameter.setProperty (id::paramValue, parameterSlider.getValue(), nullptr);
        };
        addAndMakeVisible (parameterSlider);
    }

    // Scope-Nodes zeigen die Waveform direkt in der Kachel
    if (nodeTree.getProperty (id::moduleId).toString() == ScopeModule::staticModuleId)
    {
        scopeDisplay = std::make_unique<ScopeDisplay> (graphManager, nodeUuid);
        addAndMakeVisible (*scopeDisplay);
        setSize (252, 168);
    }
    else
    {
        setSize (defaultWidth, defaultHeight);
    }

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

    if (scopeDisplay != nullptr)
        scopeDisplay->stopUpdates();  // keine Rendering-Updates mehr (5.3 Phase 1)

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
int NodeComponent::getNumInputPorts() const noexcept  { return static_cast<int> (inputPorts.size()); }
int NodeComponent::getNumOutputPorts() const noexcept { return static_cast<int> (outputPorts.size()); }

juce::Point<int> NodeComponent::getPortCentre (bool isInput, int channel) const
{
    const auto count = isInput ? getNumInputPorts() : getNumOutputPorts();
    const auto availableHeight = getHeight() - touchTarget;  // unterhalb des Headers

    return { isInput ? 12 : getWidth() - 12,
             touchTarget + availableHeight * (channel + 1) / (count + 1) };
}

const PortComponent* NodeComponent::findPortNear (juce::Point<int> localPoint,
                                                  int maxDistance) const
{
    const PortComponent* nearest = nullptr;
    auto nearestDistance = maxDistance;

    const auto consider = [&] (const std::vector<std::unique_ptr<PortComponent>>& ports)
    {
        for (const auto& port : ports)
        {
            const auto centre = getPortCentre (port->getInfo().isInput, port->getInfo().channel);
            const auto distance = juce::roundToInt (centre.getDistanceFrom (localPoint));

            if (distance <= nearestDistance)
            {
                nearest = port.get();
                nearestDistance = distance;
            }
        }
    };

    consider (inputPorts);
    consider (outputPorts);
    return nearest;
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

    // Eingerückt, damit der Slider nicht unter den Port-Hit-Zonen liegt
    parameterSlider.setBounds (bounds.removeFromBottom (touchTarget).reduced (28, 0));

    if (scopeDisplay != nullptr)
        scopeDisplay->setBounds (getLocalBounds().withTrimmedTop (touchTarget)
                                     .reduced (24, 8));  // Platz für die Port-Hit-Zonen

    const auto placePorts = [this] (std::vector<std::unique_ptr<PortComponent>>& ports)
    {
        for (auto& port : ports)
        {
            const auto centre = getPortCentre (port->getInfo().isInput, port->getInfo().channel);
            port->setCentrePosition (centre.x, centre.y);
        }
    };

    placePorts (inputPorts);
    placePorts (outputPorts);
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
