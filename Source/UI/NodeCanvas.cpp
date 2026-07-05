#include "NodeCanvas.h"

#include <algorithm>

#include "Modules/AttenuatorModule.h"
#include "PushLookAndFeel.h"
#include "UI/Browser/BrowserDragPayload.h"

namespace conduit
{

NodeCanvas::NodeCanvas (juce::ValueTree rootTree,
                        GraphManager& graphManagerToUse,
                        NodeUiRegistry& uiRegistryToUse,
                        ChannelNames* channelNamesToUse,
                        LevelMeter* inputLevelsToUse,
                        LevelMeter* outputLevelsToUse,
                        InputLinkSend* inputSendToUse,
                        UiSettings* uiSettingsToUse)
    : rootState (std::move (rootTree)),
      graphManager (graphManagerToUse),
      uiRegistry (uiRegistryToUse),
      channelNames (channelNamesToUse),
      inputLevels (inputLevelsToUse),
      outputLevels (outputLevelsToUse),
      inputSend (inputSendToUse),
      uiSettings (uiSettingsToUse)
{
    rootState.addListener (this);

    // Input-Kanal-Farben leben in ChannelNames (App-Zustand) — Änderungen
    // dort müssen die Kabel neu einfärben (M-B)
    if (channelNames != nullptr)
        channelNames->addChangeListener (this);

    rebuildAll();  // Tree kann schon Nodes tragen (Session-Restore)
}

NodeCanvas::~NodeCanvas()
{
    if (channelNames != nullptr)
        channelNames->removeChangeListener (this);

    rootState.removeListener (this);
}

//==============================================================================
int NodeCanvas::getNumNodeComponents() const noexcept
{
    return static_cast<int> (nodeComponents.size());
}

NodeComponent* NodeCanvas::findNodeComponent (const juce::String& nodeUuid) const
{
    for (const auto& component : nodeComponents)
        if (component->getNodeUuid() == nodeUuid)
            return component.get();

    return nullptr;
}

//==============================================================================
void NodeCanvas::rebuildAll()
{
    nodeComponents.clear();

    const auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
        addComponentFor (nodesTree.getChild (i));

    refreshFlowColours();  // effektive Farben nach dem Voll-Rebuild
}

void NodeCanvas::addComponentFor (juce::ValueTree nodeTree)
{
    const auto nodeUuid = nodeTree.getProperty (id::nodeId).toString();

    if (nodeUuid.isEmpty() || findNodeComponent (nodeUuid) != nullptr)
        return;

    // Deleting-Nodes (Phase 1 läuft) bekommen keine neue UI — der Subtree
    // verschwindet in Phase 2; nach einem Undo-Restore zieht der
    // nodeState → Active-Übergang die Component nach.
    if (nodeTree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
        return;

    auto component = std::make_unique<NodeComponent> (nodeTree, graphManager, uiRegistry,
                                                      channelNames, inputLevels, outputLevels,
                                                      inputSend, uiSettings);

    component->onTeardownFinished = [this] (NodeComponent& finished)
    {
        removeComponentFor (finished.getNodeUuid());  // zerstört die Component
    };

    addAndMakeVisible (*component);
    nodeComponents.push_back (std::move (component));
}

void NodeCanvas::removeComponentFor (const juce::String& nodeUuid)
{
    const auto it = std::find_if (nodeComponents.begin(), nodeComponents.end(),
                                  [&nodeUuid] (const auto& component)
                                  { return component->getNodeUuid() == nodeUuid; });

    if (it != nodeComponents.end())
        nodeComponents.erase (it);  // dtor released die NodeUiRegistry-Referenz
}

//==============================================================================
void NodeCanvas::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Kabel folgen bewegten Nodes
    if (property == id::positionX || property == id::positionY)
    {
        repaint();
        return;
    }

    // Node-Farbe geändert → effektive Farben (inkl. Downstream-Vererbung) neu
    if (property == id::nodeColour)
    {
        refreshFlowColours();
        return;
    }

    // Undo-Restore-Fall: Subtree kam mit nodeState == Deleting zurück,
    // der Graph-Swap setzt ihn auf Active — jetzt UI nachziehen.
    if (property == id::nodeState && tree.hasType (id::node)
        && tree.getProperty (id::nodeState).toString() == toString (NodeState::active))
    {
        addComponentFor (tree);
        refreshFlowColours();
    }
}

void NodeCanvas::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (id::nodes) && child.hasType (id::node))
    {
        addComponentFor (child);
        refreshFlowColours();
    }
    else if (child.hasType (id::nodes))
        rebuildAll();  // Container-Austausch (Preset-Load)
    else if (parent.hasType (id::connections) || child.hasType (id::connections))
        refreshFlowColours();  // neues Kabel → Vererbung neu
}

void NodeCanvas::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    // Fallback für Entfernung ohne Deleting-Phase (Redo, Preset-Load):
    // sofort zerstören — der reguläre Delete-Pfad hat die Component an
    // dieser Stelle bereits über den Teardown abgebaut.
    if (parent.hasType (id::nodes) && child.hasType (id::node))
    {
        removeComponentFor (child.getProperty (id::nodeId).toString());
        refreshFlowColours();
    }
    else if (child.hasType (id::nodes))
        rebuildAll();
    else if (parent.hasType (id::connections) || child.hasType (id::connections))
        refreshFlowColours();  // Kabel entfernt → Vererbung neu
}

void NodeCanvas::valueTreeRedirected (juce::ValueTree&)
{
    rebuildAll();
}

void NodeCanvas::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // ChannelNames-Farbe/Pairing geändert → Vererbung + Input-Kabel neu
    refreshFlowColours();
}

//==============================================================================
void NodeCanvas::beginCableDrag (const PortInfo& fromPort, juce::Point<int> position)
{
    activeCableDrag = CableDrag { fromPort, position };
    repaint();
}

void NodeCanvas::updateCableDrag (juce::Point<int> position)
{
    if (activeCableDrag)
    {
        activeCableDrag->currentPosition = position;
        repaint();
    }
}

void NodeCanvas::setCableDragMono()
{
    if (activeCableDrag)
        activeCableDrag->forceMono = true;
}

void NodeCanvas::endCableDrag (juce::Point<int> position)
{
    if (! activeCableDrag)
        return;

    const auto from      = activeCableDrag->from;
    const auto forceMono = activeCableDrag->forceMono;
    activeCableDrag.reset();
    repaint();

    for (const auto& component : nodeComponents)
    {
        const auto* port = component->findPortNear (position - component->getPosition(),
                                                    NodeComponent::touchTarget);

        if (port == nullptr || port->getInfo().isInput == from.isInput)
            continue;  // nur Output ↔ Input ergibt ein Kabel

        // Richtung normalisieren: Output-Seite ist immer die Quelle.
        // Duplikat/Selbstverbindung lehnt der GraphManager ab — kein Assert.
        const auto& source = from.isInput ? port->getInfo() : from;
        const auto& dest   = from.isInput ? from            : port->getInfo();

        // Stereo-Quelle (span-2, z.B. FX-Audio 0/1 oder gekoppeltes audio_in):
        // Default = beide Kabel in einer Undo-Transaktion. Dwell-Geste
        // (forceMono) → nur ein Einzel-Kabel vom Anker-Kanal.
        if (source.span == 2 && ! forceMono)
            graphManager.addConnectionPair (source.nodeUuid, source.channel,
                                            dest.nodeUuid, dest.channel);
        else
            graphManager.addConnection (source.nodeUuid, source.channel,
                                        dest.nodeUuid, dest.channel);
        return;
    }
}

juce::ValueTree NodeCanvas::findConnectionAt (juce::Point<int> position) const
{
    const auto connectionsTree = rootState.getChildWithName (id::connections);
    const auto target = position.toFloat();

    for (int i = 0; i < connectionsTree.getNumChildren(); ++i)
    {
        const auto connection = connectionsTree.getChild (i);

        const auto start = getPortCentreInCanvas (connection.getProperty (id::sourceNodeId).toString(),
                                                  false, (int) connection.getProperty (id::sourceChannel));
        const auto end   = getPortCentreInCanvas (connection.getProperty (id::destNodeId).toString(),
                                                  true,  (int) connection.getProperty (id::destChannel));

        if (! start || ! end)
            continue;

        juce::Point<float> nearestOnPath;
        makeCablePath (start->toFloat(), end->toFloat()).getNearestPoint (target, nearestOnPath);

        if (nearestOnPath.getDistanceFrom (target) <= 8.0f)
            return connection;
    }

    return {};
}

std::optional<juce::Point<int>> NodeCanvas::getPortCentreInCanvas (const juce::String& nodeUuid,
                                                                   bool isInput, int channel) const
{
    if (const auto* component = findNodeComponent (nodeUuid))
        return component->getPosition() + component->getPortCentre (isInput, channel);

    return std::nullopt;
}

juce::Path NodeCanvas::makeCablePath (juce::Point<float> start, juce::Point<float> end)
{
    // Bezier mit horizontalen Tangenten — klassische Patch-Kabel-Optik
    const auto bend = juce::jmax (40.0f, std::abs (end.x - start.x) * 0.5f);

    juce::Path path;
    path.startNewSubPath (start);
    path.cubicTo (start.translated (bend, 0.0f), end.translated (-bend, 0.0f), end);
    return path;
}

//==============================================================================
void NodeCanvas::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    // Browser-Drag schwebt über der Fläche: Akzent-Rahmen als Drop-Hinweis
    if (dropHighlight)
    {
        g.setColour (push::colours::ledOrange.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 2);
    }

    // Kabel aus Connections[] (Schema 6.2) — unter den Node-Kacheln, jedes
    // in der Farbe seiner Quelle (M-B)
    const juce::PathStrokeType cableStroke (3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded);
    const auto connectionsTree = rootState.getChildWithName (id::connections);

    for (int i = 0; i < connectionsTree.getNumChildren(); ++i)
    {
        const auto connection = connectionsTree.getChild (i);

        const auto sourceUuid    = connection.getProperty (id::sourceNodeId).toString();
        const auto sourceChannel = (int) connection.getProperty (id::sourceChannel);

        const auto start = getPortCentreInCanvas (sourceUuid, false, sourceChannel);
        const auto end   = getPortCentreInCanvas (connection.getProperty (id::destNodeId).toString(),
                                                  true,  (int) connection.getProperty (id::destChannel));

        if (start && end)
        {
            g.setColour (cableColourFor (sourceUuid, sourceChannel));
            g.strokePath (makeCablePath (start->toFloat(), end->toFloat()), cableStroke);
        }
    }

    // Kabel-Vorschau während des Drags — Farbe der (Output-)Quelle
    if (activeCableDrag)
    {
        if (const auto origin = getPortCentreInCanvas (activeCableDrag->from.nodeUuid,
                                                       activeCableDrag->from.isInput,
                                                       activeCableDrag->from.channel))
        {
            const auto previewColour = activeCableDrag->from.isInput
                ? juce::Colour (0xff8fd0a0)
                : cableColourFor (activeCableDrag->from.nodeUuid, activeCableDrag->from.channel);

            g.setColour (previewColour.withAlpha (0.5f));
            g.strokePath (makeCablePath (origin->toFloat(),
                                         activeCableDrag->currentPosition.toFloat()),
                          cableStroke);
        }
    }
}

juce::Colour NodeCanvas::cableColourFor (const juce::String& sourceUuid, int sourceChannel) const
{
    const juce::Colour defaultCable (0xff8fd0a0);
    const auto rgb = lookupSourceRgb (sourceUuid, sourceChannel);
    return rgb != 0 ? juce::Colour (0xff000000u | (rgb & 0x00ffffffu)) : defaultCable;
}

//==============================================================================
juce::uint32 NodeCanvas::blendRgb (const std::vector<juce::uint32>& colours)
{
    if (colours.empty())
        return 0;

    juce::uint64 r = 0, g = 0, b = 0;
    for (const auto c : colours)
    {
        r += (c >> 16) & 0xffu;
        g += (c >> 8)  & 0xffu;
        b += c         & 0xffu;
    }

    const auto n = (juce::uint64) colours.size();
    const auto rgb = (juce::uint32) (((r / n) << 16) | ((g / n) << 8) | (b / n));
    return rgb == 0 ? 0x010101u : rgb;  // 0 ist der „keine"-Sentinel
}

juce::uint32 NodeCanvas::computeEffectiveRgb (const juce::String& nodeUuid,
                                              std::set<juce::String>& visiting)
{
    if (const auto it = flowColours.find (nodeUuid); it != flowColours.end())
        return it->second;  // memoisiert

    const auto node = rootState.getChildWithName (id::nodes)
                          .getChildWithProperty (id::nodeId, nodeUuid);
    if (! node.isValid())
        return 0;

    // audio_in ist reine Quelle (Farbe pro Kanal) — kein Einzel-Node-Wert
    if (GraphManager::factoryKeyOf (node) == audioInputModuleId)
        return 0;

    // Explizite Node-Farbe gewinnt IMMER (bewusstes Label)
    if (const auto explicitRgb = (juce::uint32) (int) node.getProperty (id::nodeColour, 0);
        explicitRgb != 0)
    {
        flowColours[nodeUuid] = explicitRgb;
        return explicitRgb;
    }

    if (visiting.count (nodeUuid) > 0)
        return 0;  // Zyklus — Zweig nicht weiter verfolgen (nicht cachen)
    visiting.insert (nodeUuid);

    // Gemischte Farbe aller eingehenden Kabel (0/keine übersprungen)
    std::vector<juce::uint32> incoming;
    const auto connections = rootState.getChildWithName (id::connections);
    for (int i = 0; i < connections.getNumChildren(); ++i)
    {
        const auto conn = connections.getChild (i);
        if (conn.getProperty (id::destNodeId).toString() != nodeUuid)
            continue;

        const auto srcUuid = conn.getProperty (id::sourceNodeId).toString();
        const auto srcCh   = (int) conn.getProperty (id::sourceChannel);

        if (const auto rgb = sourceChannelRgb (srcUuid, srcCh, visiting); rgb != 0)
            incoming.push_back (rgb);
    }

    visiting.erase (nodeUuid);

    const auto result = blendRgb (incoming);
    flowColours[nodeUuid] = result;
    return result;
}

juce::uint32 NodeCanvas::sourceChannelRgb (const juce::String& sourceUuid, int channel,
                                           std::set<juce::String>& visiting)
{
    const auto node = rootState.getChildWithName (id::nodes)
                          .getChildWithProperty (id::nodeId, sourceUuid);
    if (! node.isValid())
        return 0;

    if (channelNames != nullptr && GraphManager::factoryKeyOf (node) == audioInputModuleId)
        return inputChannelRgb (channel);

    return computeEffectiveRgb (sourceUuid, visiting);
}

juce::uint32 NodeCanvas::lookupSourceRgb (const juce::String& sourceUuid, int channel) const
{
    const auto node = rootState.getChildWithName (id::nodes)
                          .getChildWithProperty (id::nodeId, sourceUuid);
    if (! node.isValid())
        return 0;

    // audio_in: Farbe pro Kanal (ChannelNames)
    if (channelNames != nullptr && GraphManager::factoryKeyOf (node) == audioInputModuleId)
        return inputChannelRgb (channel);

    if (const auto it = flowColours.find (sourceUuid); it != flowColours.end())
        return it->second;

    return 0;
}

juce::uint32 NodeCanvas::inputChannelRgb (int channel) const
{
    if (channelNames == nullptr)
        return 0;

    using D = ChannelNames::Direction;

    // Partner-Kanal eines Paars (Vorgänger ist Pair-Start) → Farbe des Ankers
    auto anchor = channel;
    if (channel > 0
        && ! channelNames->isPortPairStart (D::input, channel)
        && channelNames->isPortPairStart (D::input, channel - 1))
        anchor = channel - 1;

    return channelNames->getColour (D::input, anchor);
}

void NodeCanvas::refreshFlowColours()
{
    flowColours.clear();

    const auto nodes = rootState.getChildWithName (id::nodes);
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        std::set<juce::String> visiting;
        computeEffectiveRgb (nodes.getChild (i).getProperty (id::nodeId).toString(), visiting);
    }

    // Effektive Farbe in die Header-Punkte schieben (audio_in hat keinen)
    for (auto& comp : nodeComponents)
    {
        const auto it = flowColours.find (comp->getNodeUuid());
        comp->setFlowColour (it != flowColours.end() ? it->second : 0);
    }

    // Port-Striche einfärben: verbundener Port = Kabelfarbe, sonst neutral
    const auto connections = rootState.getChildWithName (id::connections);
    const juce::Colour neutralPort (0xff5a6170);

    const auto portColour = [this, &connections, neutralPort] (const PortInfo& info) -> juce::Colour
    {
        const auto matches = [&info] (int connChannel)
        {
            return connChannel == info.channel
                || (info.span == 2 && connChannel == info.channel + 1);
        };

        for (int i = 0; i < connections.getNumChildren(); ++i)
        {
            const auto c = connections.getChild (i);

            if (info.isInput)
            {
                if (c.getProperty (id::destNodeId).toString() == info.nodeUuid
                    && matches ((int) c.getProperty (id::destChannel)))
                    return cableColourFor (c.getProperty (id::sourceNodeId).toString(),
                                           (int) c.getProperty (id::sourceChannel));
            }
            else if (c.getProperty (id::sourceNodeId).toString() == info.nodeUuid
                     && matches ((int) c.getProperty (id::sourceChannel)))
            {
                return cableColourFor (info.nodeUuid, info.channel);
            }
        }

        return neutralPort;  // unverbunden
    };

    for (auto& comp : nodeComponents)
        comp->applyPortSignalColours (portColour);

    repaint();  // Kabel folgen der neuen Farbe
}

void NodeCanvas::mouseDown (const juce::MouseEvent& event)
{
    // Klick auf ein Kabel trennt es (undo-fähig) — Klicks auf Kacheln/Ports
    // kommen hier nie an (Child-Components fangen sie ab)
    const auto connection = findConnectionAt (event.getPosition());
    if (! connection.isValid())
        return;

    const auto sourceUuid    = connection.getProperty (id::sourceNodeId).toString();
    const auto sourceChannel = (int) connection.getProperty (id::sourceChannel);
    const auto destUuid      = connection.getProperty (id::destNodeId).toString();
    const auto destChannel   = (int) connection.getProperty (id::destChannel);

    // Kabel eines Stereo-Paars: der Klick trennt BEIDE Linien (eine
    // Transaktion) — das Paar hängt an einem gemeinsamen Port
    if (const auto* sourceComponent = findNodeComponent (sourceUuid))
        if (const auto anchor = sourceComponent->pairAnchorForPort (false, sourceChannel))
        {
            const auto partnerSource = sourceChannel == *anchor ? *anchor + 1 : *anchor;
            const auto partnerDest   = destChannel + (partnerSource - sourceChannel);

            graphManager.removeConnectionPair (sourceUuid, sourceChannel,
                                               destUuid, destChannel,
                                               partnerSource, partnerDest);
            return;
        }

    graphManager.removeConnection (sourceUuid, sourceChannel, destUuid, destChannel);
}

void NodeCanvas::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Bis zur Modul-Palette: Doppelklick/Doppel-Tap legt einen Attenuator an
    const auto created = graphManager.addModuleNode (AttenuatorModule::staticModuleId,
                                                     event.getPosition());
    jassertquiet (created.isValid());
}

//==============================================================================
bool NodeCanvas::isInterestedInDragSource (const SourceDetails& details)
{
    return browser_drag::extractFactoryKey (details.description.toString()).isNotEmpty();
}

void NodeCanvas::itemDropped (const SourceDetails& details)
{
    dropHighlight = false;
    repaint();

    const auto factoryKey = browser_drag::extractFactoryKey (details.description.toString());

    // localPosition ist bereits Canvas-lokal — derselbe undo-fähige Pfad
    // wie Tap-to-Load ("Modul hinzufügen"-Transaktion)
    const auto created = graphManager.addModuleNode (factoryKey, details.localPosition);
    jassertquiet (created.isValid());
}

void NodeCanvas::itemDragEnter (const SourceDetails&)
{
    dropHighlight = true;
    repaint();
}

void NodeCanvas::itemDragExit (const SourceDetails&)
{
    dropHighlight = false;
    repaint();
}

} // namespace conduit
