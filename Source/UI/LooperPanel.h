#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "LooperClipControlsRow.h"
#include "LooperTrackStrip.h"
#include "LooperWaveformStrip.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Ein Looper-Fenster der Looper-Page (M6, Design-Mock 05.07.2026):
    Kopfzeile (LOOPER n · Quellen-Combo · LED) → Waveform-/Spektrum-Strip
    (Commit-Segmente 8/4/2/1) → Clip-Controls-Leiste → Track-Spalten
    nebeneinander (+ „+"-Kachel für neue Tracks, max. 4).

    Reine UI (Muster TransportBar/LooperPage): Aktionen als Hooks mit
    Track-/Slot-Index, Zustand per Setter/Accessor — der EngineEditor
    verdrahtet Modell, Bank, Settings und Taps.
*/
class LooperPanel final : public juce::Component
{
public:
    explicit LooperPanel (int looperNumber);

    //==========================================================================
    // Hooks [Editor]
    std::function<void (const juce::String& sourceKey)> onSourceSelected;
    std::function<void (bool spectrum)> onViewToggled;   // FFT/WAVE pro Looper (07/2026)
    std::function<void (int bars)> onSegmentClicked;
    std::function<void (int trackIndex, int slotIndex)> onSlotTapped;
    std::function<void (int trackIndex, float gain01)> onTrackGain;
    std::function<void (int trackIndex, float pan)> onTrackPan;
    std::function<void (int trackIndex, float distance01)> onTrackDistance;
    std::function<void (int trackIndex, int sendIndex, float level01)> onTrackSendLevel;
    std::function<void (int trackIndex, bool muted)> onTrackMute;
    std::function<void (int trackIndex, bool solo)> onTrackSolo;
    std::function<void (int trackIndex)> onTrackStop;
    std::function<void (int trackIndex)> onTrackHeaderLongPress;
    std::function<void (int trackIndex)> onTrackHeaderTapped;   // M7: Delete-Geste
    std::function<void()> onAddTrack;

    //==========================================================================
    // Struktur [Editor]

    void setTrackCount (int count);
    [[nodiscard]] int getTrackCount() const noexcept { return (int) tracks.size(); }
    [[nodiscard]] LooperTrackStrip& getTrack (int trackIndex);

    void setVisibleSlots (int count);

    /** Quellen-Liste (Muster alte LooperPage). colour tönt den Menü-
        Eintrag UND den Combo-Text der Auswahl (Quellfarbe, 09.07.2026);
        transparent = Standard-Text. separatorBefore zieht einen Trenner
        VOR dem Eintrag — Gruppen: lokale Quellen | Link-Quellen, und
        innerhalb der Link-Gruppe pro Peer (App/Programm). */
    struct Source
    {
        juce::String key;
        juce::String label;
        juce::Colour colour {};        // transparent = Standard-Textfarbe
        bool separatorBefore = false;
    };
    void setSources (std::vector<Source> sources, const juce::String& selectedKey);

    /** [Editor-Timer] LED der Kopfzeile (irgendein Track hörbar). */
    void setAudible (bool audible);

    /** Spektrum-Ansicht dieses Loopers (FFT-Kachel + Strip) — Zustand
        kommt aus den LooperSettings (Editor); die frühere MST-Kachel
        lebt als globaler Toggle im MIXER-Tab (User 20.07.2026). */
    void setSpectrumView (bool spectrum);

    /** Gemeinsame Puls-Phase der Target-Zellen. */
    void setPulsePhase (float phase01);

    [[nodiscard]] LooperWaveformStrip& getStrip() noexcept { return strip; }
    [[nodiscard]] LooperClipControlsRow& getControls() noexcept { return controls; }
    [[nodiscard]] juce::ComboBox& getSourceCombo() noexcept { return sourceCombo; }
    [[nodiscard]] push::TextTile& getViewTile() noexcept { return viewTile; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void wireTrack (LooperTrackStrip& track, int trackIndex);
    void applySelectedSourceColour();

    int looperNumber;
    bool audible = false;

    juce::ComboBox sourceCombo;
    push::TextTile viewTile { "FFT", push::colours::ledCyan };
    std::vector<Source> currentSources;

    LooperWaveformStrip strip;
    LooperClipControlsRow controls;
    std::vector<std::unique_ptr<LooperTrackStrip>> tracks;
    int visibleSlots = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};

} // namespace conduit
