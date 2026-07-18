#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace conduit
{

//==============================================================================
/**
    Performance-Modus-Grundlagen (ADR 008, §9-Scope: plattformspezifisches
    Setup NUR im Fenster-/Input-Setup): unterdrückt auf Windows die
    Touch-Systemgesten, die einer Live-Performance in die Quere kommen —

      - System.EdgeGesture.DisableTouchWhenFullscreen (Charms/Edge-Swipes
        im Fullscreen aus; SHGetPropertyStoreForWindow)
      - Press-and-Hold-Rechtsklick + Flicks aus
        (MICROSOFT_TABLETPENSERVICE_PROPERTY)
      - Touch-Feedback-Visuals aus (SetWindowFeedbackSetting, dynamisch
        geladen — kein SDK-Versions-Risiko)

    3/4-Finger-SYSTEMGESTEN sind nicht per App abschaltbar → dokumentierte
    Setup-Anforderung (Windows-Einstellungen), analog ASIO-Setup (ADR 008).
    Andere Plattformen: No-op (iOS deferred edges folgt im iOS-Zweig,
    LinkBox braucht nichts — Input gehört der App).

    Aufruf NACH dem Erzeugen des nativen Fensters (Peer muss existieren),
    Message Thread. Idempotent.
*/
void applyPerformanceTouchSetup (juce::Component& windowComponent);

} // namespace conduit
