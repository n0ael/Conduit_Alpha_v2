#include "ConduitMacroTargets.h"

#include "Modules/ConduitModule.h"

namespace conduit::grid
{

namespace
{
    juce::String polarityGlyph (bool bipolar)
    {
        return bipolar ? juce::String::fromUTF8 ("\xc2\xb1") : juce::String ("+");
    }
}

//==============================================================================
ConduitParamTarget::ConduitParamTarget (IParamModulationSink& sinkToUse,
                                        juce::ValueTree rootStateToUse,
                                        juce::String nodeUuidToUse, juce::String paramIdToUse,
                                        bool bipolarToUse, float amountToUse,
                                        juce::String displayNameToUse)
    : sink (sinkToUse), rootState (std::move (rootStateToUse)),
      uuid (std::move (nodeUuidToUse)), param (std::move (paramIdToUse)),
      displayName (std::move (displayNameToUse)),
      bipolar (bipolarToUse), modAmount (juce::jlimit (0.0f, 1.0f, amountToUse))
{
}

ConduitParamTarget::~ConduitParamTarget()
{
    // Slot geloescht/Ziel gewechselt: Modulation abraeumen, Basis kehrt zurueck.
    sink.clearParamModulation ({ uuid, param });
}

void ConduitParamTarget::sendValue (float value01)
{
    lastValue01 = juce::jlimit (0.0f, 1.0f, value01);
    applyOffset();
}

void ConduitParamTarget::applyOffset()
{
    if (lastValue01 < 0.0f)
        return;   // noch kein Macro-Wert -- Setter vor dem ersten sendValue

    const auto offset = computeOffsetNorm (lastValue01, bipolar, modAmount);
    if (juce::exactlyEqual (offset, lastOffset))
        return;   // Dedupe auf dem Offset

    lastOffset = offset;
    sink.setParamModulation ({ uuid, param }, offset);
}

void ConduitParamTarget::setBipolar (bool shouldBeBipolar)
{
    if (bipolar == shouldBeBipolar)
        return;

    bipolar = shouldBeBipolar;
    applyOffset();
}

void ConduitParamTarget::setAmount (float newAmount01)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newAmount01);
    if (juce::exactlyEqual (modAmount, clamped))
        return;

    modAmount = clamped;
    applyOffset();
}

juce::String ConduitParamTarget::describe() const
{
    // Transiente Aufloesung ueber den Tree (nie ein Modul-Pointer, 5.3):
    // Node weg -> Cache mit "fehlt:"-Praefix (Muster AbletonParamTarget
    // unresolved).
    const auto nodeTree = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::nodeId, uuid);
    if (! nodeTree.isValid())
        return "fehlt: " + displayName;

    return displayName + " " + polarityGlyph (bipolar);
}

juce::ValueTree ConduitParamTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("nodeUuid", uuid, nullptr);
    state.setProperty ("paramId", param, nullptr);
    state.setProperty ("bipolar", bipolar, nullptr);
    state.setProperty ("amount", modAmount, nullptr);
    state.setProperty ("name", displayName, nullptr);
    return state;
}

//==============================================================================
GridControlModTarget::GridControlModTarget (IGridControlModSink& sinkToUse,
                                            const MacroControlKey& keyToUse,
                                            bool bipolarToUse, float amountToUse,
                                            juce::String displayNameToUse)
    : sink (sinkToUse), key (keyToUse), displayName (std::move (displayNameToUse)),
      bipolar (bipolarToUse), modAmount (juce::jlimit (0.0f, 1.0f, amountToUse))
{
}

GridControlModTarget::~GridControlModTarget()
{
    sink.clearControlModulation (key);
}

void GridControlModTarget::sendValue (float value01)
{
    lastValue01 = juce::jlimit (0.0f, 1.0f, value01);
    applyOffset();
}

void GridControlModTarget::applyOffset()
{
    if (lastValue01 < 0.0f)
        return;

    const auto offset = computeOffsetNorm (lastValue01, bipolar, modAmount);
    if (juce::exactlyEqual (offset, lastOffset))
        return;

    lastOffset = offset;
    sink.setControlModulation (key, offset);
}

void GridControlModTarget::setBipolar (bool shouldBeBipolar)
{
    if (bipolar == shouldBeBipolar)
        return;

    bipolar = shouldBeBipolar;
    applyOffset();
}

void GridControlModTarget::setAmount (float newAmount01)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newAmount01);
    if (juce::exactlyEqual (modAmount, clamped))
        return;

    modAmount = clamped;
    applyOffset();
}

juce::String GridControlModTarget::describe() const
{
    return displayName + " " + polarityGlyph (bipolar);
}

juce::ValueTree GridControlModTarget::toState() const
{
    juce::ValueTree state (kStateType);
    state.setProperty ("layer", key.layer, nullptr);
    state.setProperty ("controlId", key.controlId, nullptr);
    state.setProperty ("axis", key.axis, nullptr);
    state.setProperty ("bipolar", bipolar, nullptr);
    state.setProperty ("amount", modAmount, nullptr);
    state.setProperty ("name", displayName, nullptr);
    return state;
}

} // namespace conduit::grid
