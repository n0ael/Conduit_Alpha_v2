#include "ModuleBrowser.h"

#include <algorithm>

namespace conduit
{

ModuleBrowser::ModuleBrowser (std::vector<Item> itemsToShow)
    : items (std::move (itemsToShow))
{
    for (const auto& item : items)
    {
        auto* tile = tiles.add (std::make_unique<push::TextTile> (item.label));

        tile->onClick = [this, action = item.action]
        {
            if (action != nullptr)
                action();

            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        };

        addAndMakeVisible (tile);
    }

    const auto sections = (int) std::count_if (items.begin(), items.end(),
                                               [] (const Item& item) { return item.startsSection; });

    setSize (panelWidth, (int) items.size() * (itemHeight + 4) + sections * 9 + 8);
}

void ModuleBrowser::resized()
{
    auto bounds = getLocalBounds().reduced (6, 6);

    for (int i = 0; i < tiles.size(); ++i)
    {
        if (items[(size_t) i].startsSection && i > 0)
            bounds.removeFromTop (9);  // Platz für die Trennlinie

        tiles[i]->setBounds (bounds.removeFromTop (itemHeight));
        bounds.removeFromTop (4);
    }
}

void ModuleBrowser::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);

    g.setColour (push::colours::outline);

    for (int i = 1; i < tiles.size(); ++i)
        if (items[(size_t) i].startsSection)
        {
            const auto y = (float) tiles[i]->getY() - 5.0f;
            g.drawLine (8.0f, y, (float) getWidth() - 8.0f, y, 1.0f);
        }
}

} // namespace conduit
