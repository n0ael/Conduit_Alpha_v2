#include "ChassisSchema.h"

namespace conduit
{

namespace
{
    // Parameter-Skelett nach Schema 6.2 + role — bewusst lokal statt
    // ConduitModule::makeParameter (protected), damit die Migration pure bleibt.
    [[nodiscard]] juce::ValueTree makeParameterTree (const juce::String& parameterId,
                                                     double value, double minValue,
                                                     double maxValue, double defaultValue,
                                                     const char* role)
    {
        juce::ValueTree parameter (id::parameter);
        parameter.setProperty (id::paramId,      parameterId,  nullptr);
        parameter.setProperty (id::paramValue,   value,        nullptr);
        parameter.setProperty (id::paramMin,     minValue,     nullptr);
        parameter.setProperty (id::paramMax,     maxValue,     nullptr);
        parameter.setProperty (id::paramDefault, defaultValue, nullptr);
        parameter.setProperty (id::paramRole,    role,         nullptr);
        return parameter;
    }

    [[nodiscard]] const char* roleFromParameterId (const juce::String& parameterId)
    {
        if (parameterId == ChassisSchema::inputGainId
            || parameterId == ChassisSchema::outputGainId)
            return ChassisSchema::roleChassis;

        if (parameterId.endsWith (ChassisSchema::cvAmountSuffix))
            return ChassisSchema::roleCvAmount;

        return ChassisSchema::roleDsp;
    }
} // namespace

//==============================================================================
void ChassisSchema::migrate (juce::ValueTree nodeTree, int numAudioIns)
{
    if (! nodeTree.hasType (id::node))
        return;

    auto params = nodeTree.getChildWithName (id::parameters);

    if (! params.isValid())
    {
        params = juce::ValueTree (id::parameters);
        nodeTree.appendChild (params, nullptr);
    }

    // 1) Rollen des Alt-Bestands taggen (Heuristik über die Id — vor der
    //    Migration existieren nur DSP-Parameter, die Prüfung deckt trotzdem
    //    teilmigrierte Bestände idempotent ab).
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        auto parameter = params.getChild (i);

        if (! parameter.hasProperty (id::paramRole))
            parameter.setProperty (id::paramRole,
                roleFromParameterId (parameter.getProperty (id::paramId).toString()), nullptr);
    }

    // 2) Chassis-Gains vorn ergänzen (input_gain an 0, output_gain an 1)
    if (! params.getChildWithProperty (id::paramId, inputGainId).isValid())
        params.addChild (makeParameterTree (inputGainId, gainDefaultDb, gainMinDb,
                                            gainMaxDb, gainDefaultDb, roleChassis),
                         0, nullptr);

    if (! params.getChildWithProperty (id::paramId, outputGainId).isValid())
        params.addChild (makeParameterTree (outputGainId, gainDefaultDb, gainMinDb,
                                            gainMaxDb, gainDefaultDb, roleChassis),
                         1, nullptr);

    // 3) Attenuverter je DSP-Parameter direkt hinter seinem Parameter —
    //    und dabei die DSP-Parameter zählen (Kanal-Layout, Schritt 4).
    int numDspParams = 0;

    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto parameter = params.getChild (i);

        if (roleOf (parameter) != juce::String (roleDsp))
            continue;

        ++numDspParams;
        const auto cvId = cvAmountIdFor (parameter.getProperty (id::paramId).toString());

        if (! params.getChildWithProperty (id::paramId, cvId).isValid())
            params.addChild (makeParameterTree (cvId, cvAmountDefault, cvAmountMin,
                                                cvAmountMax, cvAmountDefault, roleCvAmount),
                             i + 1, nullptr);
    }

    // 4) Kanal-Layout: Audio + ein CV-Eingang pro DSP-Parameter. uiHidden
    //    ändert dieses Layout NIE (CLAUDE.md 4.6).
    nodeTree.setProperty (id::numInputChannels, numAudioIns + numDspParams, nullptr);

    // 5) Link-Send-Default
    if (! nodeTree.hasProperty (id::linkSendEnabled))
        nodeTree.setProperty (id::linkSendEnabled, false, nullptr);

    // 6) Version stempeln
    if (static_cast<int> (nodeTree.getProperty (id::stateVersion, 1)) < stateVersion)
        nodeTree.setProperty (id::stateVersion, stateVersion, nullptr);
}

} // namespace conduit
