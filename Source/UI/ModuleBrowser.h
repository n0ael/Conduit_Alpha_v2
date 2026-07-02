#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Browser hinter dem Push-„+" (CLAUDE.md 10): dunkles CallOutBox-Panel mit
    Einträgen zum Anlegen von Modulen plus Preset-Aktionen — ersetzt die
    bisherige Modul-Button-Leiste der Toolbar.

    Die Einträge kommen von außen (EngineEditor baut die Liste aus der
    ModuleFactory und den Preset-Choosern) — der Browser kennt weder
    GraphManager noch Engine. Ein Klick führt die Aktion aus und schließt
    die CallOutBox. Touch-Targets: 40-px-Zeilen über volle Panel-Breite.
*/
class ModuleBrowser final : public juce::Component
{
public:
    struct Item
    {
        juce::String label;
        std::function<void()> action;
        bool startsSection = false;  // zeichnet eine Trennlinie darüber
    };

    explicit ModuleBrowser (std::vector<Item> itemsToShow);

    void resized() override;
    void paint (juce::Graphics& g) override;

    static constexpr int itemHeight  = 40;
    static constexpr int panelWidth  = 230;

private:
    std::vector<Item> items;
    juce::OwnedArray<push::TextTile> tiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleBrowser)
};

} // namespace conduit
