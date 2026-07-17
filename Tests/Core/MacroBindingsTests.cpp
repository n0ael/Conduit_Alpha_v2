#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/MacroBindings.h"
#include "TouchLive/AbletonParamTarget.h"
#include "FakeMidiTarget.h"

namespace grid = conduit::grid;
using Catch::Approx;

namespace
{
    // Test-Double: zeichnet gesendete Werte auf (target-agnostisch).
    struct RecordingTarget final : public grid::MacroTarget
    {
        void sendValue (float value01) override { values.push_back (value01); }
        [[nodiscard]] juce::String describe() const override { return "Recorder"; }

        std::vector<float> values;
    };
}

//==============================================================================
TEST_CASE ("MacroBindings: add/count/get/remove pro Key, Limit 16", "[grid][macro]")
{
    grid::MacroBindings bindings;
    const grid::MacroControlKey key { grid::MacroControlKey::diy, 3, 0 };

    REQUIRE (bindings.count (key) == 0);
    REQUIRE (bindings.get (key, 0) == nullptr);

    for (int i = 0; i < grid::MacroBindings::kMaxTargetsPerControl; ++i)
        REQUIRE (bindings.add (key) != nullptr);

    REQUIRE (bindings.count (key) == grid::MacroBindings::kMaxTargetsPerControl);
    REQUIRE (bindings.add (key) == nullptr);   // 17. Slot abgelehnt

    bindings.remove (key, 0);
    REQUIRE (bindings.count (key) == grid::MacroBindings::kMaxTargetsPerControl - 1);
    REQUIRE (bindings.add (key) != nullptr);   // wieder Platz
}

TEST_CASE ("MacroBindings: Keys trennen Layer, Control und Achse", "[grid][macro]")
{
    grid::MacroBindings bindings;

    bindings.add ({ grid::MacroControlKey::system, 1, 0 });
    bindings.add ({ grid::MacroControlKey::diy, 1, 0 });
    bindings.add ({ grid::MacroControlKey::diy, 1, 1 });

    REQUIRE (bindings.count ({ grid::MacroControlKey::system, 1, 0 }) == 1);
    REQUIRE (bindings.count ({ grid::MacroControlKey::diy, 1, 0 }) == 1);
    REQUIRE (bindings.count ({ grid::MacroControlKey::diy, 1, 1 }) == 1);
    REQUIRE (bindings.count ({ grid::MacroControlKey::system, 1, 1 }) == 0);
}

TEST_CASE ("MacroBindings: clearControl entfernt alle Achsen eines Controls", "[grid][macro]")
{
    grid::MacroBindings bindings;
    bindings.add ({ grid::MacroControlKey::diy, 5, 0 });
    bindings.add ({ grid::MacroControlKey::diy, 5, 1 });
    bindings.add ({ grid::MacroControlKey::diy, 6, 0 });

    bindings.clearControl (grid::MacroControlKey::diy, 5);

    REQUIRE (bindings.count ({ grid::MacroControlKey::diy, 5, 0 }) == 0);
    REQUIRE (bindings.count ({ grid::MacroControlKey::diy, 5, 1 }) == 0);
    REQUIRE (bindings.count ({ grid::MacroControlKey::diy, 6, 0 }) == 1);
}

TEST_CASE ("MacroBindings: applyValue formt durch die Kurve und klemmt auf [0,1]", "[grid][macro]")
{
    grid::MacroBindings bindings;
    const grid::MacroControlKey key { grid::MacroControlKey::system, 2, 0 };

    auto* binding = bindings.add (key);
    REQUIRE (binding != nullptr);

    auto recorder = std::make_unique<RecordingTarget>();
    auto* recorderPtr = recorder.get();
    binding->target = std::move (recorder);

    // Identitaets-Kurve: Wert geht 1:1 durch.
    bindings.applyValue (key, 0.25f);
    REQUIRE (recorderPtr->values.size() == 1);
    REQUIRE (recorderPtr->values.back() == Approx (0.25f));
    REQUIRE (binding->lastInput01 == Approx (0.25f));
    REQUIRE (binding->lastOutput01 == Approx (0.25f));

    // Invertierte Kurve (Range 1..0): Ausgang gespiegelt.
    binding->curve.setOutputRange (1.0f, 0.0f);
    bindings.applyValue (key, 0.25f);
    REQUIRE (recorderPtr->values.back() == Approx (0.75f));

    // Ausgang geklemmt: Range ueber [0,1] hinaus wird begrenzt.
    binding->curve.setOutputRange (0.0f, 2.0f);
    bindings.applyValue (key, 1.0f);
    REQUIRE (recorderPtr->values.back() == Approx (1.0f));
}

TEST_CASE ("MacroBindings: Slots ohne Ziel aktualisieren nur die Anzeige", "[grid][macro]")
{
    grid::MacroBindings bindings;
    const grid::MacroControlKey key { grid::MacroControlKey::diy, 9, 0 };

    auto* binding = bindings.add (key);
    bindings.applyValue (key, 0.5f);   // darf nicht crashen (target == nullptr)

    REQUIRE (binding->lastInput01 == Approx (0.5f));
    REQUIRE (binding->lastOutput01 == Approx (0.5f));
}

//==============================================================================
TEST_CASE ("MidiCcTarget: sendet controllerEvent, dedupliziert auf 7 bit", "[grid][macro]")
{
    grid::FakeMidiTarget fake;
    grid::MidiCcTarget target (fake, 1, 74);

    target.sendValue (0.5f);
    REQUIRE (fake.messages.size() == 1);
    REQUIRE (fake.messages[0].isController());
    REQUIRE (fake.messages[0].getChannel() == 1);
    REQUIRE (fake.messages[0].getControllerNumber() == 74);
    const auto first = fake.messages[0].getControllerValue();
    REQUIRE ((first == 63 || first == 64));

    // Gleicher 7-bit-Wert: kein zweiter Send (Dedupe).
    target.sendValue (0.5f);
    REQUIRE (fake.messages.size() == 1);

    target.sendValue (1.0f);
    REQUIRE (fake.messages.size() == 2);
    REQUIRE (fake.messages[1].getControllerValue() == 127);
}

TEST_CASE ("MidiCcTarget: klemmt Kanal und CC-Nummer", "[grid][macro]")
{
    grid::FakeMidiTarget fake;
    grid::MidiCcTarget target (fake, 99, 200);

    REQUIRE (target.channel() == 16);
    REQUIRE (target.ccNumber() == 127);
}

TEST_CASE ("MidiCcTarget: describe() zeigt den Funktionsnamen (Block L)", "[grid][macro]")
{
    grid::FakeMidiTarget fake;

    grid::MidiCcTarget known (fake, 1, 1);
    REQUIRE (known.describe() == "Mod Wheel / Kanal 1");

    grid::MidiCcTarget unknown (fake, 2, 3);
    REQUIRE (unknown.describe() == "CC 3 / Kanal 2");
}

//==============================================================================
TEST_CASE ("AbletonParamTarget: mapToNative skaliert auf den Live-Bereich", "[grid][macro]")
{
    using T = grid::AbletonParamTarget;

    REQUIRE (T::mapToNative (0.0f, -60.0f, 6.0f, false) == Approx (-60.0f));
    REQUIRE (T::mapToNative (1.0f, -60.0f, 6.0f, false) == Approx (6.0f));
    REQUIRE (T::mapToNative (0.5f, 0.0f, 1.0f, false) == Approx (0.5f));

    // Geklemmt ausserhalb [0,1]
    REQUIRE (T::mapToNative (2.0f, 0.0f, 10.0f, false) == Approx (10.0f));
    REQUIRE (T::mapToNative (-1.0f, 0.0f, 10.0f, false) == Approx (0.0f));

    // Quantisiert: ganze Schritte
    REQUIRE (T::mapToNative (0.5f, 0.0f, 5.0f, true) == Approx (3.0f));   // 2.5 -> round
    REQUIRE (T::mapToNative (0.2f, 0.0f, 5.0f, true) == Approx (1.0f));
}

//==============================================================================
// MIDI-Rig M2: NRPN- und Program-Change-Ziele.

TEST_CASE ("MidiNrpnTarget: Sequenz 99/98/6/38, Mapping und 14-bit-Dedupe", "[midirig][macro]")
{
    grid::FakeMidiTarget fake;
    grid::MidiNrpnTarget target (fake, 3, 70, 0, 16383, "Analog Heat: Frequency");

    target.sendValue (1.0f);
    REQUIRE (fake.messages.size() == 4);

    REQUIRE (fake.messages[0].getControllerNumber() == 99);   // Adresse MSB
    REQUIRE (fake.messages[0].getControllerValue() == 0);     // 70 / 128
    REQUIRE (fake.messages[0].getChannel() == 3);
    REQUIRE (fake.messages[1].getControllerNumber() == 98);   // Adresse LSB
    REQUIRE (fake.messages[1].getControllerValue() == 70);
    REQUIRE (fake.messages[2].getControllerNumber() == 6);    // Daten MSB
    REQUIRE (fake.messages[2].getControllerValue() == 127);
    REQUIRE (fake.messages[3].getControllerNumber() == 38);   // Daten LSB
    REQUIRE (fake.messages[3].getControllerValue() == 127);   // 16383 & 0x7f

    // Dedupe: identischer gemappter Wert sendet nichts.
    target.sendValue (1.0f);
    REQUIRE (fake.messages.size() == 4);

    // Mitte: 8192 → MSB 64, LSB 0.
    target.sendValue (0.5f);
    REQUIRE (fake.messages.size() == 8);
    REQUIRE (fake.messages[6].getControllerValue() == 64);
    REQUIRE (fake.messages[7].getControllerValue() == 0);
}

TEST_CASE ("MidiNrpnTarget: min/max aus dem Profil begrenzen das Mapping", "[midirig][macro]")
{
    grid::FakeMidiTarget fake;
    grid::MidiNrpnTarget target (fake, 1, 200, 100, 200);

    target.sendValue (0.0f);
    REQUIRE (fake.messages.size() == 4);
    REQUIRE ((fake.messages[2].getControllerValue() << 7 | fake.messages[3].getControllerValue()) == 100);

    target.sendValue (1.0f);
    REQUIRE ((fake.messages[6].getControllerValue() << 7 | fake.messages[7].getControllerValue()) == 200);
}

TEST_CASE ("MidiNrpnTarget: describe und toState-Roundtrip-Felder", "[midirig][macro]")
{
    grid::FakeMidiTarget fake;
    grid::MidiNrpnTarget target (fake, 3, 70, 0, 16383, "Analog Heat: Frequency");

    REQUIRE (target.describe() == "Analog Heat: Frequency / Kanal 3");

    const auto state = target.toState();
    REQUIRE (state.hasType (grid::MidiNrpnTarget::kStateType));
    REQUIRE ((int) state.getProperty ("channel") == 3);
    REQUIRE ((int) state.getProperty ("number") == 70);
    REQUIRE ((int) state.getProperty ("min") == 0);
    REQUIRE ((int) state.getProperty ("max") == 16383);
    REQUIRE (state.getProperty ("name").toString() == "Analog Heat: Frequency");

    grid::MidiNrpnTarget unnamed (fake, 2, 300, 0, 16383);
    REQUIRE (unnamed.describe() == "NRPN 300 / Kanal 2");
}

TEST_CASE ("MidiProgramChangeTarget: PC mit optionaler Bank, Dedupe", "[midirig][macro]")
{
    grid::FakeMidiTarget fake;

    SECTION ("ohne Bank")
    {
        grid::MidiProgramChangeTarget target (fake, 5);
        target.sendValue (0.0f);
        REQUIRE (fake.messages.size() == 1);
        REQUIRE (fake.messages[0].isProgramChange());
        REQUIRE (fake.messages[0].getProgramChangeNumber() == 0);
        REQUIRE (fake.messages[0].getChannel() == 5);

        target.sendValue (0.0f);   // Dedupe
        REQUIRE (fake.messages.size() == 1);

        target.sendValue (1.0f);
        REQUIRE (fake.messages.size() == 2);
        REQUIRE (fake.messages[1].getProgramChangeNumber() == 127);
    }

    SECTION ("mit Bank CC0/CC32 (ADR E5)")
    {
        grid::MidiProgramChangeTarget target (fake, 1, 2, 3);
        target.sendValue (0.5f);
        REQUIRE (fake.messages.size() == 3);
        REQUIRE (fake.messages[0].getControllerNumber() == 0);
        REQUIRE (fake.messages[0].getControllerValue() == 2);
        REQUIRE (fake.messages[1].getControllerNumber() == 32);
        REQUIRE (fake.messages[1].getControllerValue() == 3);
        REQUIRE (fake.messages[2].isProgramChange());
    }

    SECTION ("toState")
    {
        grid::MidiProgramChangeTarget target (fake, 4, 7, -1);
        const auto state = target.toState();
        REQUIRE (state.hasType (grid::MidiProgramChangeTarget::kStateType));
        REQUIRE ((int) state.getProperty ("channel") == 4);
        REQUIRE ((int) state.getProperty ("bankMsb") == 7);
        REQUIRE ((int) state.getProperty ("bankLsb") == -1);
    }
}

TEST_CASE ("MidiPresetLoadTarget: Druckflanke sendet Bank+PC genau einmal, State-Roundtrip", "[midirig][macro][sysex]")
{
    grid::FakeMidiTarget out;
    grid::MidiPresetLoadTarget target { out, 3, 87, -1, 2, "Fat Bass" };

    // Press: CC32 (Bank-LSB) + Program Change, genau einmal.
    target.sendValue (1.0f);
    REQUIRE (out.messages.size() == 2);
    CHECK (out.messages[0].isController());
    CHECK (out.messages[0].getControllerNumber() == 32);
    CHECK (out.messages[0].getControllerValue() == 2);
    CHECK (out.messages[1].isProgramChange());
    CHECK (out.messages[1].getProgramChangeNumber() == 87);
    CHECK (out.messages[1].getChannel() == 3);

    // Halten + Release: nichts.
    target.sendValue (1.0f);
    target.sendValue (0.7f);
    target.sendValue (0.0f);
    CHECK (out.messages.size() == 2);

    // Erneuter Press sendet WIEDER (kein Dedupe ueber Flanken).
    target.sendValue (1.0f);
    CHECK (out.messages.size() == 4);

    // describe traegt den Preset-Namen; toState-Roundtrip vollstaendig.
    CHECK (target.describe().contains ("Fat Bass"));
    const auto state = target.toState();
    CHECK (state.hasType (grid::MidiPresetLoadTarget::kStateType));
    CHECK ((int) state.getProperty ("channel") == 3);
    CHECK ((int) state.getProperty ("program") == 87);
    CHECK ((int) state.getProperty ("bankMsb") == -1);
    CHECK ((int) state.getProperty ("bankLsb") == 2);
    CHECK (state.getProperty ("name").toString() == "Fat Bass");

    // Ohne Bank nur der PC.
    grid::FakeMidiTarget plainOut;
    grid::MidiPresetLoadTarget plain { plainOut, 1, 5, -1, -1, {} };
    plain.sendValue (1.0f);
    REQUIRE (plainOut.messages.size() == 1);
    CHECK (plainOut.messages[0].isProgramChange());
}
