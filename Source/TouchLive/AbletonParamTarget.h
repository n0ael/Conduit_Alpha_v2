#pragma once

#include <juce_osc/juce_osc.h>

#include "Core/MacroBindings.h"
#include "TouchLiveClient.h"

namespace conduit::grid
{

//==============================================================================
/** MacroTarget auf einen Ableton-Live-Device-Parameter (Block E, Masterplan:
    "Ableton-Parameter direkt ueber das vorhandene TouchLive-Remote-Script --
    Parameter-Browser statt MIDI-Learn").

    Adressierung wie TouchLiveDeviceView::sendParameter: Device-Stable-ID
    (dvid) + Parameter-Index; der 0..1-Macro-Wert wird auf den nativen
    [min,max]-Bereich aus parmeta skaliert (quantisierte Parameter auf ganze
    Schritte gerundet). Versand ueber den 16-ms-Fast-Path (sendTouchValue)
    mit Echo-Suppression (noteTouchedParameter).

    Block K (Persistenz + Re-Resolve): dvid ist eine LAUFZEIT-Stable-ID
    (Rule touchlive) — das Ziel traegt deshalb eine LiveParamSpec mit den
    STABILEN Merkmalen (Track-/Device-/Parameter-NAME + Device-Ordinal).
    Persistiert wird NUR die Spec (toState); die Laufzeit-Adresse kommt
    ueber resolve() (beim Erzeugen, nach dem Laden und nach jedem
    Live-Reconnect — LiveTargetResolver). Unaufgeloest ist sendValue ein
    No-op. */
class AbletonParamTarget final : public MacroTarget
{
public:
    AbletonParamTarget (TouchLiveClient& clientToUse, LiveParamSpec specToUse);

    /** Laufzeit-Adresse setzen/auffrischen (dvid + Index + Range aus dem
        aktuellen parmeta). */
    void resolve (const juce::String& newDeviceId, int newParameterIndex,
                  float newMinValue, float newMaxValue, bool newQuantised) noexcept;

    /** Laufzeit-Adresse verwerfen (Live-Neustart: dvid ist tot). */
    void unresolve() noexcept { deviceId.clear(); }
    [[nodiscard]] bool isResolved() const noexcept { return deviceId.isNotEmpty(); }

    [[nodiscard]] const LiveParamSpec& spec() const noexcept { return paramSpec; }

    void sendValue (float value01) override;
    [[nodiscard]] juce::String describe() const override;
    [[nodiscard]] juce::ValueTree toState() const override;   // nur die Spec (Block K)

    /** Reines Wert-Mapping 0..1 → nativer Live-Bereich (headless testbar). */
    [[nodiscard]] static float mapToNative (float value01, float rangeMin, float rangeMax,
                                            bool snapToSteps) noexcept;

    /** Spec ↔ ValueTree (Block K): specFromState liest, was toState
        schreibt — headless testbar, vom GridSessionStore/der GridPage-
        Factory genutzt. */
    [[nodiscard]] static juce::ValueTree specToState (const LiveParamSpec& spec);
    [[nodiscard]] static LiveParamSpec specFromState (const juce::ValueTree& state);

    static inline const juce::Identifier kStateType { "LiveTarget" };

private:
    TouchLiveClient& client;
    LiveParamSpec paramSpec;

    juce::String deviceId;      // dvid, Laufzeit-Stable-ID (nie serialisieren)
    int   parameterIndex = 0;
    float minValue = 0.0f, maxValue = 1.0f;
    bool  quantised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbletonParamTarget)
};

} // namespace conduit::grid
