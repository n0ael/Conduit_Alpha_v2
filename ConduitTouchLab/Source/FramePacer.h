#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace touchlab
{

//==============================================================================
/**
    Schlanke Kopie von Conduits UiFramePacer (Source/UI/UiFramePacer.h): ein
    VBlankAttachment, der pro Monitor-VBlank genau einmal feuert. Für die
    Probe wollen wir maximale Auflösung, daher kein FPS-Gate — nur der Tick.

    ACHTUNG (Conduit-Feldtest-Lektion): ComponentPeer::onVBlank liefert den
    Timestamp in SEKUNDEN, nicht ms (der VBlankAttachment-Parametername
    "timestampMs" lügt). Wer hier dt-Physik ergänzt, muss * 1000.0 rechnen.
    Der argfreie Callback umgeht die Falle für simples Repaint komplett.
*/
class FramePacer
{
public:
    FramePacer (juce::Component* owner, std::function<void()> tickToUse)
        : tick (std::move (tickToUse)),
          vblank (owner, [this] { if (! stopped && tick != nullptr) tick(); })
    {
    }

    void stop() noexcept { stopped = true; }

private:
    std::function<void()> tick;
    bool stopped = false;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FramePacer)
};

} // namespace touchlab
