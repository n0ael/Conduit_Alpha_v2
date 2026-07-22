#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchSample.h"

namespace touchlab
{

//==============================================================================
/**
    Blindvergleich: ordnet Kanal A/B zufällig (juce::Random) Nativ/Raw-Pointer
    zu, OHNE Label. Ein großer Indikator-Punkt wird vom aktuell gewählten
    Kanal (A oder B) getrieben — Leon fühlt, urteilt „A oder B feiner", das
    Harness protokolliert. Nach jeder Runde neue Zufalls-Zuordnung; „Auflösen"
    zeigt die Auszählung Nativ vs. Raw. Belegt real vs. eingebildet.
*/
class BlindAbMode final : public juce::Component
{
public:
    BlindAbMode();

    void setActive (bool shouldBeActive);
    bool isActive() const noexcept { return active; }

    /** Hub speist die GEFILTERTEN Ströme beider Quellen ein. */
    void consume (SourceTag tag, juce::Point<float> filtered, Phase phase);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void newRound();
    void castVerdict (char finerChannel);

    SourceTag channelSource (char channel) const;

    bool active = false;
    juce::Random rng;

    bool aIsNative = true;   // Zuordnung der aktuellen Runde
    char selected = 'A';     // welcher Kanal treibt den Punkt
    juce::Point<float> dot { 0.0f, 0.0f };
    bool hasDot = false;

    int trials = 0, nativeWins = 0, rawWins = 0;
    bool revealed = false;
    juce::String lastResult;

    juce::TextButton selAButton { "Kanal A" }, selBButton { "Kanal B" };
    juce::TextButton verdictAButton { "A feiner" }, verdictBButton { "B feiner" };
    juce::TextButton resolveButton { "resolve" };  // Text via fromUTF8 im Ctor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlindAbMode)
};

} // namespace touchlab
