#include <catch2/catch_test_macros.hpp>

#include "UI/GridPage.h"
#include "UI/MasterDeviceSwitch.h"
#include "UI/TrackFocusBadge.h"
#include "UI/TrackSelectorPanel.h"
#include "UI/TrackTabsStrip.h"

namespace
{

[[nodiscard]] juce::var parse (const char* json)
{
    auto result = juce::JSON::parse (juce::String::fromUTF8 (json));
    REQUIRE (result.getDynamicObject() != nullptr);
    return result;
}

/** Temp-Optionen für GridPanelSettings (Muster ConduitColorPickerTests). */
struct TempSettingsFolder
{
    TempSettingsFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitTrackTabsTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettingsFolder() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitTrackTabsTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();
        return o;
    }

    juce::File folder;
};

} // namespace

//==============================================================================
TEST_CASE ("TrackSelectorPanel: midiTrackRowsFrom filtert und sortiert MIDI-Tracks", "[grid][trackselect]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    // Live-Reihenfolge absichtlich verdreht (index), Audio/Return/Master
    // muessen komplett rausfallen
    model.applySnapshot ("tracks",
        parse (R"({"tr:1":{"name":"Bass","color":255,"kind":"midi","index":2},)"
               R"("tr:2":{"name":"Drums","color":16711680,"kind":"audio","index":0},)"
               R"("tr:3":{"name":"Keys","color":65280,"kind":"midi","index":1},)"
               R"("rt:1":{"name":"Reverb","color":0,"kind":"return","index":0},)"
               R"("ma:1":{"name":"Master","color":0,"kind":"master","index":0}})"));

    const auto rows = conduit::TrackSelectorPanel::midiTrackRowsFrom (model);

    REQUIRE (rows.size() == 2);
    REQUIRE (rows[0].name == "Keys");
    REQUIRE (rows[0].key == "tr:3");
    REQUIRE (rows[0].colour == juce::Colour (0xff00ff00));
    REQUIRE (rows[1].name == "Bass");
    REQUIRE (rows[1].key == "tr:1");
    REQUIRE (rows[1].colour == juce::Colour (0xff0000ff));
}

TEST_CASE ("TrackSelectorPanel: leeres Modell ergibt leere Liste (kein Crash)", "[grid][trackselect]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    REQUIRE (conduit::TrackSelectorPanel::midiTrackRowsFrom (model).empty());
    REQUIRE (conduit::TrackSelectorPanel::focusKeyFrom (model).isEmpty());

    conduit::TrackSelectorPanel panel (model);
    REQUIRE (panel.getHeight() == conduit::TrackSelectorPanel::kTitleHeight
                                      + conduit::TrackSelectorPanel::kRowHeight);
}

TEST_CASE ("TrackSelectorPanel: makeMidiInputFocusCommand baut das rev5-Wire-Format", "[grid][trackselect]")
{
    const auto message = conduit::TrackSelectorPanel::makeMidiInputFocusCommand (
        "tr:7", "Conduit Grid MPE", "FromPush", "FromPush;K1 (Port 1)");

    REQUIRE (message.getAddressPattern().toString() == "/live/song/set/midi_input_focus");
    REQUIRE (message.size() == 4);
    REQUIRE (message[0].getString() == "tr:7");
    REQUIRE (message[1].getString() == "Conduit Grid MPE");
    REQUIRE (message[2].getString() == "FromPush");
    REQUIRE (message[3].getString() == "FromPush;K1 (Port 1)");
}

TEST_CASE ("TrackSelectorPanel/TrackFocusBadge: conduit_focus wird aufgeloest", "[grid][trackselect]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("tracks",
        parse (R"({"tr:1":{"name":"Bass","color":255,"kind":"midi","index":0},)"
               R"("conduit_focus":"tr:1","selected":"tr:1",)"
               R"("input_options":["All Ins","Conduit Grid MPE","FromPush"]})"));

    REQUIRE (conduit::TrackSelectorPanel::focusKeyFrom (model) == "tr:1");

    const auto row = conduit::TrackFocusBadge::focusRowFrom (model);
    REQUIRE (row.key == "tr:1");
    REQUIRE (row.name == "Bass");
    REQUIRE (row.colour == juce::Colour (0xff0000ff));

    // Fokus auf unbekannten/verschwundenen Track -> leere Row (Badge aus)
    model.applySnapshot ("tracks",
        parse (R"({"tr:2":{"name":"Keys","color":0,"kind":"midi","index":0},)"
               R"("conduit_focus":"tr:1"})"));
    REQUIRE (conduit::TrackFocusBadge::focusRowFrom (model).key.isEmpty());
}

//==============================================================================
TEST_CASE ("GridPage: nextLayoutMode toggelt 64-Pad <-> XY+Fader", "[grid][trackselect]")
{
    using Mode = conduit::GridPanelSettings::GridLayoutMode;

    REQUIRE (conduit::GridPage::nextLayoutMode (Mode::fullPads) == Mode::xyFaders);
    REQUIRE (conduit::GridPage::nextLayoutMode (Mode::xyFaders) == Mode::fullPads);
}

//==============================================================================
TEST_CASE ("HoldIconTile: Tap feuert onClick, Long-Press unterdrueckt ihn", "[push][holdicon]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::push::HoldIconTile tile (conduit::push::Icon::gridMpe, "grid");
    int taps = 0, longPresses = 0;
    tile.onClick = [&] { ++taps; };
    tile.onLongPress = [&] { ++longPresses; };

    // Kurzer Tap innerhalb der Kachel
    tile.beginPress();
    tile.endPress (true);
    REQUIRE (taps == 1);
    REQUIRE (longPresses == 0);

    // Long-Press: Timeout feuert, das Loslassen ist danach KEIN Tap mehr
    tile.beginPress();
    tile.firePressTimeout();
    REQUIRE (longPresses == 1);
    tile.endPress (true);
    REQUIRE (taps == 1);

    // Timeout ohne aktiven Druck ist wirkungslos
    tile.firePressTimeout();
    REQUIRE (longPresses == 1);
}

TEST_CASE ("HoldIconTile: Loslassen ausserhalb ist kein Tap, Bewegung erlaubt Tap weiter", "[push][holdicon]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::push::HoldIconTile tile (conduit::push::Icon::gridMpe, "grid");
    int taps = 0, longPresses = 0;
    tile.onClick = [&] { ++taps; };
    tile.onLongPress = [&] { ++longPresses; };

    tile.beginPress();
    tile.endPress (false);   // Finger von der Kachel gezogen
    REQUIRE (taps == 0);

    // Bewegung ueber der Toleranz bricht nur den Long-Press ab -- der Tap
    // beim Loslassen innerhalb bleibt gueltig (Button-Semantik)
    tile.beginPress();
    tile.movePress ({ conduit::push::HoldIconTile::kMoveTolerancePx + 4, 0 });
    tile.endPress (true);
    REQUIRE (taps == 1);
    REQUIRE (longPresses == 0);
}

//==============================================================================
// Block H3: TrackTabsStrip + MasterDeviceSwitch + Favoriten

TEST_CASE ("TrackTabsStrip: Halten waehlt, schneller Tap nicht, Ziehen scrollt", "[grid][tracktabs]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;
    TempSettingsFolder temp;
    conduit::GridPanelSettings settings (temp.options());

    model.applySnapshot ("tracks",
        parse (R"({"tr:1":{"name":"Bass","color":255,"kind":"midi","index":0},)"
               R"("tr:2":{"name":"Keys","color":65280,"kind":"midi","index":1},)"
               R"("tr:3":{"name":"Audio","color":0,"kind":"audio","index":2}})"));

    conduit::TrackTabsStrip tabs (model, settings);
    tabs.setSize (400, 28);

    REQUIRE (tabs.tabCount() == 2);
    REQUIRE (tabs.tabIndexAt (10) == 0);
    REQUIRE (tabs.tabIndexAt (210) == 1);   // Tab-Breite 200 (400/2 < max 220)

    juce::StringArray chosen;
    tabs.onTrackChosen = [&] (const juce::String& key) { chosen.add (key); };

    // Schneller Tap: Loslassen VOR dem Timeout -> KEIN Wechsel (Runde 3)
    tabs.beginPress (10);
    tabs.endPress();
    REQUIRE (chosen.isEmpty());

    // Halten bis zum Timeout -> Wechsel
    tabs.beginPress (210);
    tabs.fireSelectTimeout();
    REQUIRE (chosen.strings.getLast() == "tr:2");
    tabs.endPress();

    // Ziehen ueber die Toleranz bricht die Auswahl ab (Timeout wirkungslos)
    chosen.clear();
    tabs.beginPress (10);
    tabs.movePress (conduit::TrackTabsStrip::kScrollTolerancePx + 4);
    tabs.fireSelectTimeout();
    REQUIRE (chosen.isEmpty());
    tabs.endPress();

    // refresh nach Domain-Update nimmt neue Tracks auf
    model.applySnapshot ("tracks",
        parse (R"({"tr:1":{"name":"Bass","color":255,"kind":"midi","index":0},)"
               R"("tr:2":{"name":"Keys","color":65280,"kind":"midi","index":1},)"
               R"("tr:4":{"name":"Pad","color":123,"kind":"midi","index":2}})"));
    tabs.refresh();
    REQUIRE (tabs.tabCount() == 3);
}

TEST_CASE ("TrackTabsStrip: Mindestbreite macht den Strip scrollbar", "[grid][tracktabs]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;
    TempSettingsFolder temp;
    conduit::GridPanelSettings settings (temp.options());

    // 4 MIDI-Tracks auf 200 px Breite mit 90 px Mindestbreite -> Inhalt
    // 360 px, Strip scrollt.
    model.applySnapshot ("tracks",
        parse (R"({"tr:1":{"name":"A","color":1,"kind":"midi","index":0},)"
               R"("tr:2":{"name":"B","color":2,"kind":"midi","index":1},)"
               R"("tr:3":{"name":"C","color":3,"kind":"midi","index":2},)"
               R"("tr:4":{"name":"D","color":4,"kind":"midi","index":3}})"));

    conduit::TrackTabsStrip tabs (model, settings);
    tabs.setMinTabWidth (90);
    tabs.setSize (200, 28);

    REQUIRE (tabs.tabIndexAt (10) == 0);

    tabs.beginPress (100);
    tabs.movePress (-120);   // nach links ziehen = Inhalt nach rechts scrollen
    REQUIRE (tabs.scrollOffset() == 120);
    tabs.movePress (-999);   // weit ueber das Ende -> geklemmt (360-200)
    REQUIRE (tabs.scrollOffset() == 160);
    tabs.endPress();

    // Index-Treffer beruecksichtigt den Scroll-Offset (x=10 -> 170 -> Tab 1)
    REQUIRE (tabs.tabIndexAt (10) == 1);
}

TEST_CASE ("MasterDeviceSwitch: Tap zykelt, Drag scrollt, Commit meldet", "[grid][masterswitch]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::MasterDeviceSwitch sw;
    sw.setFavourites ({ "FromPush", "K1 (Port 1)", "TouchOSC" });
    sw.setCurrent ("FromPush");

    juce::StringArray chosen;
    sw.onMasterChosen = [&] (const juce::String& name) { chosen.add (name); };

    // Tap = naechster Favorit (zyklisch)
    sw.beginGesture();
    sw.endGesture (true);
    REQUIRE (chosen.strings.getLast() == "K1 (Port 1)");
    REQUIRE (sw.currentName() == "K1 (Port 1)");

    // Drag: ein Schritt je 44 px nach oben, live sichtbar, Commit beim Ende
    sw.beginGesture();
    sw.dragGesture (-conduit::MasterDeviceSwitch::kPixelsPerStep);
    REQUIRE (sw.currentName() == "TouchOSC");
    sw.dragGesture (-2 * conduit::MasterDeviceSwitch::kPixelsPerStep);   // wrap
    REQUIRE (sw.currentName() == "FromPush");
    sw.endGesture (false);
    REQUIRE (chosen.strings.getLast() == "FromPush");

    // Tap vom letzten Eintrag wrappt an den Anfang
    sw.setCurrent ("TouchOSC");
    sw.beginGesture();
    sw.endGesture (true);
    REQUIRE (sw.currentName() == "FromPush");
}

TEST_CASE ("MasterDeviceSwitch: ohne Favoriten keine Bewegung", "[grid][masterswitch]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::MasterDeviceSwitch sw;
    sw.setCurrent ("FromPush");

    int commits = 0;
    sw.onMasterChosen = [&] (const juce::String&) { ++commits; };

    sw.beginGesture();
    sw.dragGesture (-200);
    sw.endGesture (true);

    REQUIRE (sw.currentName() == "FromPush");   // Liste leer -> unveraendert
    REQUIRE (commits == 1);                     // Commit meldet den Ist-Wert
}

