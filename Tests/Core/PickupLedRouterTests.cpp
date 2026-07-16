#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <tuple>

#include "Core/PickupLedRouter.h"

using namespace conduit::midirig;
namespace grid = conduit::grid;

namespace
{
    /** Mini-K1: eine Status-Gruppe (Push + Knob + Fader mit Detail-Pads),
        zwei freie Pads (Detail-Ziele), ein Shift-Pad und ein einfaches
        Control ohne Gruppe -- alles ueber den echten CSV-Parser (Roundtrip
        der M6-Schema-Erweiterung inklusive). */
    ControllerProfile testProfile()
    {
        const auto csv = juce::String (
            "device,id,type,group,send_kind,send_channel,send_number,"
            "feedback1_kind,feedback1_channel,feedback1_number,feedback1_meaning,"
            "feedback2_kind,feedback2_channel,feedback2_number,feedback2_meaning,"
            "feedback3_kind,feedback3_channel,feedback3_number,feedback3_meaning\n"
            "TestK1,push1,pad,col1,note,1,52,note,1,52,status_red,note,1,88,status_amber,note,1,124,status_green\n"
            "TestK1,knob1,encoder,col1,cc,1,4,note,1,36,led_pickup,,,,,,,,\n"
            "TestK1,fader1,fader,col1,cc,1,16,note,1,24,led_pickup,,,,,,,,\n"
            "TestK1,pad_a,pad,,note,1,36,note,1,36,led,note,1,72,led_layer_b,note,1,108,led_layer_c\n"
            "TestK1,pad_m,pad,,note,1,24,note,1,24,led,note,1,60,led_layer_b,note,1,96,led_layer_c\n"
            "TestK1,shiftpad,pad,,note,1,12,note,1,12,led,note,1,48,led_layer_b,note,1,84,led_layer_c\n"
            "TestK1,solo,knob,,cc,1,30,note,1,40,led_pickup,,,,,,,,\n"
            // M7: zwei Spalten-Pads (group col1) fuer den Ebenen-Blink.
            "TestK1,strippad1,pad,col1,note,1,100,note,1,100,led,note,1,101,led_layer_b,note,1,102,led_layer_c\n"
            "TestK1,strippad2,pad,col1,note,1,103,note,1,103,led,note,1,104,led_layer_b,note,1,105,led_layer_c\n");

        ControllerParseReport report;
        const auto profile = parseControllerProfileCsv (csv, &report);
        REQUIRE (report.accepted == 9);
        return profile;
    }

    struct RouterRig
    {
        PickupLedRouter router;
        std::vector<std::tuple<bool, int, int>> sent;      // isNote, number, value
        std::set<std::pair<bool, int>> bound;              // gebundene Send-Adressen
        std::map<std::pair<bool, int>, int> echo;          // Echo-Cache (Restore)

        RouterRig()
        {
            router.send = [this] (bool isNote, int number, int value)
            { sent.emplace_back (isNote, number, value); };

            router.isAddressBound = [this] (bool isNote, int number)
            { return bound.count ({ isNote, number }) > 0; };

            router.lastEchoValueFor = [this] (bool isNote, int number)
            {
                const auto it = echo.find ({ isNote, number });
                return it != echo.end() ? it->second : -1;
            };

            router.setProfile (testProfile());
        }

        void ticks (int n) { for (int i = 0; i < n; ++i) router.tick(); }

        /** Letzter an (isNote, number) gesendeter Wert, -1 = nie gesendet. */
        [[nodiscard]] int lastValueFor (bool isNote, int number) const
        {
            for (auto it = sent.rbegin(); it != sent.rend(); ++it)
                if (std::get<0> (*it) == isNote && std::get<1> (*it) == number)
                    return std::get<2> (*it);
            return -1;
        }

        [[nodiscard]] int sendCountFor (bool isNote, int number) const
        {
            int count = 0;
            for (const auto& entry : sent)
                if (std::get<0> (entry) == isNote && std::get<1> (entry) == number)
                    ++count;
            return count;
        }
    };

    grid::MidiInBindings::PickupState waitingState (float distance,
                                                    grid::ModifierSet modifiers = {},
                                                    bool activeRecently = false)
    {
        grid::MidiInBindings::PickupState state;
        state.waiting        = true;
        state.distance01     = distance;
        state.modifiers      = std::move (modifiers);
        state.activeRecently = activeRecently;
        return state;
    }

    grid::MidiInBindings::PickupState notWaiting()
    {
        return {};
    }

    // Shift-Ebene wartet (Richtung ueber physicalAbove: true = verringern/rot).
    grid::MidiInBindings::PickupState shiftWaiting (grid::ModifierSet modifiers,
                                                    bool physicalAbove,
                                                    float distance = 0.4f)
    {
        grid::MidiInBindings::PickupState state;
        state.waiting       = true;
        state.distance01    = distance;
        state.modifiers     = std::move (modifiers);
        state.physicalAbove = physicalAbove;
        return state;
    }

    // Shift-Ebene abgeholt (gruen), Pad noch gehalten.
    grid::MidiInBindings::PickupState shiftAligned (grid::ModifierSet modifiers)
    {
        grid::MidiInBindings::PickupState state;
        state.aligned   = true;
        state.modifiers = std::move (modifiers);
        return state;
    }
}

//==============================================================================
TEST_CASE ("PickupLedRouter: Gruppen-Status -- aus/gruen/orange/rot (Fader dominiert)", "[midirig]")
{
    RouterRig rig;

    // Nichts gebunden: alle Status-Farben aus.
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 52) == 0);
    CHECK (rig.lastValueFor (true, 88) == 0);
    CHECK (rig.lastValueFor (true, 124) == 0);

    // Knob gebunden, nichts wartet: gruen.
    rig.bound.insert ({ false, 4 });
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 124) == PickupLedRouter::kLedOn);

    // Knob wartet (ohne Bewegung): orange solid, gruen aus.
    rig.router.updatePickupState ({ 1, 4, false }, waitingState (0.4f));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 88) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 124) == 0);

    // Zusaetzlich wartet der Fader: rot dominiert, orange aus.
    rig.router.updatePickupState ({ 1, 16, false }, waitingState (0.6f));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 52) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 88) == 0);

    // Fader abgeholt, Knob abgeholt: wieder gruen.
    rig.router.updatePickupState ({ 1, 16, false }, notWaiting());
    rig.router.updatePickupState ({ 1, 4, false }, notWaiting());
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 52) == 0);
    CHECK (rig.lastValueFor (true, 124) == PickupLedRouter::kLedOn);
}

TEST_CASE ("PickupLedRouter: Dreh-Blink distanz-kodiert (nah = schnell), solid nach Pickup", "[midirig]")
{
    // Nahe Distanz blinkt schneller als ferne: Toggle-Zaehlung ueber
    // dieselbe Tick-Zahl vergleichen.
    const auto togglesFor = [] (float distance)
    {
        RouterRig rig;
        rig.bound.insert ({ false, 16 });
        rig.router.updatePickupState ({ 1, 16, false },
                                      waitingState (distance, {}, true));   // aktiv gedreht
        rig.ticks (60);
        return rig.sendCountFor (true, 52);   // jeder Phasenwechsel = 1 Send (Dedupe)
    };

    const auto fastToggles = togglesFor (0.05f);
    const auto slowToggles = togglesFor (0.6f);
    CHECK (fastToggles > slowToggles);
    CHECK (slowToggles >= 2);   // auch fern blinkt sichtbar

    // Sofortiger erster Puls beim Eintritt + solid nach dem Abholen.
    RouterRig rig;
    rig.bound.insert ({ false, 16 });
    rig.router.updatePickupState ({ 1, 16, false }, waitingState (0.3f, {}, true));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 52) == PickupLedRouter::kLedOn);

    rig.router.updatePickupState ({ 1, 16, false }, notWaiting());
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 52) == 0);
    CHECK (rig.lastValueFor (true, 124) == PickupLedRouter::kLedOn);   // gruen solid
}

TEST_CASE ("PickupLedRouter: Detail-Modus momentary -- Pads zeigen Einzel-Status, Restore beim Loslassen", "[midirig]")
{
    RouterRig rig;
    rig.bound.insert ({ false, 4 });    // Knob gebunden + abgeholt
    rig.bound.insert ({ false, 16 });   // Fader gebunden ...
    rig.router.updatePickupState ({ 1, 16, false }, waitingState (0.5f));   // ... wartet
    rig.echo[{ true, 36 }] = 90;        // pad_a-LED zeigt einen Toggle-Zustand

    // Ohne Push: Detail-Pads unangetastet.
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 36) == -1);
    CHECK (rig.lastValueFor (true, 24) == -1);

    // Push halten: Knob abgeholt -> pad_a gruen (Ebene led_layer_c, Note 108),
    // Basis aus; Fader wartet -> pad_m Basis (24) blinkt rot, gruen (96) aus.
    rig.router.handleControllerNote (52, true);
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 108) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 36) == 0);
    CHECK (rig.lastValueFor (true, 24) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 96) == 0);

    // Push loslassen: Restore -- pad_a-Basis bekommt den Echo-Stand (90)
    // zurueck, die uebrigen Detail-Adressen fallen auf 0.
    rig.router.handleControllerNote (52, false);
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 36) == 90);
    CHECK (rig.lastValueFor (true, 108) == 0);
    CHECK (rig.lastValueFor (true, 24) == 0);
}

TEST_CASE ("PickupLedRouter: led_pickup ohne Gruppe blinkt immer bei waiting", "[midirig]")
{
    RouterRig rig;

    rig.router.updatePickupState ({ 1, 30, false }, waitingState (0.4f));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 40) == PickupLedRouter::kLedOn);

    // Abgeholt: Adresse wird restauriert (kein Echo-Stand -> 0).
    rig.router.updatePickupState ({ 1, 30, false }, notWaiting());
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 40) == 0);
}

TEST_CASE ("PickupLedRouter: Shift-Pad zeigt die Richtung solid (kein Blinken)", "[midirig]")
{
    RouterRig rig;
    // shiftpad: led=12 (rot), led_layer_b=48 (orange), led_layer_c=84 (gruen).

    // Nur wartende Ebene, Pad NICHT gehalten: keine Anzeige.
    rig.router.updatePickupState ({ 1, 30, false },
                                  shiftWaiting ({ { 1, 12 } }, /*physicalAbove*/ true));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 12) == -1);

    // Pad gehalten + physisch ueber Software: rot solid (verringern), ohne
    // Bewegung -- der Status steht schon "im Moment des Drueckens".
    rig.router.handleControllerNote (12, true);
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 12) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 48) == 0);
    CHECK (rig.lastValueFor (true, 84) == 0);

    // Der Router "besitzt" die Farb-LEDs jetzt -- das Echo muss sie ueberspringen.
    CHECK (rig.router.isManaging (true, 12));
    CHECK (rig.router.isManaging (true, 48));
    CHECK (rig.router.isManaging (true, 84));

    // Solid: ueber viele Ticks kein Toggle (genau ein Send dank Dedupe).
    rig.ticks (30);
    CHECK (rig.sendCountFor (true, 12) == 1);

    // Richtung dreht: physisch unter Software -> orange (erhoehen), rot aus.
    rig.router.updatePickupState ({ 1, 30, false },
                                  shiftWaiting ({ { 1, 12 } }, /*physicalAbove*/ false));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 48) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 12) == 0);

    // Abgeholt (aligned): gruen solid, obwohl nicht mehr "waiting".
    rig.router.updatePickupState ({ 1, 30, false }, shiftAligned ({ { 1, 12 } }));
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 84) == PickupLedRouter::kLedOn);
    CHECK (rig.lastValueFor (true, 48) == 0);

    // Pad loslassen: Anzeige endet (Restore auf 0, kein Echo-Stand).
    rig.router.handleControllerNote (12, false);
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 84) == 0);
    CHECK_FALSE (rig.router.isManaging (true, 84));   // Echo darf wieder senden
}

TEST_CASE ("PickupLedRouter: Channelstrip-Ebene dauerhaft + tempo-synchrone Blinks (M7b)", "[midirig]")
{
    // strippad1: led=100 (rot), led_layer_b=101 (orange), led_layer_c=102 (gruen).
    // strippad2: 103/104/105. (col1)
    SECTION ("Basis: alle Spalten-Pads SOLID in der Ebenen-Farbe")
    {
        RouterRig rig;
        rig.router.setColumnLayer ("col1", 1);   // gruen = led_layer_c
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 102) == PickupLedRouter::kLedOn);   // strippad1 gruen
        CHECK (rig.lastValueFor (true, 105) == PickupLedRouter::kLedOn);   // strippad2 gruen
        CHECK (rig.lastValueFor (true, 100) == 0);                         // rot aus

        // Solid: ueber viele Ticks genau EIN Send (Dedupe, kein Blinken).
        rig.ticks (30);
        CHECK (rig.sendCountFor (true, 102) == 1);

        // Ebene 0 -> rot=Basis, Ebene 2 -> orange=led_layer_b.
        rig.router.setColumnLayer ("col1", 0);
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 100) == PickupLedRouter::kLedOn);
        CHECK (rig.lastValueFor (true, 102) == 0);
    }

    SECTION ("Aktives Pad (Echo>0) blinkt im 8tel-Takt, ruhende bleiben solid")
    {
        RouterRig rig;
        rig.echo[{ true, 100 }] = 127;           // strippad1 als Toggle AN
        rig.router.setColumnLayer ("col1", 0);   // rot = led-Basis (100/103)

        rig.router.setBeatPosition (0.0);        // 8tel-Phase AN
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 100) == PickupLedRouter::kLedOn);
        CHECK (rig.lastValueFor (true, 103) == PickupLedRouter::kLedOn);   // strippad2 solid

        rig.router.setBeatPosition (0.25);       // frac(0.25/0.5)=0.5 -> AUS
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 100) == 0);              // aktives Pad blinkt aus
        CHECK (rig.lastValueFor (true, 103) == PickupLedRouter::kLedOn);   // ruhendes bleibt an
    }

    SECTION ("Ebenen-Wechsel: 16tel-Flackern, danach wieder solid")
    {
        RouterRig rig;
        rig.router.setColumnLayer ("col1", 0);   // Erst-Init (kein Fenster)
        rig.router.setColumnLayer ("col1", 1);   // Wechsel -> 16tel-Fenster

        rig.router.setBeatPosition (0.0);        // 16tel-Phase AN
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 102) == PickupLedRouter::kLedOn);

        rig.router.setBeatPosition (0.125);      // frac(0.125/0.25)=0.5 -> AUS
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 102) == 0);

        // Fenster laeuft ab -> solid gruen, unabhaengig von der Beat-Phase.
        rig.ticks (PickupLedRouter::kLayerChangeTicks + 2);
        rig.router.setBeatPosition (0.125);
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 102) == PickupLedRouter::kLedOn);
    }
}

TEST_CASE ("PickupLedRouter: reset verwirft Zustand ohne Sends, clearProfile restauriert", "[midirig]")
{
    RouterRig rig;
    rig.bound.insert ({ false, 4 });
    rig.router.tick();
    CHECK (rig.lastValueFor (true, 124) == PickupLedRouter::kLedOn);

    SECTION ("reset: kein Send (Geraet ist weg), naechster Tick baut neu auf")
    {
        rig.sent.clear();
        rig.router.reset();
        CHECK (rig.sent.empty());

        rig.router.tick();   // Status-LEDs werden frisch aufgebaut
        CHECK (rig.lastValueFor (true, 124) == PickupLedRouter::kLedOn);
    }

    SECTION ("clearProfile: verwaltete LEDs werden restauriert")
    {
        rig.router.clearProfile();
        rig.router.tick();
        CHECK (rig.lastValueFor (true, 124) == 0);
    }
}
