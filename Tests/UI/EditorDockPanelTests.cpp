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
