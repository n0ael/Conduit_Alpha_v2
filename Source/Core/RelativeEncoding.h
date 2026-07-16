#pragma once

#include <juce_core/juce_core.h>

namespace conduit::midirig
{

//==============================================================================
/** Kodierung eines Endlos-Encoders (MIDI-Rig M8, Feldtest 16.07.2026) --
    GERAETEABHAENGIG und daher rein profil-getrieben (CSV-Spalte
    `rel_encoding`), nie im Code pro Geraet verzweigt.

    Es gibt genau drei uebliche Verfahren (dieselbe Dreiteilung wie in
    Abletons Remote-Scripts: "Relative (2's Comp.)" / "(Signed Bit)" /
    "(Bin Offset)"). Alle drei sind fuer POSITIVE Werte identisch -- der
    Unterschied faellt erst beim Zurueckdrehen auf (Feldtest-Lektion:
    das AlphaTrack lief hoch, sprang aber beim Zurueckdrehen auf 0). */
enum class RelativeEncoding
{
    /** 1..63 = +1..+63, 65..127 = -63..-1 (Wert − 128). Default; Annahme
        fuer das Xone:K1. */
    twosComplement,

    /** Bit 6 (0x40) = Vorzeichen, Bits 0..5 = Betrag: 0x01..0x3f = +1..+63,
        0x41..0x7f = -1..-63. **Frontier AlphaTrack** (per Native-Mode-Doku
        verifiziert: 0x43 = 3 Ticks gegen den Uhrzeigersinn). */
    signBit,

    /** 64 = 0, 65..127 = +1..+63, 0..63 = -64..-1 (Wert − 64). */
    binaryOffset
};

/** Relatives Encoder-Event dekodieren (pur, testbar). */
[[nodiscard]] inline int decodeRelativeDelta (int value7bit, RelativeEncoding encoding) noexcept
{
    const auto v = juce::jlimit (0, 127, value7bit);

    switch (encoding)
    {
        case RelativeEncoding::signBit:
            // 0x40 = Vorzeichen, 0x3f = Betrag. 0x00/0x40 = Betrag 0.
            return (v & 0x40) != 0 ? -(v & 0x3f) : (v & 0x3f);

        case RelativeEncoding::binaryOffset:
            return v - 64;

        case RelativeEncoding::twosComplement:
            break;
    }

    if (v == 0 || v == 64)
        return 0;

    return v <= 63 ? v : v - 128;   // 65..127 -> -63..-1
}

/** CSV-Spalte `rel_encoding` -> Enum (leer/unbekannt = twosComplement,
    rueckwaertskompatibel: Profile ohne die Spalte verhalten sich wie M7). */
[[nodiscard]] inline RelativeEncoding parseRelativeEncoding (const juce::String& text) noexcept
{
    if (text.equalsIgnoreCase ("signbit") || text.equalsIgnoreCase ("sign"))
        return RelativeEncoding::signBit;

    if (text.equalsIgnoreCase ("binoffset") || text.equalsIgnoreCase ("bin"))
        return RelativeEncoding::binaryOffset;

    return RelativeEncoding::twosComplement;
}

} // namespace conduit::midirig
