#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/TransportSettings.h"

namespace conduit
{

class LinkClock;

//==============================================================================
/**
    Dropdown-Panel hinter dem Link-▾ der TransportBar (User-Wunsch
    2026-07-02): Start/Stop-Sync an/aus und der Clock-Offset in ms —
    als CallOutBox, dunkler Push-Look über den Default-LookAndFeel.

    Schreibt NUR in die TransportSettings; der EngineProcessor lauscht als
    ChangeListener und speist die LinkClock (gleiches Muster wie
    MeterSettings → LevelMeter). Peer-Zahl ist reine Anzeige (LinkClock,
    beim Öffnen gelesen — das Panel lebt nur wenige Sekunden).
*/
class LinkMenuPanel final : public juce::Component
{
public:
    /** metronomeTargets: Beschriftungen der Stereo-Paare (Kanäle 2n/2n+1)
        für die Metronom-Ziel-Auswahl — leer = Auswahl ausgeblendet. */
    LinkMenuPanel (TransportSettings& settingsToUse, const LinkClock& linkClock,
                   const juce::StringArray& metronomeTargets = {});

    void resized() override;
    void paint (juce::Graphics& g) override;

    static constexpr int panelWidth  = 260;
    static constexpr int panelHeight = 232;

    // Test-Zugriff
    [[nodiscard]] juce::ToggleButton& getSyncToggle() noexcept { return syncToggle; }
    [[nodiscard]] juce::Slider& getOffsetSlider() noexcept { return offsetSlider; }
    [[nodiscard]] juce::Slider& getTapCountSlider() noexcept { return tapCountSlider; }
    [[nodiscard]] juce::ComboBox& getMetronomeTargetBox() noexcept { return metronomeTargetBox; }

private:
    TransportSettings& settings;

    juce::Label peersLabel;
    juce::ToggleButton syncToggle { "Start/Stop-Sync" };
    juce::Label offsetCaption;
    juce::Slider offsetSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label tapCaption;
    juce::Slider tapCountSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label metronomeCaption;
    juce::ComboBox metronomeTargetBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkMenuPanel)
};

} // namespace conduit
