#pragma once

#include <cmath>
#include <initializer_list>

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
/** Globale Session-Tonalität (Ableton-artig): liegt als Root-Tree-Properties
    scaleRoot/scaleType im Patch (Schema 6.2) und reist pro Block im
    ClockState zu den Modulen (scaleTypeIndex = int-Cast dieses Enums).

    Skalen-Vollausbau (Block I, docs/DataModel.md): die 25 Scale-Presets in
    Ableton-/Push-Reihenfolge (Major … Spanish, 12-Bit-Maske pro Skala,
    verifiziert gegen die Push-Skalenliste/Live Scale Awareness 11.3+) plus
    chromatic (= „Skala aus", Index 0 — Kompatibilität mit Bestand). Die
    Enum-Reihenfolge IST der Index in scale::kScaleInfos — nie umsortieren
    (ClockState/Serialisierungs-Strings hängen an den IDs, nicht an den
    Indizes; alte Presets mit "pentatonic" laden als majorPentatonic). */
enum class ScaleType : int
{
    chromatic = 0,
    major,
    minor,             // natürlich Moll
    dorian,
    mixolydian,
    lydian,
    phrygian,
    locrian,
    diminished,        // half-whole
    wholeHalf,
    wholeTone,
    minorBlues,
    minorPentatonic,
    majorPentatonic,   // ehemals "pentatonic"
    harmonicMinor,
    melodicMinor,
    superLocrian,
    bhairav,
    hungarianMinor,
    minorGypsy,
    hirojoshi,
    inSen,
    iwato,
    kumoi,
    pelog,
    spanish
};

//==============================================================================
namespace scale
{
    inline constexpr int numScaleTypes = 26;   // chromatic + 25 Ableton-Presets

    /** 12-Bit-Maske aus Halbton-Liste (Bit n = Halbton n über dem Root). */
    [[nodiscard]] constexpr int makeMask (std::initializer_list<int> semitones) noexcept
    {
        int mask = 0;
        for (const auto semitone : semitones)
            mask |= 1 << (((semitone % 12) + 12) % 12);
        return mask;
    }

    struct ScaleInfo
    {
        const char* id;            // Serialisierungs-String (stabil, nie ändern)
        const char* displayName;   // UI-Name (Ableton-Schreibweise)
        int mask;
    };

    /** Index == (int) ScaleType — Reihenfolge Ableton/Push (docs/DataModel.md). */
    inline constexpr ScaleInfo kScaleInfos[numScaleTypes] = {
        { "chromatic",       "Chromatic",        0b111111111111 },
        { "major",           "Major",            makeMask ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "minor",           "Minor",            makeMask ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "dorian",          "Dorian",           makeMask ({ 0, 2, 3, 5, 7, 9, 10 }) },
        { "mixolydian",      "Mixolydian",       makeMask ({ 0, 2, 4, 5, 7, 9, 10 }) },
        { "lydian",          "Lydian",           makeMask ({ 0, 2, 4, 6, 7, 9, 11 }) },
        { "phrygian",        "Phrygian",         makeMask ({ 0, 1, 3, 5, 7, 8, 10 }) },
        { "locrian",         "Locrian",          makeMask ({ 0, 1, 3, 5, 6, 8, 10 }) },
        { "diminished",      "Diminished",       makeMask ({ 0, 1, 3, 4, 6, 7, 9, 10 }) },
        { "wholeHalf",       "Whole-half",       makeMask ({ 0, 2, 3, 5, 6, 8, 9, 11 }) },
        { "wholeTone",       "Whole Tone",       makeMask ({ 0, 2, 4, 6, 8, 10 }) },
        { "minorBlues",      "Minor Blues",      makeMask ({ 0, 3, 5, 6, 7, 10 }) },
        { "minorPentatonic", "Minor Pentatonic", makeMask ({ 0, 3, 5, 7, 10 }) },
        { "majorPentatonic", "Major Pentatonic", makeMask ({ 0, 2, 4, 7, 9 }) },
        { "harmonicMinor",   "Harmonic Minor",   makeMask ({ 0, 2, 3, 5, 7, 8, 11 }) },
        { "melodicMinor",    "Melodic Minor",    makeMask ({ 0, 2, 3, 5, 7, 9, 11 }) },
        { "superLocrian",    "Super Locrian",    makeMask ({ 0, 1, 3, 4, 6, 8, 10 }) },
        { "bhairav",         "Bhairav",          makeMask ({ 0, 1, 4, 5, 7, 8, 11 }) },
        { "hungarianMinor",  "Hungarian Minor",  makeMask ({ 0, 2, 3, 6, 7, 8, 11 }) },
        { "minorGypsy",      "Minor Gypsy",      makeMask ({ 0, 1, 4, 5, 7, 8, 10 }) },
        { "hirojoshi",       "Hirojoshi",        makeMask ({ 0, 2, 3, 7, 8 }) },
        { "inSen",           "In-Sen",           makeMask ({ 0, 1, 5, 7, 10 }) },
        { "iwato",           "Iwato",            makeMask ({ 0, 1, 5, 6, 10 }) },
        { "kumoi",           "Kumoi",            makeMask ({ 0, 2, 3, 7, 9 }) },
        { "pelog",           "Pelog",            makeMask ({ 0, 1, 3, 7, 8 }) },
        { "spanish",         "Spanish",          makeMask ({ 0, 1, 3, 4, 5, 6, 8, 10 }) },
    };

    [[nodiscard]] constexpr int clampedIndex (int scaleTypeIndex) noexcept
    {
        return scaleTypeIndex >= 0 && scaleTypeIndex < numScaleTypes ? scaleTypeIndex : 0;
    }
} // namespace scale

[[nodiscard]] inline juce::String toString (ScaleType type)
{
    return scale::kScaleInfos[scale::clampedIndex (static_cast<int> (type))].id;
}

/** UI-Name in Ableton-Schreibweise ("Whole-half", "In-Sen" …). */
[[nodiscard]] inline juce::String scaleDisplayName (ScaleType type)
{
    return scale::kScaleInfos[scale::clampedIndex (static_cast<int> (type))].displayName;
}

[[nodiscard]] inline ScaleType scaleTypeFromString (const juce::String& text)
{
    for (int i = 0; i < scale::numScaleTypes; ++i)
        if (text == scale::kScaleInfos[i].id)
            return static_cast<ScaleType> (i);

    // Legacy (Bestands-Presets vor Block I): "pentatonic" war die
    // Dur-Pentatonik.
    if (text == "pentatonic")
        return ScaleType::majorPentatonic;

    return ScaleType::chromatic;
}

//==============================================================================
namespace scale
{
    /** Pitch-Konvention: CV-float 0–1 überspannt 5 Oktaven → 1 Halbton = 1/60.
        Das Volt-Mapping passiert erst im HardwareIO (Kalibrierung, 8). */
    inline constexpr double semitonesPerUnit = 60.0;

    /** Skalen-Maske: Bit n = Halbton n über dem Grundton gehört zur Skala. */
    [[nodiscard]] constexpr int maskFor (ScaleType type) noexcept
    {
        return kScaleInfos[clampedIndex (static_cast<int> (type))].mask;
    }

    [[nodiscard]] constexpr bool isInScale (int semitoneAboveRoot, ScaleType type) noexcept
    {
        return (maskFor (type) & (1 << (((semitoneAboveRoot % 12) + 12) % 12))) != 0;
    }

    /** Skalen-Stufen-Anzeige (VARI Display „Scale Degrees", 07/2026):
        Intervall-Name relativ zum Grundton über die feste ♭-Tabelle
        (0 = „1", 3 = „♭3", 7 = „5" …) — bewusst skalen-unabhängig
        (Intervall-Semantik). Oktav-Überträge als „8va"-Suffix, negative
        Offsets mit „−"-Präfix. */
    [[nodiscard]] inline juce::String degreeName (int semitoneOffset)
    {
        static const char* const intervalNames[12] = {
            "1", "\xe2\x99\xad""2", "2", "\xe2\x99\xad""3", "3", "4",
            "\xe2\x99\xad""5", "5", "\xe2\x99\xad""6", "6", "\xe2\x99\xad""7", "7"
        };

        const auto magnitude = semitoneOffset < 0 ? -semitoneOffset : semitoneOffset;
        auto text = juce::String::fromUTF8 (intervalNames[magnitude % 12]);

        if (const auto octaves = magnitude / 12; octaves > 0)
        {
            text << "+";
            if (octaves > 1)
                text << juce::String (octaves);
            text << "8va";
        }

        if (semitoneOffset < 0)
            text = juce::String::fromUTF8 ("\xe2\x88\x92") + text;
        return text;
    }

    /** Quantisiert einen CV-Wert (0–1) auf den nächstgelegenen Skalenton —
        pure function, allocation-free, Audio-Thread-tauglich. Bei Gleichstand
        gewinnt der höhere Ton. Wiederverwendbar für ein späteres
        Quantizer-Modul. */
    [[nodiscard]] inline float quantize (float cv01, int rootNote, ScaleType type) noexcept
    {
        // Auch 'chromatic' rastet auf Halbtöne — gewollt (Urzwerg-Verhalten)
        const auto targetSemitone = static_cast<double> (cv01) * semitonesPerUnit;
        const auto base = static_cast<int> (std::lround (targetSemitone));

        for (int offset = 0; offset <= 11; ++offset)
        {
            // Gleichstand → aufwärts (zuerst +offset prüfen)
            for (const auto candidate : { base + offset, base - offset })
            {
                if (isInScale (candidate - rootNote, type))
                    return juce::jlimit (0.0f, 1.0f,
                                         static_cast<float> (candidate / semitonesPerUnit));

                if (offset == 0)
                    break;  // +0 == -0
            }
        }

        return cv01;  // unerreichbar (keine Maske ist leer)
    }
} // namespace scale

} // namespace conduit
