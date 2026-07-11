#include "LiveTargetResolver.h"

namespace conduit::grid
{

ResolvedLiveParam resolveLiveParam (LiveSetModel& model, const LiveParamSpec& spec)
{
    ResolvedLiveParam result;

    if (spec.trackName.isEmpty() || spec.deviceName.isEmpty() || spec.paramName.isEmpty())
        return result;

    // 1. Track per NAME (erster Treffer — Live erlaubt Duplikate, dann
    //    gewinnt die Set-Reihenfolge; das entspricht der Combo-Auswahl).
    juce::String trackKey;
    auto tracks = model.getDomain ("tracks");

    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        const auto item = tracks.getChild (i);
        if (item.getProperty ("name").toString() == spec.trackName)
        {
            trackKey = item.getProperty (touchlive::id::itemKey).toString();
            break;
        }
    }

    if (trackKey.isEmpty())
        return result;

    // 2. deviceOrdinal-tes Device dieses NAMENS in der Chain.
    auto devices = model.getDomain ("devices");
    const auto chain = devices.getProperty ("chain:" + trackKey);
    const auto* chainArray = chain.getArray();
    if (chainArray == nullptr)
        return result;

    juce::String resolvedDvid;
    auto sameNameSeen = 0;

    for (const auto& entry : *chainArray)
    {
        const auto dvid = entry.toString();
        if (dvid.isEmpty())
            continue;

        const auto deviceItem = model.findItem ("devices", "dev:" + dvid);
        if (! deviceItem.isValid()
            || deviceItem.getProperty ("name").toString() != spec.deviceName)
            continue;

        if (sameNameSeen == spec.deviceOrdinal)
        {
            resolvedDvid = dvid;
            break;
        }

        ++sameNameSeen;
    }

    if (resolvedDvid.isEmpty())
        return result;

    // 3. Parameter per NAME (Index ab 1 — Index 0 = "Device On").
    const auto meta = devices.getProperty ("parmeta:" + resolvedDvid);
    const auto* metaArray = meta.getArray();
    if (metaArray == nullptr)
        return result;

    for (int i = 1; i < metaArray->size(); ++i)
    {
        const auto& entry = metaArray->getReference (i);
        if (entry.getProperty ("name", {}).toString() != spec.paramName)
            continue;

        result.found          = true;
        result.deviceId       = resolvedDvid;
        result.parameterIndex = i;
        result.minValue       = (float) (double) entry.getProperty ("min", 0.0);
        result.maxValue       = (float) (double) entry.getProperty ("max", 1.0);
        result.quantised      = (bool) entry.getProperty ("quant", false);
        break;
    }

    return result;
}

} // namespace conduit::grid
