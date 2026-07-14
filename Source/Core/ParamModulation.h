#pragma once

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
/**
    Macro-Modulation von Modul-Parametern (MIDI-Rig M5c): der ValueTree
    behält den BASISWERT (User-Fader souverän, Presets/OSC-Send sehen nur
    die Basis), der GraphManager verrechnet einen normalisierten Offset
    und schreibt den EFFEKTIVWERT in das bestehende
    getParameterTarget()-Atomic (dokumentierte Erweiterung des
    Dual-State-Musters 6.1 — Präzedenz: OSC-Fastpath schreibt das Atomic
    ebenfalls am Tree vorbei). Kein Chassis-Umbau, kein Audio-Thread-Code;
    funktioniert uniform für FX- und Nicht-FX-Module. Message Thread.

    Polarität (User-Entscheidung 14.07.2026): unipolar (+) moduliert vom
    Basiswert nach oben, bipolar (±) um den Basiswert herum — beide über
    amount [0..1] skaliert, geclamped auf User- und Hard-Range.
*/

//==============================================================================
/** Schlüssel einer Parameter-Modulation: persistenter Node-Uuid (CLAUDE.md
    §6) + paramId — bewusst KEIN Modul-Pointer (Zombie-Regel 5.3). */
struct ParamModKey
{
    juce::String nodeUuid;
    juce::String paramId;

    bool operator< (const ParamModKey& other) const noexcept
    {
        const auto nodeCompare = nodeUuid.compare (other.nodeUuid);
        return nodeCompare != 0 ? nodeCompare < 0
                                : paramId.compare (other.paramId) < 0;
    }

    bool operator== (const ParamModKey& other) const noexcept
    {
        return nodeUuid == other.nodeUuid && paramId == other.paramId;
    }
};

//==============================================================================
/** Macro-Wert [0..1] → normalisierter Offset [-1..+1]:
    unipolar: v01 · amount (nur aufwärts), bipolar: (v01−0.5)·2·amount. */
[[nodiscard]] inline float computeOffsetNorm (float value01, bool bipolar, float amount) noexcept
{
    const auto v = juce::jlimit (0.0f, 1.0f, value01);
    const auto a = juce::jlimit (0.0f, 1.0f, amount);
    return bipolar ? (v - 0.5f) * 2.0f * a : v * a;
}

/** Basis + Offset·User-Range, geclamped auf User- UND Hard-Range (das
    Doppel-Clamp ist idempotent; degenerierte Ranges kollabieren sauber). */
[[nodiscard]] inline float computeModulatedValue (float base, float offsetNorm,
                                                  float userMin, float userMax,
                                                  float hardMin, float hardMax) noexcept
{
    const auto lowUser  = juce::jmin (userMin, userMax);
    const auto highUser = juce::jmax (userMin, userMax);
    const auto lowHard  = juce::jmin (hardMin, hardMax);
    const auto highHard = juce::jmax (hardMin, hardMax);

    const auto effective = base + offsetNorm * (highUser - lowUser);
    return juce::jlimit (lowHard, highHard, juce::jlimit (lowUser, highUser, effective));
}

//==============================================================================
/** Senke der Parameter-Modulation (implementiert vom GraphManager) —
    entkoppelt die Macro-Targets vom GraphManager (Fake-Sink in Tests).
    Alle Methoden: Message Thread. */
class IParamModulationSink
{
public:
    virtual ~IParamModulationSink() = default;

    /** Offset [-1..+1] setzen — merkt sich den Wert auch, wenn das Modul
        (noch) nicht materialisiert ist (Store dann No-op, Re-Apply beim
        naechsten syncParameterValue/Rebuild). */
    virtual void setParamModulation (const ParamModKey& key, float offsetNorm) = 0;

    /** Modulation entfernen — der Basiswert kehrt ins Atomic zurück. */
    virtual void clearParamModulation (const ParamModKey& key) = 0;
};

} // namespace conduit
