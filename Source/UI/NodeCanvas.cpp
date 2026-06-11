#include "NodeCanvas.h"

#include <algorithm>

#include "Modules/AttenuatorModule.h"

namespace conduit
{

NodeCanvas::NodeCanvas (juce::ValueTree rootTree,
                        GraphManager& graphManagerToUse,
                        NodeUiRegistry& uiRegistryToUse)
    : rootState (std::move (rootTree)),
      graphManager (graphManagerToUse),
      uiRegistry (uiRegistryToUse)
{
    rootState.addListener (this);
    rebuildAll();  // Tree kann schon Nodes tragen (Session-Restore)
}

NodeCanvas::~NodeCanvas()
{
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

    auto component = std::make_unique<NodeComponent> (nodeTree, graphManager, uiRegistry);

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
    // Undo-Restore-Fall: Subtree kam mit nodeState == Deleting zurück,
    // der Graph-Swap setzt ihn auf Active — jetzt UI nachziehen.
    if (property == id::nodeState && tree.hasType (id::node)
        && tree.getProperty (id::nodeState).toString() == toString (NodeState::active))
        addComponentFor (tree);
}

void NodeCanvas::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (id::nodes) && child.hasType (id::node))
        addComponentFor (child);
    else if (child.hasType (id::nodes))
        rebuildAll();  // Container-Austausch (Preset-Load)
}

void NodeCanvas::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    // Fallback für Entfernung ohne Deleting-Phase (Redo, Preset-Load):
    // sofort zerstören — der reguläre Delete-Pfad hat die Component an
    // dieser Stelle bereits über den Teardown abgebaut.
    if (parent.hasType (id::nodes) && child.hasType (id::node))
        removeComponentFor (child.getProperty (id::nodeId).toString());
    else if (child.hasType (id::nodes))
        rebuildAll();
}

void NodeCanvas::valueTreeRedirected (juce::ValueTree&)
{
    rebuildAll();
}

//==============================================================================
void NodeCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d21));

    // Dezentes Patch-Grid
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    constexpr int gridSize = 24;

    for (int x = gridSize; x < getWidth(); x += gridSize)
        g.drawVerticalLine (x, 0.0f, static_cast<float> (getHeight()));

    for (int y = gridSize; y < getHeight(); y += gridSize)
        g.drawHorizontalLine (y, 0.0f, static_cast<float> (getWidth()));
}

void NodeCanvas::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Bis zur Modul-Palette: Doppelklick/Doppel-Tap legt einen Attenuator an
    const auto created = graphManager.addModuleNode (AttenuatorModule::staticModuleId,
                                                     event.getPosition());
    jassertquiet (created.isValid());
}

} // namespace conduit
