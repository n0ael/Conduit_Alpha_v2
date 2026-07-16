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

//==============================================================================
// MIDI-Rig M8: Relativ-Kodierungen (geraeteabhaengig, Feldtest 16.07.2026)

TEST_CASE ("M8: decodeRelativeDelta -- Zweierkomplement (Default, K1)", "[midirig][m8]")
{
    using namespace conduit::midirig;
    const auto twos = RelativeEncoding::twosComplement;

    CHECK (decodeRelativeDelta (0, twos)   == 0);
    CHECK (decodeRelativeDelta (64, twos)  == 0);
    CHECK (decodeRelativeDelta (1, twos)   == 1);
    CHECK (decodeRelativeDelta (63, twos)  == 63);
    CHECK (decodeRelativeDelta (127, twos) == -1);
    CHECK (decodeRelativeDelta (65, twos)  == -63);

    // Wrapper haelt das M7-Verhalten bei.
    CHECK (ChannelStripLayers::decodeSignedDelta (127) == -1);
    CHECK (ChannelStripLayers::decodeSignedDelta (4)   == 4);
}

TEST_CASE ("M8: decodeRelativeDelta -- Sign-Magnitude (AlphaTrack)", "[midirig][m8]")
{
    using namespace conduit::midirig;
    const auto sign = RelativeEncoding::signBit;

    // Beispiele aus der AlphaTrack-Native-Mode-Doku v1.0:
    //   Wert 0x04 = 4 Ticks im Uhrzeigersinn
    //   Wert 0x43 = 3 Ticks GEGEN den Uhrzeigersinn
    CHECK (decodeRelativeDelta (0x04, sign) == 4);
    CHECK (decodeRelativeDelta (0x43, sign) == -3);

    // Doku-Bereiche: vorwaerts 0x01-0x3f, rueckwaerts 0x41-0x7f.
    CHECK (decodeRelativeDelta (0x01, sign) == 1);
    CHECK (decodeRelativeDelta (0x3f, sign) == 63);
    CHECK (decodeRelativeDelta (0x41, sign) == -1);
    CHECK (decodeRelativeDelta (0x7f, sign) == -63);
    CHECK (decodeRelativeDelta (0x00, sign) == 0);
    CHECK (decodeRelativeDelta (0x40, sign) == 0);

    // Der Bug-Kern (Feldtest): "1 Tick zurueck" als Zweierkomplement gelesen
    // waere -63 -- der Wert faellt auf 0 statt einen Schritt zurueckzugehen.
    CHECK (decodeRelativeDelta (0x41, RelativeEncoding::twosComplement) == -63);
}

TEST_CASE ("M8: decodeRelativeDelta -- Binary Offset", "[midirig][m8]")
{
    using namespace conduit::midirig;
    const auto bin = RelativeEncoding::binaryOffset;

    CHECK (decodeRelativeDelta (64, bin)  == 0);
    CHECK (decodeRelativeDelta (65, bin)  == 1);
    CHECK (decodeRelativeDelta (127, bin) == 63);
    CHECK (decodeRelativeDelta (63, bin)  == -1);
    CHECK (decodeRelativeDelta (0, bin)   == -64);
}

TEST_CASE ("M8: parseRelativeEncoding -- leer/unbekannt = Zweierkomplement", "[midirig][m8]")
{
    using namespace conduit::midirig;

    CHECK (parseRelativeEncoding ("")         == RelativeEncoding::twosComplement);
    CHECK (parseRelativeEncoding ("twos")     == RelativeEncoding::twosComplement);
    CHECK (parseRelativeEncoding ("quatsch")  == RelativeEncoding::twosComplement);
    CHECK (parseRelativeEncoding ("signbit")  == RelativeEncoding::signBit);
    CHECK (parseRelativeEncoding ("SignBit")  == RelativeEncoding::signBit);
    CHECK (parseRelativeEncoding ("sign")     == RelativeEncoding::signBit);
    CHECK (parseRelativeEncoding ("binoffset") == RelativeEncoding::binaryOffset);
    CHECK (parseRelativeEncoding ("bin")      == RelativeEncoding::binaryOffset);
}

TEST_CASE ("M8: Ebenen-Selektor respektiert die Profil-Kodierung", "[midirig][m8]")
{
    using namespace conduit::midirig;
    ChannelStripLayers layers;

    // Sign-Magnitude-Geraet: 8 Ticks vor = Ebene 1, dann 8 zurueck = Ebene 0.
    for (int i = 0; i < 8; ++i)
        layers.feed ("col1", 0x01, RelativeEncoding::signBit);
    CHECK (layers.layerFor ("col1") == 1);

    for (int i = 0; i < 8; ++i)
        layers.feed ("col1", 0x41, RelativeEncoding::signBit);   // je -1
    CHECK (layers.layerFor ("col1") == 0);
}
