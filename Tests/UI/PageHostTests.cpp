#include <catch2/catch_test_macros.hpp>

#include "UI/PageHost.h"
#include "UI/TransportBar.h"

//==============================================================================
TEST_CASE ("PageHost: Device ist default, Umschalten zeigt genau eine Page", "[pages][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    juce::Component devicePage;  // steht für die NodeCanvas
    juce::Component looperPage;  // steht für die LooperPage (B3)
    juce::Component gridPage;    // steht für die GridPage (M1 Teil 3)
    conduit::PageHost host (devicePage, looperPage, gridPage);
    host.setBounds (0, 0, 800, 600);

    // Default: Device sichtbar (Index 3 == TransportBar::pageDevice)
    REQUIRE (host.getPage() == conduit::TransportBar::pageDevice);
    REQUIRE (devicePage.isVisible());
    REQUIRE_FALSE (looperPage.isVisible());

    // Grid (0): Device verschwindet, Grid-Page sichtbar
    host.setPage (0);
    REQUIRE_FALSE (devicePage.isVisible());
    REQUIRE (gridPage.isVisible());

    int visiblePlaceholders = 0;
    for (int i = 0; i < host.getNumChildComponents(); ++i)
    {
        auto* child = host.getChildComponent (i);
        if (child != &devicePage && child != &looperPage && child != &gridPage && child->isVisible())
            ++visiblePlaceholders;
    }

    REQUIRE (visiblePlaceholders == 0);

    // Zurück zu Device
    host.setPage (conduit::TransportBar::pageDevice);
    REQUIRE (devicePage.isVisible());
    REQUIRE (devicePage.getBounds() == host.getLocalBounds());
}

TEST_CASE ("PageHost: Looper-Page (B3) hinter der Tape-Kachel", "[pages][ui][looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    juce::Component devicePage;
    juce::Component looperPage;
    juce::Component gridPage;
    conduit::PageHost host (devicePage, looperPage, gridPage);
    host.setBounds (0, 0, 800, 600);

    // Looper einblenden: Device weg, Looper füllt den Host
    host.setPage (conduit::TransportBar::pageLooper);
    REQUIRE (host.getPage() == conduit::TransportBar::pageLooper);
    REQUIRE (looperPage.isVisible());
    REQUIRE_FALSE (devicePage.isVisible());
    REQUIRE (looperPage.getBounds() == host.getLocalBounds());

    // Page-Icon-Wechsel verlässt den Looper
    host.setPage (conduit::TransportBar::pageDevice);
    REQUIRE_FALSE (looperPage.isVisible());
    REQUIRE (devicePage.isVisible());

    // Out-of-Range wird auf die letzte Page (Looper) geclamped
    host.setPage (99);
    REQUIRE (host.getPage() == conduit::TransportBar::pageLooper);
    host.setPage (-3);
    REQUIRE (host.getPage() == conduit::TransportBar::pageGrid);
}
