#include <catch2/catch_test_macros.hpp>

#include "Core/ChannelStripLayers.h"

using namespace conduit::midirig;

namespace
{
    // Ein positiver Schritt (CW) im signed-Relativmodus.
    constexpr int kUp   = 1;     // +1
    constexpr int kDown = 127;   // -1
}

TEST_CASE ("ChannelStripLayers: signed-Relativ-Dekodierung", "[midirig]")
{
    CHECK (ChannelStripLayers::decodeSignedDelta (0)   == 0);
    CHECK (ChannelStripLayers::decodeSignedDelta (64)  == 0);
    CHECK (ChannelStripLayers::decodeSignedDelta (1)   == 1);
    CHECK (ChannelStripLayers::decodeSignedDelta (63)  == 63);
    CHECK (ChannelStripLayers::decodeSignedDelta (127) == -1);
    CHECK (ChannelStripLayers::decodeSignedDelta (65)  == -63);
}

TEST_CASE ("ChannelStripLayers: 8-Step-Zonen mit Klemmung, gespiegelt zurueck", "[midirig]")
{
    ChannelStripLayers layers;

    CHECK (layers.layerFor ("col1") == 0);   // Default: Ebene 1

    // Innerhalb der ersten Zone bleibt Ebene 0.
    for (int i = 0; i < ChannelStripLayers::kStepsPerLayer - 1; ++i)
        CHECK_FALSE (layers.feed ("col1", kUp).layerChanged);
    CHECK (layers.layerFor ("col1") == 0);

    // Der 8. Schritt (pos == 8) kippt auf Ebene 1.
    auto r = layers.feed ("col1", kUp);
    CHECK (r.layerChanged);
    CHECK (r.layer == 1);

    // 8 weitere Schritte -> Ebene 2.
    for (int i = 0; i < ChannelStripLayers::kStepsPerLayer; ++i)
        r = layers.feed ("col1", kUp);
    CHECK (r.layer == 2);

    // Am oberen Ende geklemmt: weiter drehen aendert nichts (kein Wrap).
    r = layers.feed ("col1", kUp);
    CHECK_FALSE (r.layerChanged);
    CHECK (layers.layerFor ("col1") == 2);

    // Gespiegelt zurueck: von der Spitze braucht es dieselben 8 Schritte links,
    // um die Ebene-2-Zone zu verlassen (symmetrische Hysterese, User-Spez).
    for (int i = 0; i < ChannelStripLayers::kStepsPerLayer; ++i)
        r = layers.feed ("col1", kDown);
    CHECK (r.layer == 1);
}

TEST_CASE ("ChannelStripLayers: Spalten sind unabhaengig", "[midirig]")
{
    ChannelStripLayers layers;

    for (int i = 0; i < ChannelStripLayers::kStepsPerLayer; ++i)
        layers.feed ("col1", kUp);

    CHECK (layers.layerFor ("col1") == 1);
    CHECK (layers.layerFor ("col2") == 0);   // unberuehrt
}

TEST_CASE ("ChannelStripLayers: setLayer/snapshot (Persistenz)", "[midirig]")
{
    ChannelStripLayers layers;
    layers.setLayer ("col3", 2);

    CHECK (layers.layerFor ("col3") == 2);

    const auto snap = layers.snapshot();
    REQUIRE (snap.count ("col3") == 1);
    CHECK (snap.at ("col3") == 2);

    // Nach setLayer sitzt der Akkumulator am Zonen-Anfang: ein Schritt links
    // faellt sofort auf Ebene 1 (kein Nachlauf durch alte Position).
    CHECK (layers.feed ("col3", kDown).layer == 1);

    // Ungueltige Ebene wird geklemmt.
    layers.setLayer ("col3", 9);
    CHECK (layers.layerFor ("col3") == ChannelStripLayers::kNumLayers - 1);
}
