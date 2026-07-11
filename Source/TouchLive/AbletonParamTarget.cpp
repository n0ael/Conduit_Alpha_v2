#include "AbletonParamTarget.h"

#include <cmath>

namespace conduit::grid
{

AbletonParamTarget::AbletonParamTarget (TouchLiveClient& clientToUse, LiveParamSpec specToUse)
    : client (clientToUse), paramSpec (std::move (specToUse))
{
}

void AbletonParamTarget::resolve (const juce::String& newDeviceId, int newParameterIndex,
                                  float newMinValue, float newMaxValue, bool newQuantised) noexcept
{
    deviceId       = newDeviceId;
    parameterIndex = newParameterIndex;
    minValue       = newMinValue;
    maxValue       = newMaxValue;
    quantised      = newQuantised;
}

float AbletonParamTarget::mapToNative (float value01, float rangeMin, float rangeMax,
                                       bool snapToSteps) noexcept
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, value01);
    auto native = rangeMin + (rangeMax - rangeMin) * clamped;

    // Quantisierte Parameter (parmeta.quant, Schrittweite 1.0): auf ganze
    // Schritte runden, damit Live keine Zwischenwerte sieht.
    if (snapToSteps)
        native = std::round (native);

    return native;
}

void AbletonParamTarget::sendValue (float value01)
{
    if (! isResolved())
        return;   // Block K: nach Live-Neustart bis zum Re-Resolve stumm

    // Exakt der Pfad von TouchLiveDeviceView::sendParameter: Suppression
    // fuer die heisse parvals-Zeile + Fast-Path-Versand (16-ms-Thinning).
    client.noteTouchedParameter (TouchLiveClient::makeParameterKey (
        "devices", "parvals:" + deviceId));

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/device/set/parameter") };
    message.addString (deviceId);
    message.addInt32 (parameterIndex);
    message.addFloat32 (mapToNative (value01, minValue, maxValue, quantised));
    client.sendTouchValue (message);
}

juce::String AbletonParamTarget::describe() const
{
    const auto name = paramSpec.displayName.isNotEmpty()
                          ? paramSpec.displayName
                          : ("Live-Parameter " + juce::String (parameterIndex));

    return isResolved() ? name : name + " (?)";
}

juce::ValueTree AbletonParamTarget::toState() const
{
    return specToState (paramSpec);
}

juce::ValueTree AbletonParamTarget::specToState (const LiveParamSpec& spec)
{
    juce::ValueTree state (kStateType);
    state.setProperty ("trackName", spec.trackName, nullptr);
    state.setProperty ("deviceName", spec.deviceName, nullptr);
    state.setProperty ("deviceOrdinal", spec.deviceOrdinal, nullptr);
    state.setProperty ("paramName", spec.paramName, nullptr);
    state.setProperty ("displayName", spec.displayName, nullptr);
    return state;
}

LiveParamSpec AbletonParamTarget::specFromState (const juce::ValueTree& state)
{
    LiveParamSpec spec;
    spec.trackName     = state.getProperty ("trackName").toString();
    spec.deviceName    = state.getProperty ("deviceName").toString();
    spec.deviceOrdinal = (int) state.getProperty ("deviceOrdinal", 0);
    spec.paramName     = state.getProperty ("paramName").toString();
    spec.displayName   = state.getProperty ("displayName").toString();
    return spec;
}

} // namespace conduit::grid
