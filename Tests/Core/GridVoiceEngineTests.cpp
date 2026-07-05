#include <catch2/catch_test_macros.hpp>

#include "Core/GridVoiceEngine.h"
#include "FakeVoiceSink.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("GridVoiceEngine: noteOn belegt Slots aufsteigend", "[grid]")
{
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);

    engine.noteOn (101, 60, 100);
    REQUIRE (fake.calls.size() == 1);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[0].intValue == 60);
    REQUIRE (fake.calls[0].intValue2 == 100);

    engine.noteOn (102, 64, 90);
    REQUIRE (fake.calls.size() == 2);
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[1].voiceIndex == 1);
    REQUIRE (fake.calls[1].intValue == 64);
    REQUIRE (fake.calls[1].intValue2 == 90);
}

TEST_CASE ("GridVoiceEngine: Pitch-Bend/Pressure/Slide adressieren den richtigen Slot", "[grid]")
{
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);

    engine.noteOn (101, 60, 100);
    fake.calls.clear();

    engine.setPitchBend (101, 2.0f);
    engine.setPressure (101, 0.5f);
    engine.setSlide (101, 0.5f);

    REQUIRE (fake.calls.size() == 3);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::PitchBend);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[0].floatValue == 2.0f);
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::Pressure);
    REQUIRE (fake.calls[1].voiceIndex == 0);
    REQUIRE (fake.calls[1].floatValue == 0.5f);
    REQUIRE (fake.calls[2].kind == grid::FakeVoiceSink::Kind::Slide);
    REQUIRE (fake.calls[2].voiceIndex == 0);
    REQUIRE (fake.calls[2].floatValue == 0.5f);
}

TEST_CASE ("GridVoiceEngine: unbekannter Finger erzeugt keinen Aufruf", "[grid]")
{
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);

    engine.setPressure (99, 0.5f);
    REQUIRE (fake.calls.empty());
}

TEST_CASE ("GridVoiceEngine: noteOff beendet die Stimme, danach kein Ausdruck mehr", "[grid]")
{
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);

    engine.noteOn (101, 60, 100);
    fake.calls.clear();

    engine.noteOff (101, 0);
    REQUIRE (fake.calls.size() == 1);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStop);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[0].intValue == 0);

    fake.calls.clear();
    engine.setPressure (101, 0.7f);
    REQUIRE (fake.calls.empty());
}

TEST_CASE ("GridVoiceEngine: Stealing beendet die alte Stimme vor dem neuen Start", "[grid]")
{
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake, 2);

    engine.noteOn (101, 60, 100);
    engine.noteOn (102, 64, 90);
    fake.calls.clear();

    engine.noteOn (103, 67, 80);

    REQUIRE (fake.calls.size() == 2);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::VoiceStop);
    REQUIRE (fake.calls[0].voiceIndex == 0);
    REQUIRE (fake.calls[1].kind == grid::FakeVoiceSink::Kind::VoiceStart);
    REQUIRE (fake.calls[1].voiceIndex == 0);
    REQUIRE (fake.calls[1].intValue == 67);
}

TEST_CASE ("GridVoiceEngine: allNotesOff meldet und setzt zurück", "[grid]")
{
    grid::FakeVoiceSink fake;
    grid::GridVoiceEngine engine (fake);

    engine.noteOn (101, 60, 100);
    fake.calls.clear();

    engine.allNotesOff();
    REQUIRE (fake.calls.size() == 1);
    REQUIRE (fake.calls[0].kind == grid::FakeVoiceSink::Kind::AllNotesOff);

    fake.calls.clear();
    engine.setPressure (101, 0.5f);
    REQUIRE (fake.calls.empty());
}
