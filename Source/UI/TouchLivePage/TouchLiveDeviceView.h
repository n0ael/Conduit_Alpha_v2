#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSetModel.h"
#include "TouchLive/LiveSpectrumTap.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveMeterBus.h"
#include "TouchLive/TouchLiveSettings.h"
#include "TouchLiveBespokePanel.h"
#include "UI/PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    DEVICE-Sub-Tab der TouchLive-Page (M3, docs/TouchLive.md §6b):
    generische Fernsteuerung JEDES Live-Devices.

    Aufbau: Track-Chips (oben) → Device-Chips der Kette des gewählten
    Tracks (mit On/Off-LED) → Parameter-Bank mit 8 Fader-Spalten
    (Name · Slider · Wertetext) → Fußzeile ‹ · Bank x/y · › · ON.

    Datenquelle ist die devices-Domain des LiveSetModel (M3-Wire-Form):
    `chain:{tid}` = Ketten-Array, `dev:{dvid}` = Item (name/class_name/
    is_active), `parmeta:{dvid}` = statische Parameter-Metadaten,
    `parvals:{dvid}` = heiße Werte-Zeile. parameters[0] ist Lives
    „Device On" — die Bänke beginnen bei Index 1, der Schalter läuft
    über /live/device/set/is_active.

    Schreibweg (Feel-Regeln §5.1): Slider-Drag lokal-optimistisch +
    noteTouchedParameter("devices"/parvals-Zeile) + sendTouchValue
    (/live/device/set/parameter [dvid, index, value], Fast-Path der
    Gegenseite). Struktur-Änderungen rebuilden coalesced (AsyncUpdater);
    reine parvals-Diffs aktualisieren nur die Slider.

    Gain Reduction (User-Wunsch 09.07.2026, Push-Vorbild): Dynamics-Devices
    senden ihre GR als dv:-Tripel im Meter-Frame — die View pollt den
    MeterBus @ 30 Hz (roh, §5.1) und zeigt eine GR-Spalte rechts der Bank,
    sobald das gewählte Device je einen Wert geliefert hat.

    Bespoke-UIs (M5, §6b): liefert die Registry (createBespokePanel) für
    die class_name des gewählten Devices ein Panel UND meldet es
    isUsable(), ersetzt es die Bank; die viewTile (BANK ↔ Kürzel) schaltet
    jederzeit aufs generische Panel zurück — dort bleiben ALLE Parameter
    erreichbar. Kein Treffer/nicht nutzbar → Bank wie gehabt.
*/
class TouchLiveDeviceView final : public juce::Component,
                                  private juce::ValueTree::Listener,
                                  private juce::AsyncUpdater,
                                  private juce::Timer
{
public:
    /** spectrumTap darf nullptr sein (Tests) — dann keine SPEC-Kachel. */
    TouchLiveDeviceView (TouchLiveClient& clientToUse, LiveSetModel& modelToUse,
                         TouchLiveMeterBus& meterBusToUse,
                         LiveSpectrumTap* spectrumTapToUse = nullptr);
    ~TouchLiveDeviceView() override;

    static constexpr int parametersPerBank = 8;
    static constexpr int chipRowHeight = 34;
    static constexpr int footerHeight = 36;

    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Auswahl (Laufzeit-Zustand, nie persistiert)
    void selectTrack (const juce::String& trackKey);
    void selectDevice (const juce::String& deviceKey);
    void setBank (int newBank);

    [[nodiscard]] const juce::String& getSelectedTrackKey() const noexcept { return selectedTrack; }
    [[nodiscard]] const juce::String& getSelectedDeviceKey() const noexcept { return selectedDevice; }
    [[nodiscard]] int getBank() const noexcept { return bank; }
    [[nodiscard]] int getBankCount() const;

    //==========================================================================
    // Bespoke (M5)
    [[nodiscard]] bool isBespokeActive() const noexcept;
    [[nodiscard]] TouchLiveBespokePanel* getBespokePanel() noexcept { return bespokePanel.get(); }
    void setBespokePreferred (bool shouldPreferBespoke);

    //==========================================================================
    // Test-Seams
    void flushPendingRebuild();

    /** Ein GR-Meter-Tick sofort (der 30-Hz-Timer ruft dieselbe Logik). */
    void refreshGainReductionNow();
    [[nodiscard]] float getGainReductionLevel() const noexcept { return grLevel; }
    [[nodiscard]] int getTrackChipCount() const noexcept { return (int) trackChips.size(); }
    [[nodiscard]] int getDeviceChipCount() const noexcept { return (int) deviceChips.size(); }
    [[nodiscard]] juce::Slider* getParameterSlider (int column);
    [[nodiscard]] juce::String getParameterName (int column) const;
    [[nodiscard]] juce::String getParameterValueText (int column) const;

    push::TextTile onTile { "ON", push::colours::ledOrange };
    push::TextTile bankPrevTile { "<" };
    push::TextTile bankNextTile { ">" };
    push::TextTile viewTile { "BANK", push::colours::ledCyan };
    push::TextTile spectrumTile { "SPEC", push::colours::ledCyan };
    push::ValueTile averagingTile { "spectrumAvg" };

private:
    //==========================================================================
    /** Eine Fader-Spalte der Parameter-Bank. */
    struct ParameterStrip
    {
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::String name;
        juce::String valueText;
        int parameterIndex = -1;   // Index in parvals (−1 = Spalte leer)
        bool quantized = false;
        juce::StringArray items;
        juce::Rectangle<int> bounds;
    };

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void rebuild();
    void rebuildParameterBank();
    void refreshValues();
    void updateBespokeVisibility();
    void layoutChips();
    void sendParameter (int parameterIndex, float value);
    void toggleDeviceActive();

    [[nodiscard]] juce::ValueTree devicesDomain() const;
    [[nodiscard]] juce::var chainOf (const juce::String& trackKey) const;
    [[nodiscard]] static juce::String valueTextFor (const ParameterStrip& strip, double value);

    void cycleSpectrumMode();
    void refreshSpectrumTiles();

    TouchLiveClient& client;
    LiveSetModel& model;
    TouchLiveMeterBus& meterBus;
    LiveSpectrumTap* spectrumTap = nullptr;   // nullptr in Tests
    double averagingAtDragStart = 0.5;

    // Listener-Handle als Member (Lektion M1b)
    juce::ValueTree modelState;

    // Gain Reduction des gewählten Devices (roh aus dem MeterBus)
    juce::uint32 lastMeterFrame = 0;
    float grLevel = 0.0f;
    bool grSeen = false;
    juce::Rectangle<int> grBounds;

    // Chips (dynamisch, in Viewport-Zeilen)
    juce::Viewport trackChipViewport, deviceChipViewport;
    juce::Component trackChipRow, deviceChipRow;
    std::vector<std::unique_ptr<push::TextTile>> trackChips;
    std::vector<std::unique_ptr<push::TextTile>> deviceChips;

    std::array<ParameterStrip, parametersPerBank> strips;

    // Bespoke-Panel (M5): pro class_name höchstens eins, Laufzeit-Zustand
    std::unique_ptr<TouchLiveBespokePanel> bespokePanel;
    juce::String bespokeClassName;
    bool bespokePreferred = true;

    juce::String selectedTrack, selectedDevice;
    int bank = 0;
    bool deviceActive = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveDeviceView)
};

} // namespace conduit
