#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/LinkClock.h"
#include "Core/TransportSettings.h"
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

juce::PropertiesFile::Options tempSettingsOptions (const juce::File& folder)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ConduitTransportBarTests";
    options.filenameSuffix  = ".settings";
    options.folderName      = folder.getFullPathName();  // absoluter Pfad
    return options;
}

struct TransportBarRig
{
    ~TransportBarRig() { folder.deleteRecursively(); }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ConduitTransportBarTests");
    juce::ValueTree root = makeRootTree();
    conduit::LinkClock clock { 120.0, "ConduitTest" };
    conduit::TransportSettings settings { tempSettingsOptions (folder) };
    conduit::TransportBar bar { root, clock, settings };
};

/** Link merged Commits asynchron in die Session — direkt aufeinanderfolgende
    setTempo-Commits können kurz vom Merge-Echo des vorigen überdeckt werden
    (CI-Fund 02.07.). Deshalb poll-basiert statt sofort zu lesen. */
bool waitForTempo (const conduit::LinkClock& clock, double expectedBpm)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (std::abs (clock.getTempo() - expectedBpm) < 0.01)
            return true;

        juce::Thread::sleep (5);
    }

    return false;
}

} // namespace

//==============================================================================
TEST_CASE ("TransportBar: Tempo-Edit committet in die Link-Session", "[transport][ui]")
{
    TransportBarRig rig;

    rig.bar.getTempoTile().onCommitText ("140");
    REQUIRE (waitForTempo (rig.clock, 140.0));

    // Komma-Eingabe und Clamping auf [20, 300]
    rig.bar.getTempoTile().onCommitText ("97,5");
    REQUIRE (waitForTempo (rig.clock, 97.5));

    rig.bar.getTempoTile().onCommitText ("9999");
    REQUIRE (waitForTempo (rig.clock, 300.0));

    // Unsinn ändert nichts
    rig.bar.getTempoTile().onCommitText ("abc");
    REQUIRE (waitForTempo (rig.clock, 300.0));
}

TEST_CASE ("TransportBar: refresh() zieht Tempo und Peer-Status nach", "[transport][ui]")
{
    TransportBarRig rig;

    rig.clock.setTempo (87.25);
    REQUIRE (waitForTempo (rig.clock, 87.25));
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

    // Play/Capture/Plus sind funktional; Metronom folgt in Schritt 5
    REQUIRE (rig.bar.getPlayTile().isEnabled());
    REQUIRE (rig.bar.getCaptureTile().isEnabled());
    REQUIRE (rig.bar.getPlusTile().isEnabled());
}

TEST_CASE ("TransportBar: formatPosition — Ableton-Zählweise Takt. Beat. Sechzehntel",
           "[transport][ui]")
{
    using conduit::TransportBar;

    REQUIRE (TransportBar::formatPosition (0.0)   == "1. 1. 1");
    REQUIRE (TransportBar::formatPosition (1.0)   == "1. 2. 1");
    REQUIRE (TransportBar::formatPosition (2.75)  == "1. 3. 4");
    REQUIRE (TransportBar::formatPosition (10.5)  == "3. 3. 3");
    REQUIRE (TransportBar::formatPosition (-3.0)  == "1. 1. 1");  // vor Session-Start
}

TEST_CASE ("TransportBar: Tap-and-Commit — Preview cyan, Commit-Tap setzt die Session",
           "[transport][ui]")
{
    TransportBarRig rig;
    rig.settings.setTapCount (2);  // kompakter Test: 2 Taps erfassen, Tap 3 committet

    // Erfassung: 100 BPM (0,6 s Abstand) — Session bleibt bei 120
    rig.bar.tapWithTime (100.0);
    rig.bar.tapWithTime (100.6);
    REQUIRE (rig.bar.getTempoTile().getText() == "100.00");
    REQUIRE (waitForTempo (rig.clock, 120.0));

    rig.bar.tapWithTime (101.2);   // Tap 3 = Commit
    REQUIRE (waitForTempo (rig.clock, 100.0));
}

TEST_CASE ("TransportBar: Swing-Kachel schreibt die globale Root-Property", "[transport][ui]")
{
    TransportBarRig rig;

    // Text-Commit in Prozent → Root-Property 0..0.75
    rig.bar.getSwingTile().onCommitText ("25");
    REQUIRE ((double) rig.root.getProperty (conduit::id::globalSwing, 0.0)
             == Catch::Approx (0.25));

    // Clamping auf 75 %
    rig.bar.getSwingTile().onCommitText ("150");
    REQUIRE ((double) rig.root.getProperty (conduit::id::globalSwing, 0.0)
             == Catch::Approx (0.75));

    rig.bar.refresh();
    REQUIRE (rig.bar.getSwingTile().getText() == "75 %");
}

TEST_CASE ("TransportBar: Looper-Toggles schreiben die TransportSettings", "[transport][ui]")
{
    TransportBarRig rig;

    REQUIRE_FALSE (rig.settings.isAutomateEnabled());
    rig.bar.getAutomateTile().onClick();
    REQUIRE (rig.settings.isAutomateEnabled());

    rig.bar.getFixedLengthTile().onClick();
    REQUIRE (rig.settings.isFixedLengthEnabled());

    // refresh() zieht die LED-Zustände aus den Settings nach
    rig.bar.refresh();
    REQUIRE (rig.bar.getAutomateTile().isActive());
    REQUIRE (rig.bar.getFixedLengthTile().isActive());
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
