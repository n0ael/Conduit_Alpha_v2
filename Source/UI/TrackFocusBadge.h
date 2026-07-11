#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSetModel.h"

namespace conduit
{

//==============================================================================
/**
    Kleines Badge „welchen Ableton-Track spielt das Grid gerade" (Block H
    v2): Farb-Swatch in der Live-Track-Farbe + Trackname. Sitzt oben links
    auf der Grid-Page (dort, wo bis Block H die Layout-Modus-Kacheln
    wohnten — der Modus wird jetzt über den Page-Button getoggelt und am
    Page-Icon abgelesen). Quelle ist der `conduit_focus`-Key der
    tracks-Domain — das Badge folgt also dem vom Remote Script verwalteten
    Fokus, NICHT Lives Track-Selektion (bewusst kein Push-Verhalten,
    User-Entscheidung 11.07.2026).

    Wiederverwendbar: Besitzer füttert setFocus() (z. B. aus einem
    ValueTree-Listener auf dem LiveSetModel) und steuert die Sichtbarkeit;
    ohne Fokus zeigt das Badge nichts. Message Thread.
*/
class TrackFocusBadge final : public juce::Component
{
public:
    struct FocusRow
    {
        juce::String key;      // Stable-ID, leer = kein Fokus
        juce::String name;
        juce::Colour colour;
    };

    TrackFocusBadge() = default;

    /** conduit_focus der tracks-Domain aufgelöst zu Name + Farbe —
        headless, Catch2-getestet; key leer, wenn kein Fokus/Item. */
    [[nodiscard]] static FocusRow focusRowFrom (LiveSetModel& model);

    /** Aktualisiert Anzeige (repaint nur bei Änderung). */
    void setFocus (const FocusRow& newFocus);

    [[nodiscard]] bool hasFocus() const noexcept { return focus.key.isNotEmpty(); }

    void paint (juce::Graphics& g) override;

private:
    FocusRow focus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackFocusBadge)
};

} // namespace conduit
