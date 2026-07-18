#include "NodeCanvas.h"

#include <algorithm>
#include <cmath>

#include "Core/PageManager.h"
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
                        UiSettings* uiSettingsToUse,
                        PageManager* pageManagerToUse)
    : rootState (std::move (rootTree)),
      graphManager (graphManagerToUse),
      uiRegistry (uiRegistryToUse),
      channelNames (channelNamesToUse),
      inputLevels (inputLevelsToUse),
      outputLevels (outputLevelsToUse),
      inputSend (inputSendToUse),
      uiSettings (uiSettingsToUse),
      pageManager (pageManagerToUse)
{
    rootState.addListener (this);

    // Input-Kanal-Farben leben in ChannelNames (App-Zustand) — Änderungen
    // dort müssen die Kabel neu einfärben (M-B)
    if (channelNames != nullptr)
        channelNames->addChangeListener (this);

    // Interaktions-Sperr-Schwelle folgt dem Dev-Tuning-Wert live
    if (uiSettings != nullptr)
        uiSettings->addChangeListener (this);

    // Transform-Träger (ADR 008 M3a): einzige direkte Kind-Component, alle
    // NodeComponents leben darin; Kabel zeichnet der Canvas in ihrem Kontext
    addAndMakeVisible (content);
    content.onPaintCables = [this] (juce::Graphics& g) { paintCables (g); };

    // Ebene 2: Pinch/Pan kontinuierlich; Gesten-Ende persistiert den Viewport.
    // Ebenen 3/4/5 (onLevelBegin/End) bleiben in M3a bewusst ungesetzt.
    recognizer.onPinchPan = [this] (double scaleFactor, juce::Point<double> panDelta,
                                    juce::Point<double> anchor)
    {
        view = canvas_view::applyPinch (view, scaleFactor, panDelta, anchor);
        applyViewTransform();
    };
    recognizer.onGestureEnd = [this] { persistViewState(); };

    // Ebene 4 (M3b): Seiten-Swipe — Peek-Versatz während der Geste,
    // Commit/Snap-back am Ende. Ebenen 3/5 bleiben für M4 reserviert.
    recognizer.onLevelBegin = [this] (int fingers)
    {
        if (fingers == 4)
        {
            swipeActive = true;
            swipeDelta = {};
        }
    };
    recognizer.onLevelDrag = [this] (int fingers, juce::Point<double> delta)
    {
        if (fingers == 4 && swipeActive)
        {
            swipeDelta += delta;
            applyViewTransform();   // Peek: Content folgt dem Wisch
        }
    };
    recognizer.onLevelEnd = [this] (int fingers)
    {
        if (fingers == 4 && swipeActive)
            handleSwipeEnd();
    };

    applyRecognizerTuning();

    rebuildAll();  // Tree kann schon Nodes tragen (Session-Restore)
    restoreViewState();
}

NodeCanvas::~NodeCanvas()
{
    if (uiSettings != nullptr)
        uiSettings->removeChangeListener (this);

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

    // Seiten-Filter (M3b): nur die aktive Seite rendert — Cross-Page-Kabel
    // bleiben bis M5 (Portal-Badges) unsichtbar, weil getPortCentreInCanvas
    // für gefilterte Nodes nullopt liefert
    if (! isOnActivePage (nodeTree))
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

    content.addAndMakeVisible (*component);   // Kinder leben im Transform-Träger
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

    // Seitenwechsel (M3b): navigatePages/M4-Übersicht setzen die
    // activePage-Property — EIN Pfad für alle Auslöser
    if (property == id::activePage && tree == rootState)
    {
        rebuildAll();
        restoreViewState();
        return;
    }

    // Node wandert auf eine andere Seite (setNodePage): Component folgt
    if (property == id::pageUuid && tree.hasType (id::node))
    {
        if (isOnActivePage (tree))
            addComponentFor (tree);
        else
            removeComponentFor (tree.getProperty (id::nodeId).toString());

        refreshFlowColours();
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
    else if (child.hasType (id::pages))
        restoreViewState();  // Preset-Load/Migration bringt den Pages-Zweig
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
    restoreViewState();
}

void NodeCanvas::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // Dev-Tuning (ADR 008 M3a): Sperr-Schwelle + Pinch-Schwelle
    if (uiSettings != nullptr && source == uiSettings)
    {
        applyRecognizerTuning();
        applyViewTransform();
        return;
    }

    // ChannelNames-Farbe/Pairing geändert → Vererbung + Input-Kabel neu
    refreshFlowColours();
}

//==============================================================================
void NodeCanvas::beginCableDrag (const PortInfo& fromPort, juce::Point<int> position)
{
    // API nimmt Canvas-Koordinaten (PortComponent-Konvention); die Vorschau
    // zeichnet im Content → intern Content-Koordinaten (M3a)
    activeCableDrag = CableDrag { fromPort, toContentPosition (position) };
    repaint();
}

void NodeCanvas::updateCableDrag (juce::Point<int> position)
{
    if (activeCableDrag)
    {
        activeCableDrag->currentPosition = toContentPosition (position);
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

    const auto contentPosition = toContentPosition (position);

    // Drop-Toleranz screen-konstant halten: Content-Einheiten skalieren
    // invers zum Zoom (bei Zoom 1.0 unverändert touchTarget)
    const auto tolerance = (int) std::ceil ((double) NodeComponent::touchTarget
                                            / juce::jmax (0.1, view.zoom));

    for (const auto& component : nodeComponents)
    {
        const auto* port = component->findPortNear (contentPosition - component->getPosition(),
                                                    tolerance);

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

    // API nimmt Canvas-Koordinaten; Kabel-Pfade leben in Content-Koordinaten.
    // Toleranz screen-konstant: 8 px / Zoom (bei Identität unverändert 8).
    const auto target = toContentPosition (position).toFloat();
    const auto tolerance = (float) (8.0 / juce::jmax (0.1, view.zoom));

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

        if (nearestOnPath.getDistanceFrom (target) <= tolerance)
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
    // Hintergrund + Drop-Rahmen bleiben UNTRANSFORMIERT am Canvas — die
    // Kabel zeichnet paintCables() im Content-Kontext (Transform, M3a)
    g.fillAll (push::colours::background);

    if (dropHighlight)
    {
        g.setColour (push::colours::ledOrange.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 2);
    }
}

void NodeCanvas::paintOverChildren (juce::Graphics& g)
{
    // Swipe-Badge (M3b): Zielseite des laufenden 4-Finger-Swipes — das
    // Live-Peek der Nachbar-MODULE kommt mit den M4-Miniaturen
    if (! swipeActive || pageManager == nullptr)
        return;

    const auto thresholdX = pageSwipeCommitFraction * juce::jmax (1, getWidth());
    const auto thresholdY = pageSwipeCommitFraction * juce::jmax (1, getHeight());
    const auto horizontal = std::abs (swipeDelta.x) >= std::abs (swipeDelta.y);
    const auto committed  = horizontal ? std::abs (swipeDelta.x) > thresholdX
                                       : std::abs (swipeDelta.y) > thresholdY;

    const auto dx = horizontal ? (swipeDelta.x < 0 ? 1 : -1) : 0;
    const auto dy = horizontal ? 0 : (swipeDelta.y < 0 ? 1 : -1);

    const auto currentUuid = pageManager->getActivePageUuid();
    const auto neighbour = pageManager->neighbourPage (currentUuid, dx, dy);
    const auto current = pageManager->findPageByUuid (currentUuid);

    const auto targetLabel = neighbour.isValid()
        ? "Seite (" + neighbour.getProperty (id::pageGridX).toString() + ", "
              + neighbour.getProperty (id::pageGridY).toString() + ")"
        : juce::String ("Neue Seite (")
              + juce::String ((int) current.getProperty (id::pageGridX) + dx) + ", "
              + juce::String ((int) current.getProperty (id::pageGridY) + dy) + ")";

    const auto badge = juce::Rectangle<int> (0, 0, 260, 44)
                           .withCentre ({ getWidth() / 2, 60 });

    g.setColour (push::colours::background.withAlpha (committed ? 0.95f : 0.75f));
    g.fillRoundedRectangle (badge.toFloat(), 8.0f);
    g.setColour (committed ? push::colours::ledGreen
                           : juce::Colours::white.withAlpha (0.6f));
    g.drawRoundedRectangle (badge.toFloat(), 8.0f, 2.0f);
    g.setFont (juce::Font (juce::FontOptions (16.0f)));
    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92 ") + targetLabel,
                badge, juce::Justification::centred);
}

void NodeCanvas::paintCables (juce::Graphics& g)
{
    // Kabel aus Connections[] (Schema 6.2) — unter den Node-Kacheln, jedes
    // in der Farbe seiner Quelle (M-B); Koordinaten == Node-Positionen
    // (Content-lokal), der Content-Transform übernimmt Zoom/Pan
    const juce::PathStrokeType cableStroke (3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded);
    const auto connectionsTree = rootState.getChildWithName (id::connections);

    // Clip-Culling (UI-Framerate 14.07.2026): Meter-/Marker-Frames
    // invalidieren kleine Ausschnitte bis zu 120x pro Sekunde — Kabel,
    // deren Hülle den Clip nicht schneidet, werden gar nicht erst gebaut/
    // gestroket (Pfad-Stroking ist der teure Teil dieses paint()).
    const auto clipBounds = g.getClipBounds().toFloat();

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
            // Hülle des Bezier: y bleibt zwischen den Endpunkten (horizontale
            // Tangenten), x ragt maximal `bend` über sie hinaus (makeCablePath).
            const auto startF = start->toFloat();
            const auto endF   = end->toFloat();
            const auto bend   = juce::jmax (40.0f, std::abs (endF.x - startF.x) * 0.5f);

            if (! clipBounds.intersects (juce::Rectangle<float> (startF, endF)
                                             .expanded (bend + 4.0f, 4.0f)))
                continue;

            g.setColour (cableColourFor (sourceUuid, sourceChannel));
            g.strokePath (makeCablePath (startF, endF), cableStroke);
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
    // Touch auf dem Canvas-Hintergrund → Gesten-Leiter (Leerraum-Regel:
    // Touches auf Modulen kommen hier nie an, JUCE-Hit-Testing)
    if (event.source.isTouch())
    {
        recognizer.touchDown (event.source.getIndex(), event.position);

        // Ab dem zweiten Finger keine Klick-Semantik mehr (Pinch-Absicht)
        if (recognizer.getActiveFingerCount() > 1)
            return;
    }

    // Mittlere Maustaste = Pan (Gesten-Parität, ADR 008)
    if (event.mods.isMiddleButtonDown())
    {
        panDragLast = event.position;
        return;
    }

    // Interaktions-Sperre (User-Feedback 18.07.2026): unterhalb der
    // Zoom-Schwelle ist der Patch nur Navigation — auch Kabel-Trennen aus
    if (isInteractionLocked())
        return;

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

void NodeCanvas::mouseDrag (const juce::MouseEvent& event)
{
    if (event.source.isTouch())
    {
        recognizer.touchMove (event.source.getIndex(), event.position);
        return;
    }

    if (panDragLast)
    {
        view.offsetX += (double) (event.position.x - panDragLast->x);
        view.offsetY += (double) (event.position.y - panDragLast->y);
        panDragLast = event.position;
        applyViewTransform();
    }
}

void NodeCanvas::mouseUp (const juce::MouseEvent& event)
{
    if (event.source.isTouch())
    {
        recognizer.touchUp (event.source.getIndex());
        return;
    }

    if (panDragLast)
    {
        panDragLast.reset();
        persistViewState();
    }
}

void NodeCanvas::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Gesperrt = nur Navigation — auch kein Modul-Anlegen (18.07.2026)
    if (isInteractionLocked())
        return;

    // Bis zur Modul-Palette: Doppelklick/Doppel-Tap legt einen Attenuator an
    // (Position im Content-/Tree-Koordinatenraum, M3a)
    const auto created = graphManager.addModuleNode (AttenuatorModule::staticModuleId,
                                                     toContentPosition (event.getPosition()));
    jassertquiet (created.isValid());
}

void NodeCanvas::mouseWheelMove (const juce::MouseEvent& event,
                                 const juce::MouseWheelDetails& wheel)
{
    // Alt+Scroll = Seitenwechsel (Ebene-4-Parität, M3b): akkumuliert bis
    // zur Schwelle; Scroll links (negativ) = Seite rechts (wie der Wisch)
    if (event.mods.isAltDown())
    {
        const auto delta = std::abs ((double) wheel.deltaX) >= std::abs ((double) wheel.deltaY)
            ? (double) wheel.deltaX
            : (double) wheel.deltaY;

        // Richtungswechsel setzt den Akku zurück (kein Alt-Reste-Wischen)
        if (delta * wheelSwipeAccum < 0.0)
            wheelSwipeAccum = 0.0;

        wheelSwipeAccum += delta;

        if (std::abs (wheelSwipeAccum) >= 0.5)
        {
            navigatePages (wheelSwipeAccum < 0 ? 1 : -1, 0);
            wheelSwipeAccum = 0.0;
        }

        return;
    }

    // Cmd/Ctrl+Scroll = Zoom um den Zeiger; sonst Pan (Trackpad-2-Finger-
    // Scroll und Mausrad — Gesten-Parität Ebene 2, ADR 008)
    if (event.mods.isCommandDown())
    {
        const auto factor = std::exp ((double) wheel.deltaY * 1.2);
        view = canvas_view::zoomAbout (view, event.position.toDouble(),
                                       view.zoom * factor);
    }
    else
    {
        constexpr auto pixelsPerWheelUnit = 160.0;
        view.offsetX += (double) wheel.deltaX * pixelsPerWheelUnit;
        view.offsetY += (double) wheel.deltaY * pixelsPerWheelUnit;
    }

    applyViewTransform();
    persistViewState();   // diskrete Events → direkt schreiben
}

void NodeCanvas::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    // Trackpad-Pinch (macOS Magnify / Windows Precision Touchpad)
    view = canvas_view::zoomAbout (view, event.position.toDouble(),
                                   view.zoom * (double) scaleFactor);
    applyViewTransform();
    persistViewState();
}

//==============================================================================
void NodeCanvas::setViewState (canvas_view::ViewState newView)
{
    newView.zoom = canvas_view::clampZoom (newView.zoom);
    view = newView;
    applyViewTransform();
    persistViewState();
}

bool NodeCanvas::isInteractionLocked() const noexcept
{
    const auto threshold = uiSettings != nullptr
        ? uiSettings->getInteractionMinZoom()
        : UiSettings::defaultInteractionMinZoom;

    return view.zoom < (double) threshold - 1.0e-9;
}

void NodeCanvas::applyViewTransform()
{
    // Translation auf ganze Screen-Pixel gerundet ANWENDEN — view selbst
    // bleibt double-genau (sonst verschluckt die Rundung kleine Deltas).
    // Sub-Pixel-Offsets ließen Kacheln/Text beim Pannen zwischen Pixeln
    // zittern (Smoke-Feedback 18.07.2026). Während des 4-Finger-Swipes
    // kommt der transiente Peek-Versatz obendrauf (M3b).
    const auto peekX = swipeActive ? swipeDelta.x : 0.0;
    const auto peekY = swipeActive ? swipeDelta.y : 0.0;

    content.setTransform (juce::AffineTransform::scale ((float) view.zoom)
                              .translated ((float) std::round (view.offsetX + peekX),
                                           (float) std::round (view.offsetY + peekY)));

    // Interaktions-Sperre (User-Entscheidung 18.07.2026): unterhalb der
    // Schwelle sind Module reine Navigationsziele
    content.setChildInteraction (! isInteractionLocked());
    repaint();
}

void NodeCanvas::persistViewState()
{
    auto page = activePageTree();

    if (! page.isValid())
        return;   // Tests ohne Pages-Zweig — Viewport bleibt transient

    // Ohne Undo — View-State (Muster Node-Drag: kein Undo-Spam)
    page.setProperty (id::viewOffsetX, view.offsetX, nullptr);
    page.setProperty (id::viewOffsetY, view.offsetY, nullptr);
    page.setProperty (id::viewZoom,    view.zoom,    nullptr);
}

void NodeCanvas::restoreViewState()
{
    const auto page = activePageTree();

    if (page.isValid())
    {
        view.offsetX = (double) page.getProperty (id::viewOffsetX, 0.0);
        view.offsetY = (double) page.getProperty (id::viewOffsetY, 0.0);
        view.zoom    = canvas_view::clampZoom ((double) page.getProperty (id::viewZoom, 1.0));
    }
    else
    {
        view = {};
    }

    applyViewTransform();
}

juce::ValueTree NodeCanvas::activePageTree()
{
    if (pageManager != nullptr)
        return pageManager->findPageByUuid (pageManager->getActivePageUuid());

    // Fallback ohne PageManager (Tests): Pages[0] wie in M3a
    const auto pages = rootState.getChildWithName (id::pages);
    return pages.getNumChildren() > 0 ? pages.getChild (0) : juce::ValueTree();
}

bool NodeCanvas::isOnActivePage (const juce::ValueTree& nodeTree)
{
    if (pageManager == nullptr)
        return true;   // Alt-Rigs ohne Seiten-Konzept

    const auto nodePage = PageManager::pageOf (nodeTree);
    return nodePage.isEmpty() || nodePage == pageManager->getActivePageUuid();
}

void NodeCanvas::navigatePages (int dx, int dy)
{
    if (pageManager == nullptr || (dx == 0 && dy == 0))
        return;

    const auto currentUuid = pageManager->getActivePageUuid();
    const auto currentPage = pageManager->findPageByUuid (currentUuid);
    const auto neighbour = pageManager->neighbourPage (currentUuid, dx, dy);

    // Ziel existiert nicht → anlegen (undo-fähig; paritätisch zum Wisch
    // ins Leere, ADR 008)
    const auto targetUuid = neighbour.isValid()
        ? neighbour.getProperty (id::pageUuid).toString()
        : pageManager->createPage ((int) currentPage.getProperty (id::pageGridX) + dx,
                                   (int) currentPage.getProperty (id::pageGridY) + dy);

    persistViewState();   // Viewport der ALTEN Seite sichern
    pageManager->setActivePage (targetUuid);
    // Rebuild + Viewport-Restore laufen über den activePage-Listener
}

void NodeCanvas::handleSwipeEnd()
{
    const auto delta = swipeDelta;
    swipeActive = false;
    swipeDelta = {};

    const auto thresholdX = pageSwipeCommitFraction * juce::jmax (1, getWidth());
    const auto thresholdY = pageSwipeCommitFraction * juce::jmax (1, getHeight());

    // Dominante Achse entscheidet; Content folgt dem Finger — Wisch nach
    // links (delta negativ) zeigt die Seite RECHTS (gridX+1)
    if (std::abs (delta.x) >= std::abs (delta.y) && std::abs (delta.x) > thresholdX)
        navigatePages (delta.x < 0 ? 1 : -1, 0);
    else if (std::abs (delta.y) > thresholdY)
        navigatePages (0, delta.y < 0 ? 1 : -1);
    else
        applyViewTransform();   // Snap-back (Peek-Versatz fällt weg)

    repaint();
}

juce::Point<int> NodeCanvas::toContentPosition (juce::Point<int> canvasPosition) const
{
    return canvas_view::toContent (view, canvasPosition.toDouble()).roundToInt();
}

void NodeCanvas::applyRecognizerTuning()
{
    // Pinch-Schwelle (Dev-Tuning, User-Feedback 18.07.2026): Prozent
    // Spread-Änderung → Log-Einheiten (log1p hält kleine Werte äquivalent)
    const auto fraction = uiSettings != nullptr ? uiSettings->getPinchDeadZone()
                                                : UiSettings::defaultPinchDeadZone;
    recognizer.setZoomDeadZone (std::log1p ((double) fraction));

    // Zoom-Antwort: Gesamt-Stärke + progressive Kurve (Dev-Tuning)
    const auto gain = uiSettings != nullptr ? uiSettings->getZoomStrength()
                                            : UiSettings::defaultZoomStrength;
    const auto curve = uiSettings != nullptr ? uiSettings->getZoomCurve()
                                             : UiSettings::defaultZoomCurve;
    recognizer.setZoomResponse ((double) gain, (double) curve);

    // Gesten-Glättung gegen Touch-Sensor-Rauschen (Dev-Tuning)
    const auto smoothing = uiSettings != nullptr ? uiSettings->getGestureSmoothing()
                                                 : UiSettings::defaultGestureSmoothing;
    recognizer.setSmoothing ((double) smoothing);
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

    // localPosition ist Canvas-lokal → in den Content-/Tree-Koordinatenraum
    // (M3a); derselbe undo-fähige Pfad wie Tap-to-Load
    const auto created = graphManager.addModuleNode (factoryKey,
                                                     toContentPosition (details.localPosition));
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
