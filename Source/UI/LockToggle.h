#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "PushLookAndFeel.h"

namespace conduit
{

//==============================================================================
/**
    Wiederverwendbares Schloss-Toggle im Push-Stil: zu (Default) = Zustand
    aus, offen = Zustand an. Ursprünglich als "Offset"-Schloss in
    MpeShapingView entstanden (S2c-2a-Nachbesserung), hier achsen-/domänen-
    agnostisch herausgezogen (S2c-2a-Politur) -- kein Wissen über
    GridVoiceEngine o. ä., keine eigene Beschriftung (der Besitzer platziert
    bei Bedarf ein eigenes Label daneben).

    Component ist eine feste 44×44-px-Touch-Zone (kComponentSize,
    CLAUDE.md 10); das Schloss-Symbol wird darin auf kSymbolSize skaliert
    und zentriert gezeichnet, die restliche Fläche ist transparent, aber
    klickbar. Der Rahmen wird separat von der Icon-Geometrie gezeichnet,
    seine Stärke skaliert mit der Größe, geklemmt auf ein Minimum von 2 px --
    dieses Muster gilt allgemein für künftige, größenskalierende
    Rahmen-Elemente.

    Akzentfarbe konfigurierbar (Default cyan): die aktive/gefüllte Fläche
    des Symbols nutzt diese Farbe, im inaktiven Zustand ist das Symbol
    gedämpft (textDim).
*/
class LockToggle final : public juce::Button
{
public:
    LockToggle();

    void setActive (bool shouldBeActive) noexcept;
    [[nodiscard]] bool isActive() const noexcept { return active; }

    /** Füllfarbe des Symbols im offenen (aktiven) Zustand -- Default cyan. */
    void setAccentColour (juce::Colour newColour) noexcept;

    static constexpr int   kComponentSize = 44;     // Touch-Zone (CLAUDE.md 10)
    static constexpr float kSymbolSize    = 26.0f;  // Schloss-Symbol, zentriert
    // Rahmenstärke = jmax(2px, Breite * kBorderFactor) -- bei 44px ≈ 2px.
    static constexpr float kBorderFactor  = 2.0f / (float) kComponentSize;

private:
    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;

    // SVG-Vorlage, viewBox 34×34 -- Korpus ist bei beiden Zuständen gleich
    // (siehe paintButton), diese liefern je nur den Bügel in Symbol-lokalen
    // (unskalierten) Koordinaten.
    [[nodiscard]] static juce::Path buildClosedLockPath() noexcept;
    [[nodiscard]] static juce::Path buildOpenLockPath() noexcept;

    bool active = false;
    juce::Colour accentColour = push::colours::ledCyan;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LockToggle)
};

} // namespace conduit
