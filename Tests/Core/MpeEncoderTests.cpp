#include <catch2/catch_test_macros.hpp>

#include "Core/MpeEncoder.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("MpeEncoder: Kanal-Zuordnung + NoteOn/NoteOff", "[grid]")
{
    const grid::MpeEncoder encoder;

    REQUIRE (encoder.channelForVoice (0) == 2);
    REQUIRE (encoder.channelForVoice (1) == 3);

    const auto on = encoder.noteOn (0, 60, 100);
    REQUIRE (on.isNoteOn());
    REQUIRE (on.getChannel() == 2);
    REQUIRE (on.getNoteNumber() == 60);
    REQUIRE (on.getVelocity() == 100);

    const auto off = encoder.noteOff (1, 64, 40);
    REQUIRE (off.isNoteOff());
    REQUIRE (off.getChannel() == 3);
    REQUIRE (off.getNoteNumber() == 64);
}

TEST_CASE ("MpeEncoder: Pitch-Bend Skalierung + Clamp", "[grid]")
{
    const grid::MpeEncoder encoder;

    REQUIRE (encoder.pitchBend (0, 0.0f).getPitchWheelValue() == 8192);
    REQUIRE (encoder.pitchBend (0, 48.0f).getPitchWheelValue() == 16383);
    REQUIRE (encoder.pitchBend (0, -48.0f).getPitchWheelValue() == 0);
    REQUIRE (encoder.pitchBend (0, 96.0f).getPitchWheelValue() == 16383);
}

TEST_CASE ("MpeEncoder: Pressure (Channel-Pressure) + Slide (CC74)", "[grid]")
{
    const grid::MpeEncoder encoder;

    REQUIRE (encoder.pressure (0, 1.0f).getChannelPressureValue() == 127);
    REQUIRE (encoder.pressure (0, 0.0f).getChannelPressureValue() == 0);

    const auto slideMsg = encoder.slide (0, 0.5f);
    REQUIRE (slideMsg.isController());
    REQUIRE (slideMsg.getControllerNumber() == 74);
    const int slideValue = slideMsg.getControllerValue();
    REQUIRE ((slideValue == 63 || slideValue == 64));
}
