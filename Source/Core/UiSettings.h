#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
/**
    Oberflächen-Einstellungen — App-Zustand, KEIN Patch-Zustand (eigene
    juce::PropertiesFile "Conduit/Ui.settings", Muster MeterSettings).

      - uiScale:   globale UI-Skalierung wie Ableton (0.5–2.0, Default 1.0)
      - fontScale: separater Schriftgrößen-Faktor (0.8–1.4, Default 1.0)
      - devMode:   erst wenn aktiv, zeigen Modul-Header ihren DEV-Toggle

    WICHTIG: diese Klasse SPEICHERT nur und broadcastet Änderungen — sie ruft
    nie juce::Desktop::setGlobalScaleFactor und fasst keine Fonts an. Die
    Anwendung übernehmen Main.cpp (Start) und der EngineEditor (live) —
    dadurch bleibt die Klasse headless pur testbar und Tests setzen nie
    globalen Desktop-Zustand.

    Threading: Setter/Getter auf dem Message Thread.
*/
class UiSettings : public juce::ChangeBroadcaster
{
public:
    static constexpr float minUiScale     = 0.5f;
    static constexpr float maxUiScale     = 2.0f;
    static constexpr float defaultUiScale = 1.0f;

    static constexpr float minFontScale     = 0.8f;
    static constexpr float maxFontScale     = 1.4f;
    static constexpr float defaultFontScale = 1.0f;

    /** Eigene Datei neben Meter.settings / Transport.settings. */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit UiSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~UiSettings() override;

    [[nodiscard]] float getUiScale() const noexcept { return uiScale; }
    void setUiScale (float scale);

    [[nodiscard]] float getFontScale() const noexcept { return fontScale; }
    void setFontScale (float scale);

    [[nodiscard]] bool isDevModeEnabled() const noexcept { return devModeEnabled; }
    void setDevModeEnabled (bool enabled);

private:
    void loadFromFile();

    juce::ApplicationProperties applicationProperties;
    float uiScale        = defaultUiScale;
    float fontScale      = defaultFontScale;
    bool  devModeEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiSettings)
};

} // namespace conduit
