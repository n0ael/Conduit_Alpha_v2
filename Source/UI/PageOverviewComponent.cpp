#include "UI/PageOverviewComponent.h"

#include "Modules/ConduitModule.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

PageOverviewComponent::PageOverviewComponent (juce::ValueTree rootTree,
                                              PageManager& pageManagerToUse)
    : rootState (std::move (rootTree)),
      pageManager (pageManagerToUse)
{
    setWantsKeyboardFocus (true);   // Esc schließt
    rootState.addListener (this);

    // Miniatur-Neuaufbau VBlank-gesteuert, max. EINE pro Frame (Invariante:
    // NIE in paint) — der Tick endet von selbst, wenn nichts dirty ist
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this]
    {
        rebuildNextThumbnail();
    });

    rebuildLayout();
    markAllThumbnailsDirty();
}

PageOverviewComponent::~PageOverviewComponent()
{
    rootState.removeListener (this);
}

//==============================================================================
void PageOverviewComponent::rebuildLayout()
{
    tiles.clear();

    const auto pages = rootState.getChildWithName (id::pages);

    if (pages.getNumChildren() == 0)
        return;

    // Grid normalisieren (min-Koordinate → Ursprung)
    int minX = std::numeric_limits<int>::max(), minY = minX;

    for (int i = 0; i < pages.getNumChildren(); ++i)
    {
        minX = juce::jmin (minX, (int) pages.getChild (i).getProperty (id::pageGridX));
        minY = juce::jmin (minY, (int) pages.getChild (i).getProperty (id::pageGridY));
    }

    const auto activeUuid = pageManager.getActivePageUuid();

    for (int i = 0; i < pages.getNumChildren(); ++i)
    {
        const auto page = pages.getChild (i);
        const auto uuid = page.getProperty (id::pageUuid).toString();

        Tile tile;
        tile.pageUuid = uuid;
        tile.empty    = ! pageHasNodes (uuid);
        tile.active   = uuid == activeUuid;

        const auto column = (int) page.getProperty (id::pageGridX) - minX;
        const auto row    = (int) page.getProperty (id::pageGridY) - minY;
        tile.bounds = { 40 + column * (tileWidth + tileGap),
                        70 + row * (tileHeight + tileGap),
                        tileWidth, tileHeight };

        tiles.push_back (tile);
    }
}

bool PageOverviewComponent::pageHasNodes (const juce::String& pageUuid) const
{
    return rootState.getChildWithName (id::nodes)
               .getChildWithProperty (id::pageUuid, pageUuid).isValid();
}

juce::Rectangle<int> PageOverviewComponent::tileBoundsFor (const juce::String& pageUuid) const
{
    for (const auto& tile : tiles)
        if (tile.pageUuid == pageUuid)
            return tile.bounds;

    return {};
}

//==============================================================================
void PageOverviewComponent::markAllThumbnailsDirty()
{
    dirtyThumbnails.clear();

    const auto pages = rootState.getChildWithName (id::pages);

    for (int i = 0; i < pages.getNumChildren(); ++i)
        dirtyThumbnails.add (pages.getChild (i).getProperty (id::pageUuid).toString());
}

void PageOverviewComponent::markPageThumbnailDirty (const juce::String& pageUuid)
{
    if (pageUuid.isNotEmpty())
        dirtyThumbnails.addIfNotAlreadyThere (pageUuid);
}

void PageOverviewComponent::rebuildNextThumbnail()
{
    if (dirtyThumbnails.isEmpty())
        return;

    const auto uuid = dirtyThumbnails[0];
    dirtyThumbnails.remove (0);

    const auto page = pageManager.findPageByUuid (uuid);

    if (! page.isValid())
    {
        thumbnails.erase (uuid);   // Seite gelöscht → Cache-Eintrag weg
        return;
    }

    thumbnails[uuid] = renderThumbnail (page);
    repaint();
}

juce::Image PageOverviewComponent::renderThumbnail (const juce::ValueTree& pageTree) const
{
    // Schematischer Proxy (ADR 008): Node-Rechtecke + Kabellinien aus dem
    // Tree — andere Seiten haben keine live-Components. Läuft im
    // VBlank-Tick, nie in paint().
    juce::Image image (juce::Image::ARGB, tileWidth, tileHeight, true);
    juce::Graphics g (image);

    const auto pageUuid = pageTree.getProperty (id::pageUuid).toString();
    const auto nodes = rootState.getChildWithName (id::nodes);

    // Bounding-Box der Seite (Default-Kachelmaße als Näherung)
    juce::Rectangle<int> world;
    std::map<juce::String, juce::Rectangle<int>> nodeBounds;

    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        const auto node = nodes.getChild (i);

        if (node.getProperty (id::pageUuid).toString() != pageUuid)
            continue;

        const juce::Rectangle<int> bounds ((int) node.getProperty (id::positionX),
                                           (int) node.getProperty (id::positionY),
                                           168, 104);
        nodeBounds[node.getProperty (id::nodeId).toString()] = bounds;
        world = world.isEmpty() ? bounds : world.getUnion (bounds);
    }

    if (nodeBounds.empty())
        return image;   // leere Seite — Kachel bleibt frei (gedimmt gezeichnet)

    world = world.expanded (40);
    const auto scale = juce::jmin ((float) tileWidth / (float) world.getWidth(),
                                   (float) tileHeight / (float) world.getHeight(),
                                   0.35f);
    const auto toTile = [&world, scale] (juce::Point<int> p)
    {
        return juce::Point<float> (((float) (p.x - world.getX())) * scale
                                       + ((float) tileWidth - (float) world.getWidth() * scale) * 0.5f,
                                   ((float) (p.y - world.getY())) * scale
                                       + ((float) tileHeight - (float) world.getHeight() * scale) * 0.5f);
    };

    // Kabellinien unter den Kacheln
    g.setColour (juce::Colour (0xff8fd0a0).withAlpha (0.7f));
    const auto connections = rootState.getChildWithName (id::connections);

    for (int i = 0; i < connections.getNumChildren(); ++i)
    {
        const auto c = connections.getChild (i);
        const auto source = nodeBounds.find (c.getProperty (id::sourceNodeId).toString());
        const auto dest   = nodeBounds.find (c.getProperty (id::destNodeId).toString());

        if (source == nodeBounds.end() || dest == nodeBounds.end())
            continue;   // Cross-Page-Kabel: erst mit M5 sichtbar

        g.drawLine (juce::Line<float> (
                        toTile ({ source->second.getRight(), source->second.getCentreY() }),
                        toTile ({ dest->second.getX(), dest->second.getCentreY() })),
                    1.5f);
    }

    // Node-Rechtecke
    for (const auto& [uuid, bounds] : nodeBounds)
    {
        juce::ignoreUnused (uuid);
        const auto topLeft = toTile (bounds.getTopLeft());
        g.setColour (push::colours::tile.brighter (0.35f));
        g.fillRoundedRectangle (topLeft.x, topLeft.y,
                                (float) bounds.getWidth() * scale,
                                (float) bounds.getHeight() * scale, 2.0f);
    }

    return image;
}

//==============================================================================
void PageOverviewComponent::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background.withAlpha (0.93f));

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (push::scaledFont (18.0f));
    g.drawText (juce::String::fromUTF8 ("Seiten — Tippen springt, × löscht leere"),
                getLocalBounds().removeFromTop (56).reduced (40, 0),
                juce::Justification::centredLeft);

    for (const auto& tile : tiles)
    {
        const auto bounds = tile.bounds.toFloat();
        const auto alpha = tile.empty ? 0.35f : 1.0f;   // leere GEDIMMT

        g.setColour (push::colours::tile.withAlpha (alpha));
        g.fillRoundedRectangle (bounds, 6.0f);

        // Miniatur blitten — NIE hier bauen (VBlank-Cache, ADR-Invariante)
        if (const auto it = thumbnails.find (tile.pageUuid); it != thumbnails.end())
        {
            g.setOpacity (alpha);
            g.drawImageAt (it->second, tile.bounds.getX(), tile.bounds.getY());
            g.setOpacity (1.0f);
        }

        g.setColour (tile.active ? push::colours::ledGreen
                                 : juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds, 6.0f, tile.active ? 2.5f : 1.0f);

        // Regel a: × nur an leeren, nicht-aktiven Kacheln
        if (tile.empty && ! tile.active)
        {
            const juce::Rectangle<int> closeZone (tile.bounds.getRight() - 28,
                                                  tile.bounds.getY(), 28, 28);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (push::scaledFont (16.0f));
            g.drawText (juce::String::fromUTF8 ("\xc3\x97"), closeZone,
                        juce::Justification::centred);
        }
    }
}

void PageOverviewComponent::resized()
{
    rebuildLayout();
}

void PageOverviewComponent::mouseUp (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    for (const auto& tile : tiles)
    {
        if (! tile.bounds.contains (position))
            continue;

        // ×-Zone (oben rechts) leerer, nicht-aktiver Kacheln → Regel a
        const juce::Rectangle<int> closeZone (tile.bounds.getRight() - 28,
                                              tile.bounds.getY(), 28, 28);

        if (tile.empty && ! tile.active && closeZone.contains (position))
        {
            if (pageManager.deletePage (tile.pageUuid))
            {
                rebuildLayout();
                repaint();
            }
            return;
        }

        if (onPageChosen)
        {
            // Kopie auf den Stack: der Callback zerstört das Overlay (und
            // damit das function-Member samt Captures) — die laufende
            // Kopie bleibt gültig (UAF-Lektion 20.07.2026)
            const auto chosen = onPageChosen;
            chosen (tile.pageUuid);
        }
        return;
    }

    if (onDismiss)
    {
        const auto dismiss = onDismiss;
        dismiss();   // Tap auf den Hintergrund schließt
    }
}

bool PageOverviewComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onDismiss)
        {
            const auto dismiss = onDismiss;   // Stack-Kopie, s. mouseUp
            dismiss();
        }
        return true;
    }

    return false;
}

//==============================================================================
void PageOverviewComponent::valueTreePropertyChanged (juce::ValueTree& tree,
                                                      const juce::Identifier& property)
{
    if (tree.hasType (id::node)
        && (property == id::positionX || property == id::positionY
            || property == id::pageUuid))
    {
        markPageThumbnailDirty (tree.getProperty (id::pageUuid).toString());

        if (property == id::pageUuid)
        {
            rebuildLayout();      // empty-Status kann kippen
            markAllThumbnailsDirty();
        }
    }
    else if (property == id::activePage)
    {
        rebuildLayout();
        repaint();
    }
}

void PageOverviewComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (id::node) || child.hasType (id::connection)
        || child.hasType (id::page) || child.hasType (id::pages))
    {
        rebuildLayout();
        markAllThumbnailsDirty();
        repaint();
    }
}

void PageOverviewComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    valueTreeChildAdded (parent, child);   // gleiche Reaktion
}

void PageOverviewComponent::valueTreeRedirected (juce::ValueTree&)
{
    rebuildLayout();
    markAllThumbnailsDirty();
    repaint();
}

} // namespace conduit
