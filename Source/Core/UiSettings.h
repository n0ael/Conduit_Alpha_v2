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
      - uiFpsLimit: globale Obergrenze der UI-Refresh-Rate (User-Regel
                   14.07.2026: Anzeige läuft nativ per VBlank, gedeckelt) —
                   120 = Nativ (max 120, VBlank liefert ohnehin höchstens
                   die Monitor-Rate), Drossel-Modi 60 und 30. Angewendet
                   über uiframe::setFpsLimit (UiFramePacer) durch den
                   EngineEditor als ChangeListener.

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

    static constexpr int defaultUiFpsLimit = 120;   // = Nativ (max 120)

    /** 120 (Nativ, max) | 60 | 30 — andere Werte werden auf den nächsten
        erlaubten Modus geklemmt. */
    [[nodiscard]] int getUiFpsLimit() const noexcept { return uiFpsLimit; }
    void setUiFpsLimit (int limitFps);

    // Interaktions-Zoom-Grenze der Node-Canvas (ADR 008 M3a, User-Entscheidung
    // 18.07.2026): unterhalb dieses Zoom-Faktors sind Module reine
    // Navigationsziele (Touch-Targets fielen unter die 44-px-Regel) —
    // Dev-Tuning-Wert zum Experimentieren pro Gerät.
    static constexpr float minInteractionMinZoom     = 0.1f;
    static constexpr float maxInteractionMinZoom     = 1.0f;
    static constexpr float defaultInteractionMinZoom = 0.5f;

    [[nodiscard]] float getInteractionMinZoom() const noexcept { return interactionMinZoom; }
    void setInteractionMinZoom (float zoomThreshold);

    // Pinch-Schwelle (ADR 008 M3a, User-Feedback 18.07.2026): relative
    // Spread-Änderung, ab der eine 2-Finger-Bewegung als Zoom zählt —
    // ungenaue Touchscreens/unsaubere Bewegungen brauchen mehr Toleranz
    // beim reinen Pannen. 0 = jede Änderung zoomt sofort. Dev-Tuning.
    static constexpr float minPinchDeadZone     = 0.0f;
    static constexpr float maxPinchDeadZone     = 0.30f;
    static constexpr float defaultPinchDeadZone = 0.06f;

    [[nodiscard]] float getPinchDeadZone() const noexcept { return pinchDeadZone; }
    void setPinchDeadZone (float spreadFraction);

    // Zoom-Antwort der Pinch-Geste (ADR 008 M3a, User-Feedback 18.07.2026):
    // zoomStrength senkt die Gesamt-Geschwindigkeit (< 100 % = träger),
    // zoomCurve > 1 lässt den Zoom langsam beginnen und kontinuierlich
    // stärker werden (progressiv statt linear). Dev-Tuning pro Gerät.
    static constexpr float minZoomStrength     = 0.1f;
    static constexpr float maxZoomStrength     = 1.0f;
    static constexpr float defaultZoomStrength = 0.6f;

    static constexpr float minZoomCurve     = 1.0f;   // linear
    static constexpr float maxZoomCurve     = 3.0f;
    static constexpr float defaultZoomCurve = 1.6f;

    [[nodiscard]] float getZoomStrength() const noexcept { return zoomStrength; }
    void setZoomStrength (float gain);

    [[nodiscard]] float getZoomCurve() const noexcept { return zoomCurve; }
    void setZoomCurve (float exponent);

    // Gesten-Glättung (ADR 008 M3a, Release-Smoke 18.07.2026): EMA-Tiefpass
    // auf die 2-Finger-Geste gegen Sensor-Rauschen des Touchscreens
    // (Zittern beim Pannen). 0 = aus; höher = ruhiger, aber mehr Latenz.
    static constexpr float minGestureSmoothing     = 0.0f;
    static constexpr float maxGestureSmoothing     = 0.9f;
    static constexpr float defaultGestureSmoothing = 0.5f;

    [[nodiscard]] float getGestureSmoothing() const noexcept { return gestureSmoothing; }
    void setGestureSmoothing (float amount);

    // Zoom-PEGEL der Node-Canvas (ADR 008 M4, Quasimode-Prinzip: Gesten
    // bleiben deterministisch, nur die Stufen sind justierbar):
    // Arbeits-Zoom = Ziel nach Birdeye-Loslassen/Übersicht-Sprung,
    // Birdeye-Zoom = Übersichts-Stufe des 3-Finger-HOLD.
    static constexpr float minWorkZoom     = 0.5f;
    static constexpr float maxWorkZoom     = 2.0f;
    static constexpr float defaultWorkZoom = 1.0f;

    static constexpr float minBirdeyeZoom     = 0.1f;
    static constexpr float maxBirdeyeZoom     = 0.5f;
    static constexpr float defaultBirdeyeZoom = 0.22f;

    [[nodiscard]] float getWorkZoom() const noexcept { return workZoom; }
    void setWorkZoom (float level);

    [[nodiscard]] float getBirdeyeZoom() const noexcept { return birdeyeZoom; }
    void setBirdeyeZoom (float level);

private:
    void loadFromFile();

    juce::ApplicationProperties applicationProperties;
    float uiScale         = defaultUiScale;
    float fontScale       = defaultFontScale;
    bool  devModeEnabled  = false;
    bool  dspMeterEnabled = true;
    bool  softKeyboardEnabled = defaultSoftKeyboardEnabled;
    int   uiFpsLimit = defaultUiFpsLimit;
    float interactionMinZoom = defaultInteractionMinZoom;
    float pinchDeadZone = defaultPinchDeadZone;
    float zoomStrength = defaultZoomStrength;
    float zoomCurve = defaultZoomCurve;
    float gestureSmoothing = defaultGestureSmoothing;
    float workZoom = defaultWorkZoom;
    float birdeyeZoom = defaultBirdeyeZoom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiSettings)
};

} // namespace conduit
