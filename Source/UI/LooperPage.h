#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "LooperWaveformStrip.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Retro-Looper-Page (Looper-Baustein B3) — erreichbar über die Tape-Kachel
    (oo) der TransportBar, Page-Index TransportBar::pageLooper.

    Endlesss-Muster: der Looper nimmt immer auf (Capture-Ring), die Page
    zeigt die letzten 8 Takte der gewählten Quelle als gestauchte Wellenform
    (LooperWaveformStrip, B4) — ein Klick auf ein Segment committet
    rückwirkend die letzten 8/4/2/1 Takte (Baustein B5).

    Wie die TransportBar besitzt die Page NUR UI-Zustand: Aktionen laufen
    über std::function-Hooks (der EngineEditor verdrahtet Engine +
    Persistenz), Status kommt über Setter vom Editor-Timer.

    Quell-Schlüssel (persistiert in TransportSettings::looperSource):
      "master"      — Session-Summe (Master-Output-Tap, B2)
      "hw:{paar}"   — Hardware-Eingangs-Paar 2n/2n+1 (ChannelNames-Labels)
      "tap:{name}"  — Capture-Tap eines Moduls (Basisname ohne _l/_r)
*/
class LooperPage final : public juce::Component
{
public:
    LooperPage();

    //==========================================================================
    struct Source
    {
        juce::String key;    // Schlüssel (siehe Klassendoku)
        juce::String label;  // Anzeige, z. B. "Master", "In 1/2", "Tap: delay_1"
    };

    /** [Editor] Quellen-Liste neu aufbauen; selectedKey = persistierte
        Auswahl (unbekannter Schlüssel → erste Quelle, ohne Notification). */
    void setSources (std::vector<Source> sources, const juce::String& selectedKey);

    /** Klick-Auswahl einer Quelle — der Editor armt den Capture-Kanal und
        persistiert den Schlüssel. */
    std::function<void (const juce::String& sourceKey)> onSourceSelected;

    /** [Editor-Timer] Statuszeile (B5: „spielt: 4 Bars" etc.). */
    void setStatus (const juce::String& statusText);

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

    // Test-/Editor-Zugriff
    [[nodiscard]] juce::ComboBox& getSourceCombo() noexcept { return sourceCombo; }
    [[nodiscard]] push::TextTile& getStopTile() noexcept { return stopTile; }
    [[nodiscard]] LooperWaveformStrip& getStrip() noexcept { return strip; }

private:
    juce::Label sourceCaption;
    juce::ComboBox sourceCombo;
    push::TextTile stopTile { "Stop", push::colours::ledRed };
    juce::Label statusLabel;

    std::vector<Source> currentSources;

    // Gestauchte Wellenform der letzten 8 Takte (B4) — Segment-Klicks
    // verdrahtet der Editor über strip.onSegmentClicked
    LooperWaveformStrip strip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPage)
};

} // namespace conduit
