#pragma once

#include "Core/MacroBindings.h"
#include "LiveSetModel.h"

namespace conduit::grid
{

//==============================================================================
/** Ergebnis der Re-Auflösung einer LiveParamSpec gegen den aktuellen
    Live-Set-Spiegel (Block K): found = alle Merkmale eindeutig gefunden. */
struct ResolvedLiveParam
{
    bool found = false;
    juce::String deviceId;      // frisches dvid (Laufzeit)
    int   parameterIndex = 0;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool  quantised = false;
};

/** Löst eine LiveParamSpec über die tracks-/devices-Domains des
    LiveSetModel auf (Block K, Rule touchlive: dvid ist Laufzeit-ID —
    persistiert werden nur Namen + Ordinal):
      Track per NAME (erster Treffer) → chain:{tid} → deviceOrdinal-tes
      Device mit passendem NAMEN → parmeta per Parameter-NAME (Index ab 1,
      Index 0 = "Device On") → dvid + Index + min/max/quant.
    found = false, wenn irgendein Schritt keinen Treffer hat (Live nicht
    verbunden, Track/Device umbenannt oder gelöscht). Message Thread. */
[[nodiscard]] ResolvedLiveParam resolveLiveParam (LiveSetModel& model,
                                                  const LiveParamSpec& spec);

} // namespace conduit::grid
