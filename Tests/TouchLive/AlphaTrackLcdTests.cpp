#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "TouchLive/AlphaTrackLcd.h"

using conduit::AlphaTrackLcd;

namespace
{
    struct LcdRig
    {
        std::vector<juce::MidiMessage> sent;
        AlphaTrackLcd lcd { [this] (const juce::MidiMessage& m) { sent.push_back (m); } };

        /** Rohbytes inkl. F0/F7. */
        [[nodiscard]] std::vector<juce::uint8> bytesOf (size_t index) const
        {
            const auto& m = sent[index];
            return { m.getRawData(), m.getRawData() + m.getRawDataSize() };
        }

        /** ASCII-Nutzlast (nach Header+pos, vor F7). */
        [[nodiscard]] juce::String textOf (size_t index) const
        {
            const auto bytes = bytesOf (index);
            juce::String text;
            for (size_t i = 7; i + 1 < bytes.size(); ++i)
                text << (juce::juce_wchar) bytes[i];
            return text;
        }

        [[nodiscard]] int positionOf (size_t index) const
        {
            return (int) bytesOf (index)[6];
        }
    };
}

TEST_CASE ("AlphaTrackLcd: erster Tick zeichnet beide Zeilen mit korrektem Framing", "[bridge][lcd]")
{
    LcdRig rig;
    rig.lcd.setLine (0, "Drums");
    rig.lcd.setLine (1, "17.3");
    rig.lcd.tick();

    REQUIRE (rig.sent.size() == 2);   // eine Nachricht pro Zeile

    // Framing byte-genau: F0 00 01 40 20 00 <pos> <ascii...> F7
    const auto bytes = rig.bytesOf (0);
    REQUIRE (bytes.size() >= 8);
    CHECK (bytes[0] == 0xf0);
    CHECK (bytes[1] == 0x00);
    CHECK (bytes[2] == 0x01);
    CHECK (bytes[3] == 0x40);
    CHECK (bytes[4] == 0x20);
    CHECK (bytes[5] == 0x00);
    CHECK (bytes.back() == 0xf7);

    CHECK (rig.positionOf (0) == 0x00);
    CHECK (rig.positionOf (1) == 0x10);   // Zeile 2 = pos 0x10
    CHECK (rig.textOf (0) == "Drums           ");   // auf 16 gepolstert
    CHECK (rig.textOf (1) == "17.3            ");
}

TEST_CASE ("AlphaTrackLcd: unveraenderter Inhalt sendet nichts", "[bridge][lcd]")
{
    LcdRig rig;
    rig.lcd.setLine (0, "Drums");
    rig.lcd.tick();
    rig.sent.clear();

    rig.lcd.setLine (0, "Drums");
    rig.lcd.tick();
    rig.lcd.tick();

    CHECK (rig.sent.empty());
}

TEST_CASE ("AlphaTrackLcd: Diff sendet nur den geaenderten Bereich", "[bridge][lcd]")
{
    LcdRig rig;
    rig.lcd.setLine (1, "1.1");
    rig.lcd.tick();
    rig.sent.clear();

    rig.lcd.setLine (1, "1.2");   // nur Spalte 2 aendert sich
    rig.lcd.tick();

    REQUIRE (rig.sent.size() == 1);
    CHECK (rig.positionOf (0) == 0x12);   // Zeile 2, Spalte 2
    CHECK (rig.textOf (0) == "2");
}

TEST_CASE ("AlphaTrackLcd: Nicht-ASCII wird sanitisiert, Ueberlaenge gekappt", "[bridge][lcd]")
{
    LcdRig rig;
    rig.lcd.setLine (0, juce::String::fromUTF8 ("Tr\xc3\xa4um") + "0123456789ABCDEF");
    rig.lcd.tick();

    // Erster Tick zeichnet IMMER beide Zeilen (Display-Clear inklusive).
    REQUIRE (rig.sent.size() == 2);
    const auto text = rig.textOf (0);
    CHECK (text.length() == 16);
    CHECK (text.startsWith ("Tr?um"));   // Umlaut -> '?'
}

TEST_CASE ("AlphaTrackLcd: forceRedraw sendet den kompletten Stand erneut", "[bridge][lcd]")
{
    LcdRig rig;
    rig.lcd.setLine (0, "Drums");
    rig.lcd.tick();
    rig.sent.clear();

    rig.lcd.forceRedraw();
    rig.lcd.tick();

    REQUIRE (rig.sent.size() == 2);   // beide Zeilen neu (auch die leere)
    CHECK (rig.textOf (0) == "Drums           ");
}

TEST_CASE ("AlphaTrackLcd: nativeModeForce hat die dokumentierten Bytes", "[bridge][lcd]")
{
    const auto message = AlphaTrackLcd::nativeModeForce();
    const auto* raw = message.getRawData();
    const std::vector<juce::uint8> expected { 0xf0, 0x00, 0x01, 0x40, 0x20, 0x01, 0x00, 0xf7 };
    REQUIRE (message.getRawDataSize() == (int) expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK (raw[i] == expected[i]);
}
