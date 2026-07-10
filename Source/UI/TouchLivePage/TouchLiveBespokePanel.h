#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSpectrumTap.h"
#include "TouchLive/TouchLiveClient.h"

namespace conduit
{

//==============================================================================
/**
    Basis der bespoke Device-UIs (M5, docs/TouchLive.md §6b).

    Die DeviceView hält pro gewähltem Device höchstens ein bespoke Panel
    (Registry `class_name → Panel` via createBespokePanel); liefert die
    Registry nichts ODER meldet das Panel isUsable()==false (Parameter-
    Zuordnung fehlgeschlagen, z. B. andere Live-Version), bleibt die
    generische 8er-Bank sichtbar — jedes Device ist IMMER steuerbar.

    Datenfluss wie die Bank: setDevice() bekommt Stable-ID + parmeta
    (Struktur, selten), setValues() die heiße parvals-Zeile (jeder Diff).
    Schreibweg der Panels: /live/device/set/parameter über sendTouchValue
    (kontinuierlich) bzw. sendCommand (diskret) + noteTouchedParameter
    (Suppression-Key devices/parvals:{dvid} — identisch zur Bank).
*/
class TouchLiveBespokePanel : public juce::Component
{
public:
    ~TouchLiveBespokePanel() override = default;

    /** Struktur-Update: Stable-ID (ohne dv:-Präfix) + parmeta-Array
        ([{name,min,max,quant,items?},…]). Mappt Parameter-Indizes neu. */
    virtual void setDevice (const juce::String& deviceKey, const juce::var& parmeta) = 0;

    /** Heiße Werte-Zeile (parvals-Array, Index = Parameter-Index). */
    virtual void setValues (const juce::var& parvals) = 0;

    /** false = Zuordnung fehlgeschlagen → DeviceView zeigt die Bank. */
    [[nodiscard]] virtual bool isUsable() const = 0;
};

/** Registry (§6b): class_name → Panel; nullptr = kein bespoke UI.
    spectrumTap darf nullptr sein (Tests) — dann ohne Spektrum. */
[[nodiscard]] std::unique_ptr<TouchLiveBespokePanel>
createBespokePanel (const juce::String& className, TouchLiveClient& client,
                    LiveSpectrumTap* spectrumTap = nullptr);

} // namespace conduit
