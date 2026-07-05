#include <catch2/catch_test_macros.hpp>

#include "Core/MpeMidiSink.h"
#include "FakeMidiTarget.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("MpeMidiSink: voiceStart sendet NoteOn", "[grid]")
{
    grid::FakeMidiTarget fake;
    grid::MpeMidiSink sink (fake);

    sink.voiceStart (0, 60, 100);

    REQUIRE (fake.messages.size() == 1);
    REQUIRE (fake.messages[0].isNoteOn());
    REQUIRE (fake.messages[0].getChannel() == 2);
    REQUIRE (fake.messages[0].getNoteNumber() == 60);
    REQUIRE (fake.messages[0].getVelocity() == 100);
}

TEST_CASE ("MpeMidiSink: voicePitchBend sendet PitchWheel", "[grid]")
{
    grid::FakeMidiTarget fake;
    grid::MpeMidiSink sink (fake);

    sink.voicePitchBend (0, 0.0f);

    REQUIRE (fake.messages.size() == 1);
    REQUIRE (fake.messages[0].isPitchWheel());
    REQUIRE (fake.messages[0].getChannel() == 2);
    REQUIRE (fake.messages[0].getPitchWheelValue() == 8192);
}

TEST_CASE ("MpeMidiSink: voiceStop sendet NoteOff mit gemerkter Note", "[grid]")
{
    grid::FakeMidiTarget fake;
    grid::MpeMidiSink sink (fake);

    sink.voiceStart (0, 60, 100);
    sink.voiceStop (0, 0);

    REQUIRE (fake.messages.size() == 2);
    REQUIRE (fake.messages[1].isNoteOff());
    REQUIRE (fake.messages[1].getChannel() == 2);
    REQUIRE (fake.messages[1].getNoteNumber() == 60);
}

TEST_CASE ("MpeMidiSink: voiceStop ohne aktive Note sendet nichts", "[grid]")
{
    grid::FakeMidiTarget fake;
    grid::MpeMidiSink sink (fake);

    sink.voiceStop (0, 0);

    REQUIRE (fake.messages.empty());
}

TEST_CASE ("MpeMidiSink: allNotesOff beendet alle aktiven Stimmen", "[grid]")
{
    grid::FakeMidiTarget fake;
    grid::MpeMidiSink sink (fake);

    sink.voiceStart (0, 60, 100);
    sink.voiceStart (1, 64, 90);
    fake.messages.clear();

    sink.allNotesOff();

    REQUIRE (fake.messages.size() == 2);
    REQUIRE (fake.messages[0].isNoteOff());
    REQUIRE (fake.messages[0].getChannel() == 2);
    REQUIRE (fake.messages[0].getNoteNumber() == 60);
    REQUIRE (fake.messages[1].isNoteOff());
    REQUIRE (fake.messages[1].getChannel() == 3);
    REQUIRE (fake.messages[1].getNoteNumber() == 64);
}
