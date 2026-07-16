#pragma once

#include <map>

#include <juce_core/juce_core.h>

namespace conduit::midirig
{

//==============================================================================
/** Channelstrip-Ebenen (M7): pro Spalte (K1-Channelstrip) waehlt EIN
    Top-Encoder durch Drehen eine von 3 Binding-Baenken fuer alle uebrigen
    Controls der Spalte. Kein Sprung: ein Schritt-Akkumulator mit
    `kStepsPerLayer` Detents je Ebenengrenze (User 15.07.2026 -- "8 steps ist
    ein guter Wert"), an den Enden geklemmt (kein Wrap zwischen Ebene 3 und 1).

    Headless, deterministisch, Catch2-testbar. Message Thread. Die Spalte wird
    ueber ihren Profil-`group`-String identifiziert (z. B. "col1") -- dieselbe
    Identitaet nutzen MidiInBindings (aktive Ebene je Spalte), der
    PickupLedRouter (Ebenen-Blink) und die Persistenz. */
class ChannelStripLayers
{
public:
    static constexpr int kNumLayers     = 3;
    static constexpr int kStepsPerLayer  = 8;   // Detents je Ebenen-Zone
    static constexpr int kMaxPos         = kStepsPerLayer * kNumLayers - 1;   // 3 volle Zonen

    struct Result
    {
        int  layer        = 0;      // 0..kNumLayers-1
        bool layerChanged = false;
    };

    /** Relatives Encoder-Event (K1-Endlos-Encoder, signed-Kodierung, s. u.)
        akkumulieren und die -- ggf. neue -- Ebene der Spalte melden. */
    Result feed (const juce::String& column, int ccValue7bit);

    [[nodiscard]] int layerFor (const juce::String& column) const;

    /** Ebene direkt setzen (Persistenz-Load) -- setzt den Akkumulator an den
        Zonen-Anfang, damit anschliessendes Drehen konsistent bleibt. */
    void setLayer (const juce::String& column, int layer);

    void reset() { strips.clear(); }

    /** Spalte -> aktive Ebene (Persistenz-Save). Nur belegte Spalten. */
    [[nodiscard]] std::map<juce::String, int> snapshot() const;

    /** K1-Endlos-Encoder senden relativ (signed): 1..63 = +n (CW),
        65..127 = -(128-n) (CCW), 0/64 = 0. Isoliert + testbar, weil die
        Kodierung geraeteabhaengig ist (Feldtest kann sie hier justieren). */
    [[nodiscard]] static int decodeSignedDelta (int ccValue7bit) noexcept;

private:
    struct Strip
    {
        int pos   = 0;   // 0..kMaxPos
        int layer = 0;
    };

    [[nodiscard]] static int layerForPos (int pos) noexcept
    {
        return juce::jlimit (0, kNumLayers - 1, pos / kStepsPerLayer);
    }

    std::map<juce::String, Strip> strips;

    JUCE_LEAK_DETECTOR (ChannelStripLayers)
};

} // namespace conduit::midirig
