/* ========================================
 *  AirwindowsPlugin.h — Conduit-Basis für Airwindows-Portierungen
 *  DSP-Originale: Chris Johnson / Airwindows (MIT-Lizenz)
 *  Quelle (read-only Referenz): Third-Party/airwindows
 * ======================================== */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

#include <juce_audio_basics/juce_audio_basics.h>

namespace conduit::airwindows
{

/** Beschreibt einen Airwindows-Parameter. Alle Werte folgen der
    Airwindows-Konvention 0..1; Skalierung passiert im DSP der Subklasse.
    Instanzen müssen statische Lebensdauer haben (die Basis hält nur ein span).
*/
struct ParameterInfo
{
    const char* id;           // stabiler snake_case-Schlüssel (späterer OSC-Parameter-Teil)
    const char* name;         // Anzeigename des Originals, z. B. "Clamping"
    float       defaultValue; // 0..1
};

//==============================================================================
/** Gemeinsame Basis aller portierten Airwindows-Effekte (stereo, 2 in / 2 out).

    Thread-Ownership:
      - setParameter / getParameter / setDitherEnabled   [Message Thread]
      - prepare                                          [Message/Background Thread,
                                                          nie aus dem Audio-Callback]
      - process                                          [Audio Thread] — lock- und
                                                          allocation-free (CLAUDE.md 3.1)

    Parameterübergabe: ein std::atomic<float> pro Parameter (relaxed).
    process() zieht am Blockanfang einen Snapshot — Parameter sind damit
    blockkonstant, exakt wie beim VST-Original (setParameter zwischen Blöcken).

    fpd-Dither: die Originale dithern pro Sample am Ende der Kette (32-Bit-
    Float-Pfad). ditherEnabled (Default OFF — Dithering passiert zentral in
    Conduit) schaltet nur den Dither-Add; der fpd-Xorshift läuft immer weiter,
    exakt wie im processDoubleReplacing-Pfad der Originale. Die fpd-Seeds sind
    deterministisch statt rand() (CLAUDE.md 3.1, reproduzierbare Tests);
    prepare() kann eigene Seeds injizieren.

    In-place-Verarbeitung ist erlaubt (outX == inX) — die Originale sind
    processReplacing und lesen jedes Sample vollständig vor dem Schreiben.
*/
class AirwindowsPlugin
{
public:
    static constexpr int maxParameters = 16;

    virtual ~AirwindowsPlugin() = default;

    //==========================================================================
    // Metadaten — konstant nach Konstruktion, von jedem Thread lesbar
    const char* getEffectId() const noexcept   { return effectId; }
    const char* getEffectName() const noexcept { return effectName; }
    int getNumParameters() const noexcept      { return (int) parameterInfos.size(); }
    const ParameterInfo& getParameterInfo (int index) const noexcept
    {
        return parameterInfos[(size_t) index];
    }

    //==========================================================================
    // [Message Thread]
    void setParameter (int index, float value01) noexcept;   // klemmt auf 0..1
    float getParameter (int index) const noexcept;            // Default bei ungültigem Index: 0

    void setDitherEnabled (bool enabled) noexcept  { ditherEnabled.store (enabled, std::memory_order_relaxed); }
    bool isDitherEnabled() const noexcept          { return ditherEnabled.load (std::memory_order_relaxed); }

    //==========================================================================
    /** [Message/Background Thread] Sample-Rate setzen, fpd-Seeds setzen und
        den DSP-Zustand zurücksetzen — vor dem Einbau in den Graph aufrufen.
        Seeds < 16386 werden angehoben (Original-Klausel gegen zu kleinen
        Xorshift-Startzustand).
    */
    void prepare (double newSampleRate,
                  std::uint32_t seedL = defaultSeedL,
                  std::uint32_t seedR = defaultSeedR) noexcept;

    //==========================================================================
    /** [Audio Thread] Stereo-Block verarbeiten. In-place erlaubt. */
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples) noexcept;

protected:
    AirwindowsPlugin (const char* effectIdIn, const char* effectNameIn,
                      std::span<const ParameterInfo> parameters) noexcept;

    /** DSP der Subklasse — 1:1 der processReplacing-Körper des Originals.
        Nur aus process() aufgerufen (Snapshot + ScopedNoDenormals aktiv). */
    virtual void processStereo (const float* in1, const float* in2,
                                float* out1, float* out2, int sampleFrames) noexcept = 0;

    /** Zustands-Reset der Subklasse (lastSample, IIR, ...) — von prepare() gerufen. */
    virtual void resetState() noexcept = 0;

    //==========================================================================
    // Sicht des Audio-Threads — nur in processStereo()/resetState() lesen
    float param (int index) const noexcept   { return paramSnapshot[(size_t) index]; }
    bool ditherOn() const noexcept           { return ditherSnapshot; }
    double getSampleRate() const noexcept    { return sampleRate; }

    // Original-Konvention: uint32-Xorshift für Denormal-Guard + fpd-Dither
    std::uint32_t fpdL = defaultSeedL;
    std::uint32_t fpdR = defaultSeedR;

private:
    static constexpr std::uint32_t defaultSeedL = 0x2FCA13B5u;
    static constexpr std::uint32_t defaultSeedR = 0x5A17C0DEu;

    const char* effectId;
    const char* effectName;
    std::span<const ParameterInfo> parameterInfos;

    std::array<std::atomic<float>, maxParameters> parameterValues {};
    std::atomic<bool> ditherEnabled { false };

    // Block-Snapshot (nur Audio Thread)
    std::array<float, maxParameters> paramSnapshot {};
    bool ditherSnapshot = false;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AirwindowsPlugin)
};

} // namespace conduit::airwindows
