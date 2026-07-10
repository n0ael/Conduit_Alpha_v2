#include <catch2/catch_test_macros.hpp>

#include "UI/EditorDockPanel.h"

namespace
{
    struct ProbeComponent final : public juce::Component {};
}

//==============================================================================
TEST_CASE ("EditorDockPanel: addTab/setActiveTab zeigt genau einen Content sichtbar", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;

    auto first  = std::make_unique<ProbeComponent>();
    auto second = std::make_unique<ProbeComponent>();
    auto* firstPtr  = first.get();
    auto* secondPtr = second.get();

    panel.addTab ("a", "A", std::move (first));
    panel.addTab ("b", "B", std::move (second));

    // Erster hinzugefügter Tab wird automatisch aktiv
    REQUIRE (firstPtr->isVisible());
    REQUIRE_FALSE (secondPtr->isVisible());

    panel.setActiveTab ("b");
    REQUIRE_FALSE (firstPtr->isVisible());
    REQUIRE (secondPtr->isVisible());
}

TEST_CASE ("EditorDockPanel: geschlossen liefert getPreferredWidth 0", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;
    panel.setPanelWidth (300);
    panel.setPanelOpen (false);

    REQUIRE (panel.getPreferredWidth() == 0);

    panel.setPanelOpen (true);
    REQUIRE (panel.getPreferredWidth() == 300);
}

TEST_CASE ("EditorDockPanel: onActiveTabChanged feuert nur bei tatsaechlichem Wechsel", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;

    juce::StringArray fired;
    panel.onActiveTabChanged = [&fired] (const juce::String& id) { fired.add (id); };

    // Auto-Aktivierung des ersten Tabs feuert (Callback ist schon gesetzt)
    panel.addTab ("a", "A", std::make_unique<ProbeComponent>());
    REQUIRE (fired.size() == 1);
    REQUIRE (fired[0] == "a");
    REQUIRE (panel.getActiveTabId() == "a");

    // Zweiter Tab aktiviert sich nicht selbst
    panel.addTab ("b", "B", std::make_unique<ProbeComponent>());
    REQUIRE (fired.size() == 1);

    panel.setActiveTab ("b");
    REQUIRE (fired.size() == 2);
    REQUIRE (fired[1] == "b");
    REQUIRE (panel.getActiveTabId() == "b");

    // Gleicher Tab erneut / unbekannte id: kein Callback, kein Wechsel
    panel.setActiveTab ("b");
    panel.setActiveTab ("unbekannt");
    REQUIRE (fired.size() == 2);
    REQUIRE (panel.getActiveTabId() == "b");
}

TEST_CASE ("EditorDockPanel: setPanelWidth klemmt auf [kMinWidth, kMaxWidth]", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;
    panel.setPanelOpen (true);

    panel.setPanelWidth (10000);
    REQUIRE (panel.getPreferredWidth() == conduit::EditorDockPanel::kMaxWidth);

    panel.setPanelWidth (-500);
    REQUIRE (panel.getPreferredWidth() == conduit::EditorDockPanel::kMinWidth);
}
