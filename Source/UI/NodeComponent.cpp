#include "NodeComponent.h"

#include "Modules/ConduitModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ScopeModule.h"
#include "Modules/StepSequencerModule.h"

namespace conduit
{

NodeComponent::NodeComponent (juce::ValueTree nodeTreeToBind,
                              GraphManager& graphManagerToUse,
                              NodeUiRegistry& uiRegistryToUse,
                              ChannelNames* channelNamesToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse),
      uiRegistry (uiRegistryToUse),
      channelNames (channelNamesToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    uiRegistry.acquire (nodeUuid);
    nodeTree.addListener (this);

    const auto factoryKey = GraphManager::factoryKeyOf (nodeTree);
    isExternalEndpoint = graphManager.isExternalEndpoint (factoryKey);
    const auto isExternal = isExternalEndpoint;

    deleteButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));  // ×
    deleteButton.onClick = [this]
    {
        const auto requested = graphManager.requestNodeDelete (nodeUuid);
        jassertquiet (requested);  // Node muss existieren, solange die UI lebt
    };
    addAndMakeVisible (deleteButton);

    // Externe I/O-Endpunkte sind Grundausstattung — nicht löschbar
    deleteButton.setVisible (! isExternal);

    // named_id im Header — Doppelklick benennt um (OSC-Pfad folgt, 7)
    titleLabel.setText (nodeTree.getProperty (id::moduleId).toString(),
                        juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (15.0f)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    titleLabel.setEditable (false, ! isExternal, false);
    titleLabel.onTextChange = [this]
    {
        if (! graphManager.renameNode (nodeUuid, titleLabel.getText()))
            titleLabel.setText (nodeTree.getProperty (id::moduleId).toString(),
                                juce::dontSendNotification);  // abgelehnt → zurück
        // Bei Erfolg zieht der Property-Listener den sanitierten Namen nach
    };
    addAndMakeVisible (titleLabel);

    // Ports aus den persistierten Kanalzahlen (Schema 6.2); Kanal-Labels der
    // I/O-Endpunkte zieht rebuildPorts() gleich mit nach
    rebuildPorts();

    // Änderungen der ChannelNames (Rename, Gerätewechsel) ziehen Labels nach
    if (channelNames != nullptr && portLabelDirection().has_value())
        channelNames->addChangeListener (this);

    // Sequencer- und Send-Kacheln haben eine eigene Bedienleiste (Grid bzw.
    // Attenuator-Zeilen) — kein generischer Slider
    if (const auto parameter = firstParameter();
        parameter.isValid()
        && factoryKey != StepSequencerModule::staticModuleId
        && factoryKey != LinkAudioSendModule::staticModuleId)
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

    // Modulspezifische Anzeigen direkt in der Kachel
    if (factoryKey == ScopeModule::staticModuleId)
    {
        scopeDisplay = std::make_unique<ScopeDisplay> (graphManager, nodeUuid);
        addAndMakeVisible (*scopeDisplay);
        setSize (252, 168);
    }
    else if (factoryKey == StepSequencerModule::staticModuleId)
    {
        stepGrid = std::make_unique<StepGridDisplay> (nodeTree, graphManager);
        addAndMakeVisible (*stepGrid);

        sequencerControls = std::make_unique<SequencerControlPanel> (nodeTree);
        addAndMakeVisible (*sequencerControls);
        setSize (492, 380);
    }
    else if (factoryKey == LinkAudioSendModule::staticModuleId)
    {
        sendPanel = std::make_unique<LinkAudioSendPanel> (nodeTree, graphManager);
        addAndMakeVisible (*sendPanel);

        // Höhe folgt der Eingangszahl (fixe Zahl, kein Live-Umbau)
        const auto numInputs = juce::jmax (1, nodeTree.getChildWithName (id::inputs).getNumChildren());
        setSize (280, touchTarget + LinkAudioSendPanel::heightForInputs (numInputs));
    }
    else if (isExternalEndpoint)
    {
        updateEndpointSize();  // Höhe folgt der Hardware-Kanalzahl (Schritt B)
    }
    else
    {
        setSize (defaultWidth, defaultHeight);
    }

    applyTreePosition();
}

NodeComponent::~NodeComponent()
{
    if (channelNames != nullptr)
        channelNames->removeChangeListener (this);

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

    if (channelNames != nullptr)
        channelNames->removeChangeListener (this);
    setInterceptsMouseClicks (false, false);
    deleteButton.setEnabled (false);
    parameterSlider.setEnabled (false);
    titleLabel.setEnabled (false);

    if (scopeDisplay != nullptr)
        scopeDisplay->stopUpdates();  // keine Rendering-Updates mehr (5.3 Phase 1)

    if (stepGrid != nullptr)
        stepGrid->stopUpdates();

    if (sequencerControls != nullptr)
        sequencerControls->setEnabled (false);

    if (sendPanel != nullptr)
        sendPanel->stopUpdates();

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
        else if (property == id::moduleId)  // Rename (auch Undo/OSC-extern)
            titleLabel.setText (tree.getProperty (id::moduleId).toString(),
                                juce::dontSendNotification);
        else if (property == id::numInputChannels || property == id::numOutputChannels)
        {
            // I/O-Endpunkt hat die Hardware-Kanalzahl geändert (Schritt B):
            // Ports neu bauen, Kachel an die neue Zahl anpassen, Kabel folgen
            rebuildPorts();

            if (isExternalEndpoint)
                updateEndpointSize();

            resized();   // neue Ports positionieren (auch bei gleicher Größe)
            repaint();

            if (auto* parent = getParentComponent())
                parent->repaint();  // Kabel-Pfade des Canvas neu zeichnen
        }

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

void NodeComponent::rebuildPorts()
{
    inputPorts.clear();
    outputPorts.clear();

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

    refreshPortTooltips();  // Kanal-Labels der I/O-Endpunkte nachziehen
}

void NodeComponent::updateEndpointSize()
{
    // Ein Port braucht rund eine Touch-Reihe; die Höhe folgt dem größeren der
    // beiden Port-Bänke. defaultHeight (2 Ports) bleibt so unverändert.
    const auto maxPorts = juce::jmax (getNumInputPorts(), getNumOutputPorts(), 1);
    setSize (defaultWidth, touchTarget + maxPorts * 30);
}

//==============================================================================
std::optional<ChannelNames::Direction> NodeComponent::portLabelDirection() const
{
    if (channelNames == nullptr)
        return std::nullopt;

    // audio_input speist den Graph: seine OUTPUT-Ports sind Hardware-Inputs
    const auto factoryKey = GraphManager::factoryKeyOf (nodeTree);

    if (factoryKey == audioInputModuleId)
        return ChannelNames::Direction::input;

    if (factoryKey == audioOutputModuleId)
        return ChannelNames::Direction::output;

    return std::nullopt;
}

void NodeComponent::refreshPortTooltips()
{
    const auto direction = portLabelDirection();
    if (! direction.has_value())
        return;

    auto& ports = *direction == ChannelNames::Direction::input ? outputPorts : inputPorts;

    for (auto& port : ports)
        port->setTooltip (channelNames->getLabel (*direction, port->getInfo().channel));
}

void NodeComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPortTooltips();
    repaint();  // gemalte Port-Labels nachziehen
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

    if (error.isNotEmpty())
    {
        g.setColour (juce::Colours::orangered);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (error, getLocalBounds().reduced (8).removeFromBottom (16),
                    juce::Justification::centredLeft);
    }

    // Kanal-Labels neben den Ports der I/O-Endpunkte — Touch hat keinen
    // Hover, deshalb gemalt statt nur als Tooltip (ChannelNames-Quelle)
    if (const auto direction = portLabelDirection(); direction.has_value())
    {
        const auto isInputEndpoint = *direction == ChannelNames::Direction::input;
        const auto numPorts = isInputEndpoint ? getNumOutputPorts() : getNumInputPorts();

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        for (int channel = 0; channel < numPorts; ++channel)
        {
            // audio_input: Ports rechts → Label links davon, rechtsbündig
            const auto centre = getPortCentre (! isInputEndpoint, channel);
            const auto area = isInputEndpoint
                            ? juce::Rectangle<int> (centre.x - 110, centre.y - 8, 90, 16)
                            : juce::Rectangle<int> (centre.x + 20,  centre.y - 8, 90, 16);

            g.drawText (channelNames->getLabel (*direction, channel), area,
                        isInputEndpoint ? juce::Justification::centredRight
                                        : juce::Justification::centredLeft);
        }
    }
}

void NodeComponent::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (touchTarget);
    deleteButton.setBounds (header.removeFromRight (touchTarget));
    titleLabel.setBounds (header.withTrimmedLeft (8));

    // Eingerückt, damit der Slider nicht unter den Port-Hit-Zonen liegt
    parameterSlider.setBounds (bounds.removeFromBottom (touchTarget).reduced (28, 0));

    if (scopeDisplay != nullptr)
        scopeDisplay->setBounds (getLocalBounds().withTrimmedTop (touchTarget)
                                     .reduced (24, 8));  // Platz für die Port-Hit-Zonen

    if (stepGrid != nullptr)
    {
        auto sequencerArea = getLocalBounds().withTrimmedTop (touchTarget).reduced (24, 4);
        sequencerControls->setBounds (sequencerArea.removeFromBottom (SequencerControlPanel::preferredHeight));
        stepGrid->setBounds (sequencerArea.withTrimmedBottom (6));
    }

    if (sendPanel != nullptr)
        sendPanel->setBounds (getLocalBounds().withTrimmedTop (touchTarget).reduced (22, 4));

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
