#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "MacroBindings.h"
#include "ParamModulation.h"

namespace conduit::grid
{

//==============================================================================
/** Senke der Grid-Control-Modulation (MIDI-Rig M5c, implementiert von
    GridPage): ein normalisierter Offset [-1..+1] auf den Wert eines
    Control-WERTS (MacroControlKey) — der Basiswert des Controls bleibt
    unangetastet, nur Ausgabe/Anzeige verwenden den Effektivwert.
    Message Thread. */
class IGridControlModSink
{
public:
    virtual ~IGridControlModSink() = default;

    virtual void setControlModulation (const MacroControlKey& key, float offsetNorm) = 0;
    virtual void clearControlModulation (const MacroControlKey& key) = 0;
};

//==============================================================================
/** Macro-Ziel „Conduit-Parameter" (M5c): MODULIERT einen Modul-Parameter
    des Patches über den ParamModulationBus (GraphManager) — der User-
    Basiswert bleibt Souverän, wählbar unipolar (+, ab Basis aufwärts)
    oder bipolar (±, um die Basis herum), amount skaliert. Der Dtor räumt
    die Modulation ab (Basis kehrt zurück). Hält NIE einen Modul-Pointer
    (Zombie-Regel 5.3) — nur persistente nodeUuid/paramId; describe()
    löst transient über den Root-Tree auf. */
class ConduitParamTarget final : public MacroTarget
{
public:
    ConduitParamTarget (IParamModulationSink& sinkToUse, juce::ValueTree rootStateToUse,
                        juce::String nodeUuidToUse, juce::String paramIdToUse,
                        bool bipolarToUse, float amountToUse,
                        juce::String displayNameToUse);
    ~ConduitParamTarget() override;

    void sendValue (float value01) override;
    [[nodiscard]] juce::String describe() const override;
    [[nodiscard]] juce::ValueTree toState() const override;

    /** Panel-Setter: re-appliziert den letzten Macro-Wert sofort. */
    void setBipolar (bool shouldBeBipolar);
    void setAmount (float newAmount01);

    [[nodiscard]] bool isBipolar() const noexcept { return bipolar; }
    [[nodiscard]] float amount() const noexcept { return modAmount; }
    [[nodiscard]] const juce::String& nodeUuid() const noexcept { return uuid; }
    [[nodiscard]] const juce::String& paramId() const noexcept { return param; }

    static inline const juce::Identifier kStateType { "ConduitParamTarget" };

private:
    void applyOffset();

    IParamModulationSink& sink;
    juce::ValueTree rootState;   // ref-counted Handle (describe/Transienz), nie der Processor
    juce::String uuid;           // persistente Node-Uuid (CLAUDE.md §6)
    juce::String param;
    juce::String displayName;    // Cache fuer describe() bei geloeschtem Node
    bool  bipolar;
    float modAmount;             // 0..1

    float lastValue01 = -1.0f;   // letzter Macro-Wert (-1 = noch keiner)
    float lastOffset  = -2.0f;   // Dedupe (ausserhalb [-1,1] = nie gesendet)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConduitParamTarget)
};

//==============================================================================
/** Macro-Ziel „Grid-Control" (M5c): moduliert den Wert eines anderen
    Grid-Controls (Fader/Toggle/XY-Achse) — gleiche Polaritäts-/Amount-
    Semantik wie ConduitParamTarget; GridPage ist die Senke (Effektivwert
    fliesst nur in Ausgabe + Anzeige, Re-Entranz-Guard gegen Zyklen). */
class GridControlModTarget final : public MacroTarget
{
public:
    GridControlModTarget (IGridControlModSink& sinkToUse, const MacroControlKey& keyToUse,
                          bool bipolarToUse, float amountToUse,
                          juce::String displayNameToUse);
    ~GridControlModTarget() override;

    void sendValue (float value01) override;
    [[nodiscard]] juce::String describe() const override;
    [[nodiscard]] juce::ValueTree toState() const override;

    void setBipolar (bool shouldBeBipolar);
    void setAmount (float newAmount01);

    [[nodiscard]] bool isBipolar() const noexcept { return bipolar; }
    [[nodiscard]] float amount() const noexcept { return modAmount; }
    [[nodiscard]] const MacroControlKey& targetKey() const noexcept { return key; }

    static inline const juce::Identifier kStateType { "GridControlModTarget" };

private:
    void applyOffset();

    IGridControlModSink& sink;
    MacroControlKey key;
    juce::String displayName;
    bool  bipolar;
    float modAmount;

    float lastValue01 = -1.0f;
    float lastOffset  = -2.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridControlModTarget)
};

} // namespace conduit::grid
