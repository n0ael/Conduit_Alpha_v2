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
      - dspMeter:  DSP-/XRun-Anzeige in der TransportBar (Default an, wie
                   Abletons CPU-Meter) — bewusst UNABHÄNGIG vom Dev-Modus
                   (User-Entscheidung 04.07.2026)
      - softKeyboard: On-Screen-Tastatur des Browser-Suchfelds (M5) —
                   Plattform-Default: AN auf Linux (Kiosk/Touch), AUS auf
                   Desktop (Windows/macOS); zur Laufzeit umschaltbar

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

    [[nodiscard]] bool isDspMeterEnabled() const noexcept { return dspMeterEnabled; }
    void setDspMeterEnabled (bool enabled);

#if JUCE_LINUX
    static constexpr bool defaultSoftKeyboardEnabled = true;
#else
    static constexpr bool defaultSoftKeyboardEnabled = false;
#endif

    [[nodiscard]] bool isSoftKeyboardEnabled() const noexcept { return softKeyboardEnabled; }
    void setSoftKeyboardEnabled (bool enabled);

private:
    void loadFromFile();

    juce::ApplicationProperties applicationProperties;
    float uiScale         = defaultUiScale;
    float fontScale       = defaultFontScale;
    bool  devModeEnabled  = false;
    bool  dspMeterEnabled = true;
    bool  softKeyboardEnabled = defaultSoftKeyboardEnabled;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiSettings)
};

} // namespace conduit
