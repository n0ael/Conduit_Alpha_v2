#pragma once
#include <array>
#include <juce_core/juce_core.h>
#include "Core/ResponseCurve.h"
#include "Core/VoiceAllocator.h"   // kMaxVoices

namespace conduit::grid
{

/** Eine globale Ausdrucks-Achse über allen Stimmen: pro Voice-Slot ein
    ungeklemmter Rohwert plus ein bipolarer Offset. combined() ist der
    einzige Ort, an dem Rohwert + Offset zum finalen Ausgangswert werden
    (Clamp auf [outMin, outMax]) — künftiger Einhängepunkt für
    Response-Kurven / Min-Max / Skalen-Magnet. Message Thread. */
class ExpressionAxis
{
public:
    struct Config
    {
        float outMin      = 0.0f;   // Pressure/Slide: 0..1; PitchBend später: -range..+range
        float outMax      = 1.0f;
        float offsetScale = 1.0f;   // bipolarer Offset-Bereich (Pressure/Slide: 1.0)
    };

    // Clang lehnt eine verschachtelte Config mit In-Class-Defaults als
    // Default-Argument ab (CLAUDE.md 2) — Default-Ctor delegiert deshalb in
    // der .cpp, wo ExpressionAxis schon vollständig ist.
    ExpressionAxis() noexcept;
    explicit ExpressionAxis (const Config& cfg) noexcept;

    /** Rohwert einer Stimme setzen (ungeklemmt gespeichert). */
    void  setRaw (int voiceIndex, float rawValue) noexcept;
    /** Globalen Offset setzen (bipolar, geklemmt auf ±offsetScale). */
    void  setOffset (float bipolarOffset) noexcept;
    float offset() const noexcept;

    /** Stimme aktivieren/deaktivieren (steuert, welche Slots die
        Neuberechnung nach setOffset erfasst). activate setzt den Rohwert
        auf 0. */
    void activate   (int voiceIndex) noexcept;
    void deactivate (int voiceIndex) noexcept;
    bool isActive   (int voiceIndex) const noexcept;

    /** Finaler Ausgangswert der Stimme: clamp(curve.apply(raw) + offset,
        outMin, outMax) -- die Kurve formt VOR dem Offset (Default-Kurve =
        Identität, also verhaltensneutral). */
    float combined (int voiceIndex) const noexcept;

    void reset() noexcept;   // alle Slots inaktiv, Rohwerte 0 — Offset bleibt

    /** Zugriff auf die Response-Kurve dieser Achse (Nutzung durch das
        spätere Kurven-Panel, S2). */
    ResponseCurve& responseCurve() noexcept { return curve; }

private:
    Config config;
    float  axisOffset { 0.0f };
    ResponseCurve curve;
    std::array<float, (size_t) VoiceAllocator::kMaxVoices> raw {};
    std::array<bool,  (size_t) VoiceAllocator::kMaxVoices> active {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionAxis)
};

} // namespace conduit::grid
