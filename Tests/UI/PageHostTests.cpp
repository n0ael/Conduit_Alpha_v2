#include <catch2/catch_test_macros.hpp>

#include "UI/PageHost.h"

//==============================================================================
TEST_CASE ("PageHost: Device ist default, Umschalten zeigt genau eine Page", "[pages][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    juce::Component devicePage;  // steht für die NodeCanvas
    conduit::PageHost host (devicePage);
    host.setBounds (0, 0, 800, 600);

    // Default: Device sichtbar (Index 3 == TransportBar::pageDevice)
    REQUIRE (host.getPage() == 3);
    REQUIRE (devicePage.isVisible());

    // Grid (0): Device verschwindet, genau ein Platzhalter sichtbar
    host.setPage (0);
    REQUIRE_FALSE (devicePage.isVisible());

    int visiblePlaceholders = 0;
    for (int i = 0; i < host.getNumChildComponents(); ++i)
        if (host.getChildComponent (i) != &devicePage && host.getChildComponent (i)->isVisible())
            ++visiblePlaceholders;

    REQUIRE (visiblePlaceholders == 1);

    // Zurück zu Device
    host.setPage (3);
    REQUIRE (devicePage.isVisible());
    REQUIRE (devicePage.getBounds() == host.getLocalBounds());

    // Out-of-Range wird geclamped
    host.setPage (99);
    REQUIRE (host.getPage() == 3);
}
