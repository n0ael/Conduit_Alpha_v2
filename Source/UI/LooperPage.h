#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "LooperPanel.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Papierkorb-Kachel der Looper-Kopfzeile (Big Out 07/2026): sichtbar,
    solange der LooperTrashCan Einträge hält; zeigt ↺ + Rest-Zeit m:ss.
    Die letzten warnSeconds faden zu Rot; läuft ein Eintrag ab, flackert
    die Kachel kurz und verschwindet (User-Spezifikation 19.07.2026).
    Flacker-Ticks via Timer::callAfterDelay + SafePointer (kein
    Member-Timer — Lektion NodeEditor).
*/
class LooperTrashTile final : public juce::Button
{
public:
    static constexpr double warnSeconds = 30.0;

    LooperTrashTile();

    /** [Editor-Timer, 15 Hz] Restzeit + Bestand — Repaint nur bei
        sichtbarer Änderung (Sekunden-/Farbschritt). */
    void setTrashState (double secondsRemaining, bool hasEntries);

    /** Ablauf-Flackern: kurzes Blinken, danach folgt die Sichtbarkeit
        wieder dem Bestand (setTrashState). */
    void flashEmptied();

    [[nodiscard]] bool isFlashing() const noexcept { return flashTicks > 0; }

private:
    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;
    void scheduleFlashTick();

    double remaining = 0.0;
    bool entries = false;
    int shownSeconds = -1;
    int flashTicks = 0;
    bool flashOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperTrashTile)
};

//==============================================================================
/**
    Looper-Page — Vollausbau M6 (Design-Mock + Übergabe-Dokument
    05.07.2026): bis zu 4 Looper-Fenster NEBENEINANDER (User-Entscheidung
    „wie im Mock"), Kopfzeile mit − / + (Looper öffnen/schließen),
    globalem Output-Paar, Spectrum-Umschalter (alle Strips), ⚙
    (Einstellungs-Menü) und Stop (alle Tracks); Statuszeile unten.

    Wie die TransportBar besitzt die Page NUR UI-Zustand: Aktionen laufen
    über std::function-Hooks bzw. die Hooks der LooperPanels (der
    EngineEditor verdrahtet Modell/Bank/Settings/Taps und pollt den
    Status über seinen Timer — inkl. der gemeinsamen Puls-Phase der
    Target-Zellen).
*/
class LooperPage final : public juce::Component
{
public:
    LooperPage();

    //==========================================================================
    // Struktur [Editor]

    /** Panels neu aufbauen (1–4). Nach dem Aufbau verdrahtet der Editor
        die Panel-Hooks neu (onPanelsChanged feuert am Ende). */
    void setLooperCount (int count);
    [[nodiscard]] int getLooperCount() const noexcept { return (int) panels.size(); }
    [[nodiscard]] LooperPanel& getPanel (int looperIndex);

    /** Nach jedem Panel-Neuaufbau (Editor verdrahtet die Hooks). */
    std::function<void()> onPanelsChanged;

    //==========================================================================
    // Kopfzeile

    std::function<void()> onAddLooper;
    std::function<void()> onRemoveLooper;
    std::function<void()> onRestoreTrash;      // ↺ Papierkorb-Kachel (Big Out)
    std::function<void()> onOpenSettings;      // ⚙ → LooperSettingsMenu (Editor)
    std::function<void()> onStop;              // Stop = alle Tracks
    std::function<void (bool spectrum)> onViewToggled;
    std::function<void (int pairIndex)> onOutputPairSelected;

    void setOutputPairs (const juce::StringArray& pairLabels, int selectedPair);
    void setSpectrumView (bool spectrum);
    void setStatus (const juce::String& statusText);

    /** [Editor-Timer] Gemeinsame Puls-Phase (Target-Zellen). */
    void setPulsePhase (float phase01);

    /** [Editor-Timer] Papierkorb-Zustand (Kachel-Sichtbarkeit + Countdown). */
    void setTrashState (double secondsRemaining, bool hasEntries);

    /** Ablauf-Flackern der Papierkorb-Kachel (LooperTrashCan::onExpired). */
    void flashTrashEmptied() { trashTile.flashEmptied(); }

    //==========================================================================
    [[nodiscard]] juce::ComboBox& getOutputCombo() noexcept { return outputCombo; }
    [[nodiscard]] push::TextTile& getSpectrumTile() noexcept { return spectrumTile; }
    [[nodiscard]] push::TextTile& getStopTile() noexcept { return stopTile; }
    [[nodiscard]] push::TextTile& getSettingsTile() noexcept { return settingsTile; }
    [[nodiscard]] push::TextTile& getAddLooperTile() noexcept { return addTile; }
    [[nodiscard]] push::TextTile& getRemoveLooperTile() noexcept { return removeTile; }
    [[nodiscard]] LooperTrashTile& getTrashTile() noexcept { return trashTile; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    push::TextTile removeTile { juce::String::fromUTF8 ("−") };
    push::TextTile addTile { "+" };
    juce::Label outputCaption;
    juce::ComboBox outputCombo;
    push::TextTile spectrumTile { "Spectrum", push::colours::ledOrange };
    push::TextTile settingsTile { juce::String::fromUTF8 ("⚙") };
    LooperTrashTile trashTile;
    push::TextTile stopTile { "Stop", push::colours::ledRed };
    juce::Label statusLabel;

    std::vector<std::unique_ptr<LooperPanel>> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPage)
};

} // namespace conduit
