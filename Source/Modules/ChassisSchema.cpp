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
namespace
{
    /** Kubische Bezier-Komponente mit Endpunkten 0 und 1: B(t) für eine
        Achse mit Kontrollwerten c1, c2. */
    [[nodiscard]] float bezierComponent (float t, float c1, float c2) noexcept
    {
        const float u = 1.0f - t;
        return 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t;
    }

    /** Löst component(t) == target per Bisektion — beide Achsen sind bei
        Kontrollwerten in [0,1] monoton (ChassisSchema-Doku). */
    [[nodiscard]] float solveBezier (float target, float c1, float c2) noexcept
    {
        float lo = 0.0f, hi = 1.0f;

        for (int i = 0; i < 32; ++i)
        {
            const float mid = 0.5f * (lo + hi);

            if (bezierComponent (mid, c1, c2) < target)
                lo = mid;
            else
                hi = mid;
        }

        return 0.5f * (lo + hi);
    }
} // namespace

std::optional<ChassisSchema::BezierCurve> ChassisSchema::parseCurve (const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text, " ", {});
    tokens.removeEmptyStrings();

    if (tokens.size() != 4)
        return std::nullopt;

    const auto clamped = [&tokens] (int index)
    {
        return juce::jlimit (0.0f, 1.0f, tokens[index].getFloatValue());
    };

    return BezierCurve { clamped (0), clamped (1), clamped (2), clamped (3) };
}

juce::String ChassisSchema::curveToString (const BezierCurve& curve)
{
    return juce::String (curve.x1, 3) + " " + juce::String (curve.y1, 3) + " "
         + juce::String (curve.x2, 3) + " " + juce::String (curve.y2, 3);
}

std::optional<ChassisSchema::LinkResponse> ChassisSchema::parseLinkResponse (const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text, " ", {});
    tokens.removeEmptyStrings();

    if (tokens.size() != 4 && tokens.size() != 6)
        return std::nullopt;

    const auto clamped = [&tokens] (int index)
    {
        return juce::jlimit (0.0f, 1.0f, tokens[index].getFloatValue());
    };

    LinkResponse response;
    response.curve = { clamped (0), clamped (1), clamped (2), clamped (3) };

    if (tokens.size() == 6)
    {
        response.startY = clamped (4);
        response.endY   = clamped (5);
    }

    return response;
}

juce::String ChassisSchema::linkResponseToString (const LinkResponse& response)
{
    return curveToString (response.curve) + " "
         + juce::String (response.startY, 3) + " " + juce::String (response.endY, 3);
}

float ChassisSchema::evaluateCurve (const BezierCurve& curve, float position) noexcept
{
    const auto p = juce::jlimit (0.0f, 1.0f, position);
    const auto t = solveBezier (p, curve.x1, curve.x2);
    return juce::jlimit (0.0f, 1.0f, bezierComponent (t, curve.y1, curve.y2));
}

float ChassisSchema::curvePositionForValue (const BezierCurve& curve, float normValue) noexcept
{
    const auto y = juce::jlimit (0.0f, 1.0f, normValue);
    const auto t = solveBezier (y, curve.y1, curve.y2);
    return juce::jlimit (0.0f, 1.0f, bezierComponent (t, curve.x1, curve.x2));
}

//==============================================================================
std::optional<std::vector<ChassisSchema::ButtonPreset>> ChassisSchema::parseButtons (const juce::String& text)
{
    if (text.isEmpty())
        return std::nullopt;

    const auto parsed = juce::JSON::parse (text);
    const auto* entries = parsed.getArray();

    if (entries == nullptr || entries->size() > maxUiButtons)
        return std::nullopt;

    std::vector<ButtonPreset> buttons;
    buttons.reserve (static_cast<size_t> (entries->size()));

    for (const auto& entry : *entries)
    {
        const auto nameVar  = entry.getProperty ("n", juce::var());
        const auto valueVar = entry.getProperty ("v", juce::var());

        if (! nameVar.isString() || ! (valueVar.isDouble() || valueVar.isInt()))
            return std::nullopt;

        auto name = nameVar.toString().trim();

        if (name.length() > maxUiButtonNameLength)
            name = name.substring (0, maxUiButtonNameLength);

        buttons.push_back ({ name, static_cast<double> (valueVar) });
    }

    return buttons;
}

juce::String ChassisSchema::buttonsToString (const std::vector<ButtonPreset>& buttons)
{
    juce::Array<juce::var> entries;
    entries.ensureStorageAllocated (static_cast<int> (buttons.size()));

    for (const auto& button : buttons)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("n", button.name);
        entry->setProperty ("v", button.value);
        entries.add (juce::var (entry));
    }

    return juce::JSON::toString (juce::var (entries), true /*allOnOneLine*/);
}

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
