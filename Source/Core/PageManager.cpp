#include "Core/PageManager.h"

namespace conduit
{

PageManager::PageManager (juce::ValueTree rootTree, juce::UndoManager& undoManagerToUse)
    : rootState (std::move (rootTree)),
      undoManager (undoManagerToUse)
{
    jassert (rootState.hasType (id::root));
}

//==============================================================================
juce::ValueTree PageManager::pagesContainer() const
{
    return rootState.getChildWithName (id::pages);
}

juce::ValueTree PageManager::makePage (int gridX, int gridY) const
{
    juce::ValueTree pageTree (id::page);
    pageTree.setProperty (id::pageUuid,    juce::Uuid().toString(), nullptr);
    pageTree.setProperty (id::pageGridX,   gridX,                   nullptr);
    pageTree.setProperty (id::pageGridY,   gridY,                   nullptr);
    pageTree.setProperty (id::viewOffsetX, 0.0,                     nullptr);
    pageTree.setProperty (id::viewOffsetY, 0.0,                     nullptr);
    pageTree.setProperty (id::viewZoom,    1.0,                     nullptr);
    return pageTree;
}

//==============================================================================
juce::String PageManager::ensureDefaultPage()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Grundausstattung — kein Undo (Muster ensureIONodeStates)
    auto container = pagesContainer();

    if (! container.isValid())
    {
        container = juce::ValueTree (id::pages);
        rootState.appendChild (container, nullptr);
    }

    if (const auto existing = findPage (0, 0); existing.isValid())
        return existing.getProperty (id::pageUuid).toString();

    if (container.getNumChildren() > 0)  // (0,0) fehlt, aber Seiten existieren
        return container.getChild (0).getProperty (id::pageUuid).toString();

    auto pageTree = makePage (0, 0);
    container.appendChild (pageTree, nullptr);
    return pageTree.getProperty (id::pageUuid).toString();
}

juce::String PageManager::getActivePageUuid()
{
    // M1: keine UI, keine aktive-Seite-Auswahl — immer die Default-Seite.
    return ensureDefaultPage();
}

//==============================================================================
juce::String PageManager::createPage (int gridX, int gridY)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (const auto existing = findPage (gridX, gridY); existing.isValid())
        return existing.getProperty (id::pageUuid).toString();

    ensureDefaultPage();  // garantiert den Pages-Zweig (undo-frei)

    auto pageTree = makePage (gridX, gridY);

    undoManager.beginNewTransaction ("Seite anlegen");
    pagesContainer().appendChild (pageTree, &undoManager);
    return pageTree.getProperty (id::pageUuid).toString();
}

juce::ValueTree PageManager::findPage (int gridX, int gridY) const
{
    const auto container = pagesContainer();

    for (int i = 0; i < container.getNumChildren(); ++i)
    {
        auto pageTree = container.getChild (i);

        if ((int) pageTree.getProperty (id::pageGridX) == gridX
            && (int) pageTree.getProperty (id::pageGridY) == gridY)
            return pageTree;
    }

    return {};
}

juce::ValueTree PageManager::findPageByUuid (const juce::String& uuid) const
{
    return pagesContainer().getChildWithProperty (id::pageUuid, uuid);
}

juce::String PageManager::pageOf (const juce::ValueTree& nodeTree)
{
    return nodeTree.getProperty (id::pageUuid).toString();
}

bool PageManager::setNodePage (const juce::String& nodeUuid, const juce::String& targetPageUuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto nodeTree = rootState.getChildWithName (id::nodes)
                        .getChildWithProperty (id::nodeId, nodeUuid);

    if (! nodeTree.isValid() || ! findPageByUuid (targetPageUuid).isValid())
        return false;

    // Reines setProperty — NIE removeChild/addChild (ADR 008: sonst feuern
    // Delete-Pfad und OSC-Deregistrierung fälschlich)
    undoManager.beginNewTransaction ("Seite wechseln");
    nodeTree.setProperty (id::pageUuid, targetPageUuid, &undoManager);
    return true;
}

bool PageManager::canDeletePage (const juce::String& uuid) const
{
    if (! findPageByUuid (uuid).isValid())
        return false;

    // Regel a: nur leere Seiten sind löschbar
    return ! rootState.getChildWithName (id::nodes)
                .getChildWithProperty (id::pageUuid, uuid).isValid();
}

bool PageManager::deletePage (const juce::String& uuid)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! canDeletePage (uuid))
        return false;

    auto container = pagesContainer();

    undoManager.beginNewTransaction ("Seite löschen");
    container.removeChild (findPageByUuid (uuid), &undoManager);
    return true;
}

//==============================================================================
void PageManager::migrateAndRepair()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Komplett undo-frei — die Migration erscheint NICHT in der Undo-History.
    const auto defaultPage = ensureDefaultPage();

    // Nodes ohne pageUuid nachziehen (Bestandspatch bzw. von
    // ensureIONodeStates nach dem Laden ergänzte I/O-Endpunkte)
    auto nodesTree = rootState.getChildWithName (id::nodes);

    for (int i = 0; i < nodesTree.getNumChildren(); ++i)
    {
        auto nodeTree = nodesTree.getChild (i);

        if (! nodeTree.hasProperty (id::pageUuid))
            nodeTree.setProperty (id::pageUuid, defaultPage, nullptr);
    }

    if ((int) rootState.getProperty (id::rootStateVersion, 1) < pagesRootVersion)
        rootState.setProperty (id::rootStateVersion, pagesRootVersion, nullptr);
}

} // namespace conduit
