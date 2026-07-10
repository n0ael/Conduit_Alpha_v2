#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "Core/ChordMemory.h"

namespace grid = conduit::grid;

//==============================================================================
TEST_CASE ("ChordMemory: store/isOccupied/clear auf gueltigen Slots", "[grid]")
{
    grid::ChordMemory memory;

    REQUIRE_FALSE (memory.anyOccupied());
    REQUIRE_FALSE (memory.isOccupied (0));
    REQUIRE (memory.slot (0).empty());

    REQUIRE (memory.store (0, { { 0.25f, 0.5f, 0.1f, -0.05f, true } }));
    REQUIRE (memory.isOccupied (0));
    REQUIRE (memory.anyOccupied());
    REQUIRE_FALSE (memory.isOccupied (1));

    REQUIRE (memory.store (grid::ChordMemory::kNumSlots - 1, { grid::StoredSun{} }));
    REQUIRE (memory.isOccupied (grid::ChordMemory::kNumSlots - 1));

    memory.clear (0);
    REQUIRE_FALSE (memory.isOccupied (0));
    REQUIRE (memory.slot (0).empty());
    REQUIRE (memory.anyOccupied());   // Slot 7 ist noch belegt

    memory.clear (grid::ChordMemory::kNumSlots - 1);
    REQUIRE_FALSE (memory.anyOccupied());
}

TEST_CASE ("ChordMemory: Grenzen (Slot -1/8) und leere Listen werden abgewiesen", "[grid]")
{
    grid::ChordMemory memory;

    REQUIRE_FALSE (memory.store (-1, { grid::StoredSun{} }));
    REQUIRE_FALSE (memory.store (grid::ChordMemory::kNumSlots, { grid::StoredSun{} }));
    REQUIRE_FALSE (memory.store (0, {}));   // leere Liste
    REQUIRE_FALSE (memory.anyOccupied());

    REQUIRE_FALSE (memory.isOccupied (-1));
    REQUIRE_FALSE (memory.isOccupied (grid::ChordMemory::kNumSlots));

    memory.clear (-1);                            // kein Effekt, kein Crash
    memory.clear (grid::ChordMemory::kNumSlots);

    // slot() liefert bei ungültigem Index die leere statische Liste.
    REQUIRE (memory.slot (-1).empty());
    REQUIRE (memory.slot (grid::ChordMemory::kNumSlots).empty());
}

TEST_CASE ("ChordMemory: belegte Slots werden nicht ueberschrieben (nur via clear)", "[grid]")
{
    grid::ChordMemory memory;

    REQUIRE (memory.store (2, { { 0.5f, 0.5f, 0.0f, 0.0f, false } }));
    REQUIRE_FALSE (memory.store (2, { { 0.9f, 0.9f, 0.0f, 0.0f, false } }));

    // Der ursprüngliche Inhalt bleibt unangetastet.
    REQUIRE (memory.slot (2).size() == 1);
    REQUIRE (juce::exactlyEqual (memory.slot (2)[0].x, 0.5f));

    memory.clear (2);
    REQUIRE (memory.store (2, { { 0.9f, 0.9f, 0.0f, 0.0f, false } }));
    REQUIRE (juce::exactlyEqual (memory.slot (2)[0].x, 0.9f));
}

TEST_CASE ("ChordMemory: slot() liefert die gespeicherten Sonnen unveraendert", "[grid]")
{
    grid::ChordMemory memory;

    const std::vector<grid::StoredSun> suns { { 0.1f, 0.2f, 0.05f, -0.03f, true },
                                              { 0.7f, 0.9f, 0.0f, 0.0f, false } };
    REQUIRE (memory.store (3, suns));

    const auto& stored = memory.slot (3);
    REQUIRE (stored.size() == 2);

    REQUIRE (juce::exactlyEqual (stored[0].x, 0.1f));
    REQUIRE (juce::exactlyEqual (stored[0].y, 0.2f));
    REQUIRE (juce::exactlyEqual (stored[0].ox, 0.05f));
    REQUIRE (juce::exactlyEqual (stored[0].oy, -0.03f));
    REQUIRE (stored[0].hasOrbit);

    REQUIRE (juce::exactlyEqual (stored[1].x, 0.7f));
    REQUIRE (juce::exactlyEqual (stored[1].y, 0.9f));
    REQUIRE_FALSE (stored[1].hasOrbit);
}
