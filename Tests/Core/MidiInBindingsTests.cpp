#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>

#include "Core/MidiInBindings.h"

namespace grid = conduit::grid;
using Catch::Approx;

namespace
{
    // Kleiner Fahrstand: haelt Control-Werte und zeichnet Anwendungen auf.
    struct Rig
    {
        grid::MidiInBindings bindings;
        std::map<int, float> values;   // Key = controlId (ein Layer, Achse 0)
        std::vector<std::pair<int, float>> applied;

        void tick()
        {
            bindings.tick (
                [this] (const grid::MacroControlKey& key) { return values[key.controlId]; },
                [this] (const grid::MacroControlKey& key, float v)
                {
                    values[key.controlId] = v;
                    applied.emplace_back (key.controlId, v);
                });
        }

        void ticks (int n) { for (int i = 0; i < n; ++i) tick(); }
    };

    constexpr grid::MacroControlKey keyFor (int id) { return { grid::MacroControlKey::diy, id, 0 }; }
}

//==============================================================================
TEST_CASE ("SoftTakeover: greift bei Naehe oder Kreuzung, sonst nicht", "[grid][midiin]")
{
    grid::SoftTakeover takeover;

    // Weit weg, keine Historie -> kein Zugriff
    REQUIRE_FALSE (takeover.shouldApply (0.8f, 0.1f));

    // Weiter unterhalb -> immer noch nicht
    REQUIRE_FALSE (takeover.shouldApply (0.8f, 0.5f));

    // Kreuzt den aktuellen Wert (0.5 -> 0.9 ueber 0.8) -> ab jetzt aktiv
    REQUIRE (takeover.shouldApply (0.8f, 0.9f));
    REQUIRE (takeover.shouldApply (0.8f, 0.2f));   // bleibt aktiv

    takeover.disengage();
    REQUIRE_FALSE (takeover.shouldApply (0.8f, 0.2f));   // muss neu aufnehmen
}

TEST_CASE ("SoftTakeover: Naehe-Epsilon reicht zum Aufnehmen", "[grid][midiin]")
{
    grid::SoftTakeover takeover;
    REQUIRE (takeover.shouldApply (0.5f, 0.52f));   // |diff| < 0.03
}

//==============================================================================
TEST_CASE ("MidiInBindings: bind ersetzt Key- und CC-Kollisionen", "[grid][midiin]")
{
    grid::MidiInBindings bindings;

    bindings.bind (keyFor (1), 1, 20);
    bindings.bind (keyFor (2), 1, 21);
    REQUIRE (bindings.count() == 2);

    // Gleicher Key, neuer CC -> ersetzt die alte Bindung von Control 1
    bindings.bind (keyFor (1), 1, 30);
    REQUIRE (bindings.count() == 2);
    REQUIRE (bindings.bindingFor (keyFor (1))->cc == 30);

    // Anderer Key, aber derselbe CC wie Control 2 -> Control 2 verliert
    bindings.bind (keyFor (3), 1, 21);
    REQUIRE (bindings.count() == 2);
    REQUIRE (bindings.bindingFor (keyFor (2)) == nullptr);

    bindings.unbind (keyFor (3));
    REQUIRE (bindings.bindingFor (keyFor (3)) == nullptr);
}

TEST_CASE ("MidiInBindings: eingehender CC wird geglaettet angewendet (nach Pickup)", "[grid][midiin]")
{
    Rig rig;
    rig.bindings.bind (keyFor (1), 1, 20);
    rig.values[1] = 0.5f;

    // Erster Wert nahe am Ist-Wert -> Pickup sofort, Glaettung startet dort.
    rig.bindings.handleIncomingCc (1, 20, 64);   // ~0.504
    rig.tick();
    REQUIRE_FALSE (rig.applied.empty());
    REQUIRE (rig.values[1] == Approx (64.0f / 127.0f).margin (0.01));

    // Sprung auf 127: Glaettung faehrt weich hoch (mehrere Ticks, monoton).
    rig.applied.clear();
    rig.bindings.handleIncomingCc (1, 20, 127);
    rig.tick();
    const auto first = rig.values[1];
    REQUIRE (first < 1.0f);      // nicht sofort am Ziel
    REQUIRE (first > 0.5f);      // aber unterwegs

    rig.ticks (40);
    REQUIRE (rig.values[1] == Approx (1.0f).margin (0.005));

    // Monotonie der Fahrt
    for (size_t i = 1; i < rig.applied.size(); ++i)
        REQUIRE (rig.applied[i].second >= rig.applied[i - 1].second);
}

TEST_CASE ("MidiInBindings: kein Parametersprung ohne Pickup", "[grid][midiin]")
{
    Rig rig;
    rig.bindings.bind (keyFor (1), 1, 20);
    rig.values[1] = 0.9f;

    // Externer Fader steht weit unten -> darf nicht anwenden
    rig.bindings.handleIncomingCc (1, 20, 10);
    rig.ticks (10);
    REQUIRE (rig.applied.empty());
    REQUIRE (rig.values[1] == Approx (0.9f));

    // Externer Fader faehrt hoch und KREUZT 0.9 -> ab da wird angewendet
    rig.bindings.handleIncomingCc (1, 20, 121);   // ~0.953, kreuzt 0.9
    rig.ticks (40);
    REQUIRE_FALSE (rig.applied.empty());
    REQUIRE (rig.values[1] == Approx (121.0f / 127.0f).margin (0.01));
}

TEST_CASE ("MidiInBindings: lokaler Touch loest den Takeover", "[grid][midiin]")
{
    Rig rig;
    rig.bindings.bind (keyFor (1), 1, 20);
    rig.values[1] = 0.5f;

    rig.bindings.handleIncomingCc (1, 20, 64);
    rig.tick();
    REQUIRE_FALSE (rig.applied.empty());

    // User fasst das Control an -> naechster externer Wert weit weg greift nicht
    rig.bindings.notifyLocalTouch (keyFor (1));
    rig.values[1] = 0.1f;
    rig.applied.clear();

    rig.bindings.handleIncomingCc (1, 20, 127);
    rig.ticks (10);
    REQUIRE (rig.applied.empty());
}

TEST_CASE ("MidiInBindings: Feedback-Echo-Schnittstelle feuert beim Anwenden", "[grid][midiin]")
{
    Rig rig;
    rig.bindings.bind (keyFor (1), 1, 20);
    rig.values[1] = 0.5f;

    std::vector<std::tuple<int, int, float>> echoes;
    rig.bindings.onFeedbackEcho = [&] (int channel, int cc, float value01)
    { echoes.emplace_back (channel, cc, value01); };

    rig.bindings.handleIncomingCc (1, 20, 64);
    rig.tick();

    REQUIRE_FALSE (echoes.empty());
    REQUIRE (std::get<0> (echoes.back()) == 1);
    REQUIRE (std::get<1> (echoes.back()) == 20);
}

TEST_CASE ("MidiInBindings: MIDI-Learn bindet den naechsten CC und meldet ihn", "[grid][midiin]")
{
    Rig rig;
    rig.values[5] = 0.5f;

    std::vector<std::tuple<int, int>> learned;
    rig.bindings.onLearnCompleted = [&] (const grid::MacroControlKey& key, int channel, int cc)
    {
        REQUIRE (key.controlId == 5);
        learned.emplace_back (channel, cc);
    };

    rig.bindings.armLearn (keyFor (5));
    REQUIRE (rig.bindings.isLearnArmed());

    rig.bindings.handleIncomingCc (2, 74, 64);

    REQUIRE_FALSE (rig.bindings.isLearnArmed());
    REQUIRE (learned.size() == 1);
    REQUIRE (std::get<0> (learned[0]) == 2);
    REQUIRE (std::get<1> (learned[0]) == 74);

    const auto* binding = rig.bindings.bindingFor (keyFor (5));
    REQUIRE (binding != nullptr);
    REQUIRE (binding->channel == 2);
    REQUIRE (binding->cc == 74);

    // Der Lern-CC selbst wird normal verarbeitet -- nahe 0.5 -> Pickup sofort.
    rig.tick();
    REQUIRE (rig.values[5] == Approx (64.0f / 127.0f).margin (0.01));
}

TEST_CASE ("MidiInBindings: cancelLearn entschaerft ohne zu binden", "[grid][midiin]")
{
    Rig rig;
    rig.bindings.armLearn (keyFor (7));
    rig.bindings.cancelLearn();

    rig.bindings.handleIncomingCc (1, 40, 100);
    REQUIRE (rig.bindings.bindingFor (keyFor (7)) == nullptr);
}
