#include <catch2/catch_test_macros.hpp>

#include "Core/VoiceAllocator.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("VoiceAllocator: Zuteilung, Idempotenz, Release", "[grid]")
{
    grid::VoiceAllocator allocator (2);
    uint32_t stolen = 999;

    REQUIRE (allocator.allocate (101, stolen) == 0);
    REQUIRE (stolen == 0);
    REQUIRE (allocator.allocate (102, stolen) == 1);
    REQUIRE (stolen == 0);

    // Erneutes Allocate desselben Fingers -> derselbe Slot, nichts gestohlen
    REQUIRE (allocator.allocate (101, stolen) == 0);
    REQUIRE (stolen == 0);

    REQUIRE (allocator.voiceForFinger (101) == 0);
    REQUIRE (allocator.voiceForFinger (102) == 1);
    REQUIRE (allocator.voiceForFinger (999) == -1);

    REQUIRE (allocator.release (101) == 0);
    REQUIRE (allocator.voiceForFinger (101) == -1);
}

TEST_CASE ("VoiceAllocator: Stealing bei voller Bank", "[grid]")
{
    grid::VoiceAllocator allocator (2);
    uint32_t stolen = 999;

    REQUIRE (allocator.allocate (101, stolen) == 0);
    REQUIRE (allocator.allocate (102, stolen) == 1);

    // Bank voll -> ältester (101, Slot 0) wird verdrängt
    REQUIRE (allocator.allocate (103, stolen) == 0);
    REQUIRE (stolen == 101);
    REQUIRE (allocator.voiceForFinger (101) == -1);
    REQUIRE (allocator.voiceForFinger (103) == 0);
}

TEST_CASE ("VoiceAllocator: Reset und fingerId 0", "[grid]")
{
    grid::VoiceAllocator allocator (2);
    uint32_t stolen = 999;

    REQUIRE (allocator.allocate (101, stolen) == 0);
    REQUIRE (allocator.activeVoices() == 1);

    allocator.reset();
    REQUIRE (allocator.activeVoices() == 0);
    REQUIRE (allocator.voiceForFinger (101) == -1);

    // fingerId 0 ist Sentinel -> nie zuteilen
    REQUIRE (allocator.allocate (0, stolen) == -1);
    REQUIRE (stolen == 0);
    REQUIRE (allocator.voiceForFinger (0) == -1);
    REQUIRE (allocator.release (0) == -1);
}
