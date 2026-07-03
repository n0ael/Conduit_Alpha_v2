#pragma once

#include "ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Schema- und Rechen-Regeln des FX-Chassis (CLAUDE.md 4.6) — pure, statisch,
    ohne Modul-Instanz testbar.

    Jeder Processor-Node trägt neben seinen DSP-Parametern die Chassis-
    Parameter (input_gain/output_gain, dB) und pro DSP-Parameter einen
    Attenuverter ({param}_cv_amt, bipolar). Das Parameter-Property `role`
    unterscheidet die Schichten fürs UI-Layout; OSC-Adressen bleiben
    unverändert kanonisch.

    Kanal-Layout (FEST, nie von UI-Zustand abhängig): Audio 0..1,
    CV-Eingang des DSP-Parameters i = numAudioIns + i. Unverbundene
    CV-Kanäle liefert der Graph genullt → keine Modulation, kein Sonderfall.
*/
struct ChassisSchema
{
    //==========================================================================
    // Rollen (Parameter-Property id::paramRole)
    static constexpr const char* roleDsp      = "dsp";
    static constexpr const char* roleChassis  = "chassis";
    static constexpr const char* roleCvAmount = "cvAmount";

    // Chassis-Parameter-Ids
    static constexpr const char* inputGainId    = "input_gain";
    static constexpr const char* outputGainId   = "output_gain";
    static constexpr const char* cvAmountSuffix = "_cv_amt";

    // Gain-Range in dB; gainFloorDb wirkt als -inf (Gain exakt 0.0)
    static constexpr double gainMinDb     = -60.0;
    static constexpr double gainMaxDb     = 6.0;
    static constexpr double gainDefaultDb = 0.0;
    static constexpr float  gainFloorDb   = -60.0f;

    // Attenuverter (bipolar, Mutable-Stil)
    static constexpr double cvAmountMin     = -1.0;
    static constexpr double cvAmountMax     = 1.0;
    static constexpr double cvAmountDefault = 0.0;

    /** Chassis-Schema-Version (Node-Property stateVersion, Migration 1→2). */
    static constexpr int stateVersion = 2;

    //==========================================================================
    /** Attenuverter-Parameter-Id zu einem DSP-Parameter: "{param}_cv_amt". */
    [[nodiscard]] static juce::String cvAmountIdFor (const juce::String& dspParamId)
    {
        return dspParamId + cvAmountSuffix;
    }

    /** Rolle eines Parameters aus dem Tree; fehlendes Property = dsp
        (Alt-Bestand vor der Migration). */
    [[nodiscard]] static juce::String roleOf (const juce::ValueTree& parameterTree)
    {
        return parameterTree.getProperty (id::paramRole, roleDsp).toString();
    }

    //==========================================================================
    /** CV-Modulation (Audio Thread, pure): blockkonstanter Effektivwert.
        cv folgt der ±1-Konvention (CLAUDE.md 8.0), amount ist der
        Attenuverter (−1..+1). Hard-Clamp auf die DSP-Range — der Fader-
        User-Bereich (userMin/userMax) beschneidet die Modulation NICHT. */
    [[nodiscard]] static float computeEffective (float base, float cv, float amount,
                                                 float hardMin, float hardMax) noexcept
    {
        return juce::jlimit (hardMin, hardMax, base + cv * amount * (hardMax - hardMin));
    }

    //==========================================================================
    /** Migration eines Processor-Nodes auf das Chassis-Schema (idempotent,
        Message Thread, vom GraphManager::normalizeNode aufgerufen):

        1. Parameter ohne role-Property taggen (Heuristik über die Id).
        2. Fehlende input_gain/output_gain vorn einfügen (Reihenfolge:
           input_gain, output_gain, dann DSP-Parameter).
        3. Fehlende {param}_cv_amt direkt hinter ihrem DSP-Parameter ergänzen.
        4. numInputChannels = numAudioIns + Anzahl DSP-Parameter nachziehen
           (Audio-Kanäle 0..1 bleiben stabil — bestehende Kabel unberührt).
        5. linkSendEnabled-Default (false) setzen.
        6. stateVersion auf mindestens ChassisSchema::stateVersion stempeln.

        Annahme: alle Chassis-Module sind stereo (numAudioIns = 2) — gilt für
        den gesamten Alt-Bestand (Airwindows). */
    static void migrate (juce::ValueTree nodeTree, int numAudioIns = 2);
};

} // namespace conduit
