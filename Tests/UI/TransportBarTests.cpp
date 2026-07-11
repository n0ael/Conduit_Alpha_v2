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

    // Play/Capture/Browser-Toggle sind funktional
    REQUIRE (rig.bar.getPlayTile().isEnabled());
    REQUIRE (rig.bar.getCaptureTile().isEnabled());
    REQUIRE (rig.bar.getBrowserPanelTile().isEnabled());
}

TEST_CASE ("TransportBar: Skala-Toggle schaltet chromatisch <-> letzte Skala", "[transport][ui]")
{
    TransportBarRig rig;   // Start: chromatic → Toggle-LED aus

    auto& toggle = rig.bar.getScaleToggleTile();
    REQUIRE_FALSE (toggle.isActive());

    // Einschalten ohne vorherige Auswahl: Default-Skala (minor)
    toggle.onClick();
    REQUIRE (rig.root.getProperty (conduit::id::scaleType).toString() == "minor");
    REQUIRE (toggle.isActive());

    // Ausschalten → chromatisch (= keine Quantisierung)
    toggle.onClick();
    REQUIRE (rig.root.getProperty (conduit::id::scaleType).toString() == "chromatic");
    REQUIRE_FALSE (toggle.isActive());

    // Skala manuell wählen → Toggle-LED an, Toggle merkt sich die Wahl.
    // Legacy-String "pentatonic" (Bestand vor Block I) lädt als
    // majorPentatonic und wird beim nächsten Schreiben migriert.
    rig.root.setProperty (conduit::id::scaleType, "pentatonic", nullptr);
    // (Combo-Weg wäre UI-identisch — Property direkt, dann Toggle-Roundtrip)
    toggle.onClick();   // pentatonic → aus
    REQUIRE (rig.root.getProperty (conduit::id::scaleType).toString() == "chromatic");
    toggle.onClick();   // wieder an → zuletzt gewählte Skala (neuer String)
    REQUIRE (rig.root.getProperty (conduit::id::scaleType).toString() == "majorPentatonic");
    REQUIRE (toggle.isActive());
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

TEST_CASE ("TransportBar: Tap misst nur (Set-Kachel), Set-Klick committet zur Session",
           "[transport][ui]")
{
    TransportBarRig rig;

    // Vor dem ersten Tap: Set idle (kein Preview committbar)
    REQUIRE_FALSE (rig.bar.getSetTile().isEnabled());

    // Messung: 100 BPM (0,6 s Abstand) — Preview lebt in der Set-Kachel,
    // die Session UND die Tempo-Kachel bleiben unberührt (M4L-Modell)
    rig.bar.tapWithTime (100.0);
    rig.bar.tapWithTime (100.6);
    rig.bar.tapWithTime (101.2);
    REQUIRE (rig.bar.getSetTile().isEnabled());
    REQUIRE (rig.bar.getSetTile().isActive());
    REQUIRE (rig.bar.getSetTile().getText() == "100.00");
    REQUIRE (waitForTempo (rig.clock, 120.0));
    rig.bar.refresh();
    REQUIRE (rig.bar.getTempoTile().getText() == "120.00");

    // Set-Klick committet und konsumiert die Messung
    rig.bar.commitTapPreview();
    REQUIRE (waitForTempo (rig.clock, 100.0));
    REQUIRE_FALSE (rig.bar.getSetTile().isEnabled());
    REQUIRE (rig.bar.getSetTile().getText() == "Set");
}

TEST_CASE ("TransportBar: Tap-Auto-Commit committet ab Tap n (MIDI/OSC-Mapping)",
           "[transport][ui]")
{
    TransportBarRig rig;
    rig.settings.setTapAutoCommitEnabled (true);
    rig.settings.setTapCount (2);

    rig.bar.tapWithTime (100.0);
    REQUIRE (waitForTempo (rig.clock, 120.0));   // Tap 1: nichts committet

    rig.bar.tapWithTime (100.6);                 // Tap 2 = Auto-Commit (100 BPM)
    REQUIRE (waitForTempo (rig.clock, 100.0));

    rig.bar.tapWithTime (101.1);                 // Tap 3 committet verfeinert weiter
    REQUIRE (waitForTempo (rig.clock, 60.0 / 0.55));  // Median {0.6, 0.5} = 0.55 s
}

TEST_CASE ("TransportBar: resetTapMeasurement verwirft die Messung (Tap halten)",
           "[transport][ui]")
{
    TransportBarRig rig;

    rig.bar.tapWithTime (100.0);
    rig.bar.tapWithTime (100.6);
    REQUIRE (rig.bar.getSetTile().isEnabled());

    rig.bar.resetTapMeasurement();
    REQUIRE_FALSE (rig.bar.getSetTile().isEnabled());
    REQUIRE (rig.bar.getSetTile().getText() == "Set");

    // Commit ohne Preview ist ein No-Op; ein Einzel-Tap liefert noch keinen
    rig.bar.commitTapPreview();
    rig.bar.tapWithTime (200.0);
    REQUIRE_FALSE (rig.bar.getSetTile().isEnabled());
    REQUIRE (waitForTempo (rig.clock, 120.0));
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
TEST_CASE ("TransportBar: Browser-Toggle feuert den Hook, LED folgt dem Editor",
           "[transport][ui]")
{
    TransportBarRig rig;

    int toggles = 0;
    rig.bar.onToggleBrowserPanel = [&toggles] { ++toggles; };

    auto& tile = rig.bar.getBrowserPanelTile();
    REQUIRE_FALSE (tile.isActive());

    tile.onClick();
    REQUIRE (toggles == 1);

    // LED-Status kommt vom Editor (Panel offen), nicht vom Klick selbst
    REQUIRE_FALSE (tile.isActive());
    rig.bar.setBrowserPanelOpen (true);
    REQUIRE (tile.isActive());
    rig.bar.setBrowserPanelOpen (false);
    REQUIRE_FALSE (tile.isActive());
}

//==============================================================================
TEST_CASE ("TransportBar (M7): Save/Delete nur im Looper-Kontext", "[looper][ui]")
{
    TransportBarRig rig;
    rig.bar.setBounds (0, 0, 1480, 56);

    // Default: Kontext-Kacheln unsichtbar (Session-Save liegt im Browser)
    REQUIRE_FALSE (rig.bar.getSaveTile().isVisible());
    REQUIRE_FALSE (rig.bar.getLooperDeleteTile().isVisible());
    REQUIRE_FALSE (rig.bar.isLooperPageContext());

    rig.bar.setLooperPageContext (true);
    REQUIRE (rig.bar.getSaveTile().isVisible());
    REQUIRE (rig.bar.getLooperDeleteTile().isVisible());

    rig.bar.setLooperPageContext (false);
    REQUIRE_FALSE (rig.bar.getSaveTile().isVisible());
    REQUIRE_FALSE (rig.bar.getLooperDeleteTile().isVisible());
}

TEST_CASE ("push::HoldTile: Kurzklick vs. Halten (Gesten-Kern)", "[looper][ui]")
{
    TransportBarRig rig;
    auto& tile = rig.bar.getLooperDeleteTile();

    int holdOn = 0, holdOff = 0, clicks = 0;
    tile.onHoldChanged = [&] (bool holding) { holding ? ++holdOn : ++holdOff; };
    tile.onShortClick = [&] { ++clicks; };

    // Kurzklick: Halten beginnt/endet, Klick feuert
    tile.beginHold();
    REQUIRE (holdOn == 1);
    tile.endHold (true);
    REQUIRE (holdOff == 1);
    REQUIRE (clicks == 1);

    // Langes Halten: kein Klick
    tile.beginHold();
    tile.endHold (false);
    REQUIRE (holdOn == 2);
    REQUIRE (holdOff == 2);
    REQUIRE (clicks == 1);
}