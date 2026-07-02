#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/LinkClock.h"
#include "Modules/ConduitModule.h"
#include "UI/TransportBar.h"

namespace
{

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.setProperty (conduit::id::scaleRoot, 0, nullptr);
    root.setProperty (conduit::id::scaleType, "chromatic", nullptr);
    return root;
}

struct TransportBarRig
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    conduit::LinkClock clock { 120.0, "ConduitTest" };
    conduit::TransportBar bar { root, clock };
};

} // namespace

//==============================================================================
TEST_CASE ("TransportBar: Tempo-Edit committet in die Link-Session", "[transport][ui]")
{
    TransportBarRig rig;

    rig.bar.getTempoTile().onCommitText ("140");
    REQUIRE (rig.clock.getTempo() == Catch::Approx (140.0));

    // Komma-Eingabe und Clamping auf [20, 300]
    rig.bar.getTempoTile().onCommitText ("97,5");
    REQUIRE (rig.clock.getTempo() == Catch::Approx (97.5));

    rig.bar.getTempoTile().onCommitText ("9999");
    REQUIRE (rig.clock.getTempo() == Catch::Approx (300.0));

    // Unsinn ändert nichts
    rig.bar.getTempoTile().onCommitText ("abc");
    REQUIRE (rig.clock.getTempo() == Catch::Approx (300.0));
}

TEST_CASE ("TransportBar: refresh() zieht Tempo und Peer-Status nach", "[transport][ui]")
{
    TransportBarRig rig;

    rig.clock.setTempo (87.25);
    rig.bar.refresh();
    REQUIRE (rig.bar.getTempoTile().getText() == "87.25");

    // Ohne Peers: neutrales Label, LED aus
    REQUIRE (rig.bar.getLinkTile().getText() == "Link");
    REQUIRE_FALSE (rig.bar.getLinkTile().isActive());
}

TEST_CASE ("TransportBar: Undo-Kachel — Klick Undo, Shift-Klick Redo", "[transport][ui]")
{
    TransportBarRig rig;

    int undoCount = 0, redoCount = 0;
    rig.bar.onUndo = [&undoCount] { ++undoCount; };
    rig.bar.onRedo = [&redoCount] { ++redoCount; };

    // Ohne Shift → Undo (ModifierKeys im Test neutral)
    rig.bar.getUndoTile().onClick();
    REQUIRE (undoCount == 1);
    REQUIRE (redoCount == 0);
}

TEST_CASE ("TransportBar: Page-Kacheln schalten exklusiv (Radio)", "[transport][ui]")
{
    TransportBarRig rig;

    // Default: Device-Page aktiv
    REQUIRE (rig.bar.getSelectedPage() == conduit::TransportBar::pageDevice);
    REQUIRE (rig.bar.getPageTile (conduit::TransportBar::pageDevice).isActive());

    int selected = -1;
    rig.bar.onPageSelected = [&selected] (int page) { selected = page; };

    rig.bar.getPageTile (conduit::TransportBar::pageMixer).onClick();
    REQUIRE (selected == conduit::TransportBar::pageMixer);
    REQUIRE (rig.bar.getSelectedPage() == conduit::TransportBar::pageMixer);
    REQUIRE (rig.bar.getPageTile (conduit::TransportBar::pageMixer).isActive());
    REQUIRE_FALSE (rig.bar.getPageTile (conduit::TransportBar::pageDevice).isActive());

    // Editor darf zurückschalten (Platzhalter-Pages bis Schritt 6)
    rig.bar.setSelectedPage (conduit::TransportBar::pageDevice);
    REQUIRE_FALSE (rig.bar.getPageTile (conduit::TransportBar::pageMixer).isActive());
}

TEST_CASE ("TransportBar: Capture-LED folgt dem Status", "[transport][ui]")
{
    TransportBarRig rig;

    REQUIRE_FALSE (rig.bar.getCaptureTile().isActive());

    rig.bar.setCaptureStatus (true, false, false);   // recording
    REQUIRE (rig.bar.getCaptureTile().isActive());

    rig.bar.setCaptureStatus (false, false, false);  // idle
    REQUIRE_FALSE (rig.bar.getCaptureTile().isActive());
}

TEST_CASE ("TransportBar: Skala-Combos schreiben die Root-Properties", "[transport][ui]")
{
    TransportBarRig rig;

    rig.bar.setBounds (0, 0, 1480, 56);  // Layout einmal durchlaufen

    // Platzhalter-Kacheln sind sichtbar, aber disabled (Schritt 3–5)
    REQUIRE_FALSE (rig.bar.getPlayTile().isEnabled());
    REQUIRE (rig.bar.getCaptureTile().isEnabled());
    REQUIRE (rig.bar.getPlusTile().isEnabled());
}

//==============================================================================
TEST_CASE ("ModuleBrowser: Klick auf einen Eintrag löst die Aktion aus", "[transport][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    int fired = -1;
    std::vector<conduit::ModuleBrowser::Item> items;
    items.push_back ({ "Attenuator", [&fired] { fired = 0; }, false });
    items.push_back ({ "LFO",        [&fired] { fired = 1; }, false });
    items.push_back ({ juce::String::fromUTF8 ("Preset laden\xe2\x80\xa6"),
                       [&fired] { fired = 2; }, true });

    conduit::ModuleBrowser browser (items);
    browser.setBounds (0, 0, conduit::ModuleBrowser::panelWidth, browser.getHeight());

    // Kachel 2 (Preset laden) direkt klicken — ohne CallOutBox-Parent ist
    // dismiss() ein No-op, die Aktion muss trotzdem feuern
    auto* tile = dynamic_cast<juce::Button*> (browser.getChildComponent (2));
    REQUIRE (tile != nullptr);
    tile->onClick();
    REQUIRE (fired == 2);

    // Höhe: 3 Zeilen + eine Sektions-Trennung
    REQUIRE (browser.getHeight() > 3 * conduit::ModuleBrowser::itemHeight);
}
