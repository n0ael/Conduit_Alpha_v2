#include <catch2/catch_test_macros.hpp>

#include "UI/GridPage.h"
#include "UI/TrackFocusBadge.h"
#include "UI/TrackSelectorPanel.h"

namespace
{

[[nodiscard]] juce::var parse (const char* json)
{
    auto result = juce::JSON::parse (juce::String::fromUTF8 (json));
    REQUIRE (result.getDynamicObject() != nullptr);
    return result;
}

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

    conduit::TrackSelectorPanel panel (model, true);
    REQUIRE (panel.getHeight() == conduit::TrackSelectorPanel::kTitleHeight
                                      + conduit::TrackSelectorPanel::kFollowHeight
                                      + conduit::TrackSelectorPanel::kRowHeight);
}

TEST_CASE ("TrackSelectorPanel: makeMidiInputFocusCommand baut das v2-Wire-Format", "[grid][trackselect]")
{
    const auto message = conduit::TrackSelectorPanel::makeMidiInputFocusCommand (
        "tr:7", "Conduit Grid MPE", "FromPush", true);

    REQUIRE (message.getAddressPattern().toString() == "/live/song/set/midi_input_focus");
    REQUIRE (message.size() == 4);
    REQUIRE (message[0].getString() == "tr:7");
    REQUIRE (message[1].getString() == "Conduit Grid MPE");
    REQUIRE (message[2].getString() == "FromPush");
    REQUIRE (message[3].getInt32() == 1);
}

TEST_CASE ("TrackSelectorPanel: makeFollowCommand baut das Wire-Format", "[grid][trackselect]")
{
    const auto onMessage = conduit::TrackSelectorPanel::makeFollowCommand (true);
    REQUIRE (onMessage.getAddressPattern().toString() == "/live/song/set/midi_input_follow");
    REQUIRE (onMessage.size() == 1);
    REQUIRE (onMessage[0].getInt32() == 1);

    REQUIRE (conduit::TrackSelectorPanel::makeFollowCommand (false)[0].getInt32() == 0);
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
