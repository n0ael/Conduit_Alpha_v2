#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

namespace conduit::touchlive
{

//==============================================================================
/**
    Näherung des Ableton-Live-Volume-Mappings (Wert 0..1 ↔ dB) für die
    Remote-Fader (docs/TouchLive.md §5): Lives Mixer-Volume-Parameter ist
    NICHT dB-linear — die Gegenseite liefert den rohen Parameterwert
    (mixer-Domain, Feld "vol"), Anzeige/Skala brauchen dB.

    Community-Fit (verbreitet in Remote-Script-Umfeld, gegen die Anker
    0.85 → 0 dB und 1.0 → +6 dB kalibriert):

        value ≥ 0.4:  dB = 40·value − 34          (linear oben)
        value < 0.4:  dB = −18 + 25·log10(v/0.4)  (log-Auslauf → −inf)

    Exaktheit unter −18 dB ist eine Näherung — nach dem Live-Feldtest ggf.
    nachkalibrieren (Dossier §11 Offen). Reine Mathematik, Message Thread.
*/
namespace faderscale
{
    /** 0-dB-Anker: Lives Unity-Gain liegt bei Parameterwert 0.85. */
    constexpr double unityValue = 0.85;

    /** Anzeige-Boden: darunter rendern wir „−inf". */
    constexpr double silenceDb = -72.0;

    [[nodiscard]] inline double dbFromValue (double value)
    {
        value = juce::jlimit (0.0, 1.0, value);

        if (value >= 0.4)
            return 40.0 * value - 34.0;

        if (value <= 0.0001)
            return silenceDb;

        return juce::jmax (silenceDb, -18.0 + 25.0 * std::log10 (value / 0.4));
    }

    [[nodiscard]] inline double valueFromDb (double db)
    {
        if (db <= silenceDb)
            return 0.0;

        if (db >= -18.0)
            return juce::jlimit (0.0, 1.0, (db + 34.0) / 40.0);

        return juce::jlimit (0.0, 1.0, 0.4 * std::pow (10.0, (db + 18.0) / 25.0));
    }

    /** Anzeige-Text: „0.0" / „-12.3" / „-inf" (ohne dB-Suffix, Platz!). */
    [[nodiscard]] inline juce::String dbText (double value)
    {
        const auto db = dbFromValue (value);

        if (db <= silenceDb + 0.01)
            return "-inf";

        return juce::String (db, 1);
    }
} // namespace faderscale

} // namespace conduit::touchlive
