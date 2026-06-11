#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
/** Globale Session-Tonalität (Ableton-artig): liegt als Root-Tree-Properties
    scaleRoot/scaleType im Patch (Schema 6.2) und reist pro Block im
    ClockState zu den Modulen. */
enum class ScaleType : int
{
    chromatic = 0,
    major,
    minor,        // natürlich Moll
    pentatonic    // Dur-Pentatonik
};

[[nodiscard]] inline juce::String toString (ScaleType type)
{
    switch (type)
    {
        case ScaleType::chromatic:  return "chromatic";
        case ScaleType::major:      return "major";
        case ScaleType::minor:      return "minor";
        case ScaleType::pentatonic: return "pentatonic";
    }

    jassertfalse;
    return "chromatic";
}

[[nodiscard]] inline ScaleType scaleTypeFromString (const juce::String& text)
{
    if (text == "major")      return ScaleType::major;
    if (text == "minor")      return ScaleType::minor;
    if (text == "pentatonic") return ScaleType::pentatonic;
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
        switch (type)
        {
            case ScaleType::chromatic:  return 0b111111111111;
            case ScaleType::major:      return 0b101010110101;  // 0 2 4 5 7 9 11
            case ScaleType::minor:      return 0b010101101101;  // 0 2 3 5 7 8 10
            case ScaleType::pentatonic: return 0b001010010101;  // 0 2 4 7 9
        }

        return 0b111111111111;
    }

    [[nodiscard]] constexpr bool isInScale (int semitoneAboveRoot, ScaleType type) noexcept
    {
        return (maskFor (type) & (1 << (((semitoneAboveRoot % 12) + 12) % 12))) != 0;
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

        return cv01;  // unerreichbar (volle Maske garantiert Treffer)
    }
} // namespace scale

} // namespace conduit
