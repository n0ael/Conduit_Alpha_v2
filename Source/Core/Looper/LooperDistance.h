#pragma once

#include <algorithm>
#include <cmath>

namespace conduit::looper
{

//==============================================================================
/**
    Pure Distanz-Mathe des Track-Mixers (07/2026) — nach „Monolake
    Distance" (R. Henke, ml.distance): die Y-Achse des XY-Panners macht
    das Signal mit wachsender Entfernung d ∈ [0, 1] dumpfer (High-Shelf-
    Absenkung + Tiefpass Richtung Hi Cut), schmaler (M/S-Width) und
    leiser (Vol-Kurve bis zur kompletten Stille bei d = 1 — User-
    Entscheidung 20.07.2026: die Y-Achse ist der primäre Pegelweg).

    Bewusst JUCE-frei (Muster LooperClipMath): LooperBank und Tests
    teilen exakt dieselbe Arithmetik. Filter sind RBJ-Biquads (Audio EQ
    Cookbook) in Transposed Direct Form II; die Koeffizienten berechnet
    der Audio-Thread EINMAL pro Block aus der geslewten Distanz —
    allocation- und lock-frei, Topologie wie das line~-Smoothing des
    Originals.
*/

/** Globale Distanz-Parameter (Spiegel der LooperSettings-Werte). */
struct DistanceGlobals
{
    float hiDumpDb   = 9.0f;      // Shelf-Absenkung bei d=1 (0..18 dB)
    float hiCutHz    = 8000.0f;   // Tiefpass-Ziel bei d=1 (bis 16 kHz)
    float baseFreqHz = 2000.0f;   // Shelf-Eckfrequenz (200 Hz..4 kHz)
    float width01    = 0.5f;      // verbleibende Stereo-Breite bei d=1
    bool  volDumpOn  = true;      // Pegelabfall aktiv (Default AN)
    float volDumpDb  = 12.0f;     // zusätzliche dB-Neigung der Vol-Kurve
};

struct BiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
struct BiquadState  { float z1 = 0.0f, z2 = 0.0f; };

/** TDF2-Schritt; Koeffizienten sind auf a0 normiert. */
[[nodiscard]] inline float processBiquad (BiquadState& s, const BiquadCoeffs& c,
                                          float x) noexcept
{
    const auto y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

/** Denormal-Schutz für die Filter-Zustände (Blockende — Test-Rigs laufen
    ohne ScopedNoDenormals). */
inline void snapStateToZero (BiquadState& s) noexcept
{
    if (s.z1 > -1.0e-8f && s.z1 < 1.0e-8f) s.z1 = 0.0f;
    if (s.z2 > -1.0e-8f && s.z2 < 1.0e-8f) s.z2 = 0.0f;
}

[[nodiscard]] inline double dbToGain (double db) noexcept
{
    return std::pow (10.0, db / 20.0);
}

//==============================================================================
// Distanz → Parameter-Mappings (alle monoton in d)

/** Tiefpass-Cutoff: d=0 → offen (0.45·sr, max 20 kHz), d=1 → hiCutHz;
    exponentielle Interpolation (gehörrichtig, amxd-Expo-Charakter). */
[[nodiscard]] inline double lowpassCutoffHz (double d, double hiCutHz,
                                             double sampleRate) noexcept
{
    const auto open = std::min (20000.0, 0.45 * sampleRate);
    const auto target = std::clamp (hiCutHz, 20.0, open);
    return open * std::pow (target / open, std::clamp (d, 0.0, 1.0));
}

/** High-Shelf-Gain: linear von 0 dB (d=0) auf −hiDumpDb (d=1). */
[[nodiscard]] inline double shelfGainDb (double d, double hiDumpDb) noexcept
{
    return -hiDumpDb * std::clamp (d, 0.0, 1.0);
}

/** Side-Faktor der M/S-Stufe: 1 (d=0) → width01 (d=1). */
[[nodiscard]] inline double widthFactor (double d, double width01) noexcept
{
    return 1.0 + (std::clamp (width01, 0.0, 1.0) - 1.0) * std::clamp (d, 0.0, 1.0);
}

/** Vol-Kurve: Equal-Power-Fade cos(d·π/2) — erreicht bei d=1 EXAKT 0
    (Stille) — mal zusätzlicher dB-Neigung volDumpDb·d. Aus = konstant 1. */
[[nodiscard]] inline double volDumpGain (double d, bool on, double volDumpDb) noexcept
{
    if (! on)
        return 1.0;

    const auto clamped = std::clamp (d, 0.0, 1.0);
    if (clamped >= 1.0)
        return 0.0;   // cos(π/2) wäre nur ~6e-17 — Stille explizit

    return std::cos (clamped * 1.5707963267948966)
         * dbToGain (-volDumpDb * clamped);
}

//==============================================================================
// RBJ-Koeffizienten (Audio EQ Cookbook), normiert auf a0

/** Tiefpass, Q = 1/√2 (Butterworth). */
[[nodiscard]] inline BiquadCoeffs makeLowpass (double cutoffHz, double sampleRate) noexcept
{
    const auto nyquist = 0.49 * sampleRate;
    const auto w0 = 2.0 * 3.141592653589793 * std::clamp (cutoffHz, 20.0, nyquist)
                  / sampleRate;
    const auto cosw = std::cos (w0);
    const auto alpha = std::sin (w0) / (2.0 * 0.70710678118654752);

    const auto a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = static_cast<float> ((1.0 - cosw) * 0.5 / a0);
    c.b1 = static_cast<float> ((1.0 - cosw) / a0);
    c.b2 = c.b0;
    c.a1 = static_cast<float> (-2.0 * cosw / a0);
    c.a2 = static_cast<float> ((1.0 - alpha) / a0);
    return c;
}

/** High-Shelf bei cornerHz mit gainDb (S = 1). */
[[nodiscard]] inline BiquadCoeffs makeHighShelf (double cornerHz, double gainDb,
                                                 double sampleRate) noexcept
{
    const auto nyquist = 0.49 * sampleRate;
    const auto w0 = 2.0 * 3.141592653589793 * std::clamp (cornerHz, 20.0, nyquist)
                  / sampleRate;
    const auto cosw = std::cos (w0);
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);   // S = 1
    const auto twoSqrtAAlpha = 2.0 * std::sqrt (A) * alpha;

    const auto a0 = (A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha;
    BiquadCoeffs c;
    c.b0 = static_cast<float> (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha) / a0);
    c.b1 = static_cast<float> (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0);
    c.b2 = static_cast<float> (A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha) / a0);
    c.a1 = static_cast<float> (2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0);
    c.a2 = static_cast<float> (((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha) / a0);
    return c;
}

} // namespace conduit::looper
