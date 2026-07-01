#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
/** Ein Eingang eines Multi-Input-Send-Moduls, wie ihn der GraphManager aus dem
    ValueTree-Schema (<Inputs>) ableitet und vor der Materialisierung injiziert.
    Der effektive Kanal-Name trägt NOCH KEIN Node-Präfix — das Modul stellt
    seine moduleId selbst voran (7.2, Kanal-Name = {moduleId}/{effectiveName}). */
struct SendInputConfig
{
    juce::String inputId;        // stabile Eingangs-UUID (serialisiert)
    int          width = 1;      // 1 = mono, 2 = stereo (Kanalbreite am Bus)
    juce::String effectiveName;  // userName ?: autoName ?: "input{n}"
    juce::String gainParamId;    // "in{n}_gain" — Ziel für getParameterTarget
};

//==============================================================================
/**
    Mixin-Interface (CLAUDE.md 4.2-Stil): Module, deren Ein-/Ausgangs-Kanalzahl
    und Kanal-Struktur aus dem ValueTree stammt (Multi-Input Link Audio Send).
    Der GraphManager injiziert die Konfiguration bei der Materialisierung VOR
    prepareForGraph (5.2 Schritt 1) — der Bus-Layout muss stehen, bevor
    setPlayConfigDetails/prepareToPlay läuft, und die Sinks entstehen in
    prepareToPlay aus dieser Struktur.

    Alle Methoden laufen auf dem Message Thread.
*/
class ISendConfigClient
{
public:
    virtual ~ISendConfigClient() = default;

    /** Setzt das Bus-Kanal-Layout (Summe der Breiten) und die Eingangs-
        Struktur. VOR prepareForGraph, Message Thread. */
    virtual void applySendConfig (const std::vector<SendInputConfig>& inputs) = 0;

    /** Der effektive Name eines Eingangs hat sich geändert (userName/autoName,
        z.B. Auto-Naming-Snapshot oder Refresh) — der Kanal-Name folgt live zu
        den Peers (sink.setName, {moduleId}/{name}). Message Thread. */
    virtual void inputNameChanged (const juce::String& inputId,
                                   const juce::String& effectiveName) = 0;
};

} // namespace conduit
