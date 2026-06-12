#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Capture/CaptureService.h"
#include "Core/Capture/CaptureSettings.h"

namespace conduit
{

//==============================================================================
/**
    Einklappbares Capture-Panel unter der Toolbar — Übergangslösung, bis die
    Kanal-Anzeigen mit dem Mixer-Meilenstein in die Channel-Strips wandern.

    Read/listen-only gegenüber der Engine (CLAUDE.md 6): die Controls
    schreiben ausschließlich in CaptureSettings [Message Thread]. Rechts
    eine Zeile pro Input-Kanal — Status-LED, Mini-Pegel mit Noise-Floor-
    Marker (InputMeter-Atomics) und Einzel-Capture-Button (44 px,
    CaptureService::exportChannel) — gefüttert vom EngineEditor-Timer über
    refresh() (kein eigener Timer, Repaint nur bei quantisierter Änderung).
    Die Kanalanzahl folgt dem aktiven Device: prepare() feuert einen
    ChangeBroadcast, refresh() prüft zusätzlich pro Tick (defensiv).

    Virtuelle Kanäle (Capture-Taps): unter den Hardware-Zeilen erscheint
    ein Abschnitt "Taps" mit denselben Zeilen (LED/Pegel/Einzel-Capture) —
    Zeilenname = registrierter Spurname (moduleId). Register/Unregister/
    Rename feuern einen ChangeBroadcast des Service; ein Tap ohne Puffer
    (Erweiterung wartet auf inaktive Kanäle) zeigt seine Zeile mit
    idle-LED und stummem Pegel.

    Resize-Policy-UI (CaptureSettings-Doku): bufferMinutes/preRollSeconds
    laufen über die Settings-Setter; bei aktiver Aufnahme feuert
    onPendingResize → async Ok/Cancel-AlertWindow
    (JUCE_MODAL_LOOPS_PERMITTED=0) → confirm-/cancelPendingResize.

    "Nach Export freigeben" schaltet nur die Nachfrage frei — der
    RAM-Puffer wird NIE ohne Bestätigung geleert (der Dialog selbst kommt
    vom EngineEditor beim Export-Abschluss).

    Touch-first (10): alle Controls 44 px hoch, Maus/Keyboard-Parität über
    native JUCE-Controls.
*/
class CapturePanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    static constexpr int preferredHeight = 128;

    CapturePanel (CaptureSettings& settingsToUse, CaptureService& serviceToUse);
    ~CapturePanel() override;

    /** [Message Thread, Editor-Timer] Kanal-Zeilen aus den Status-Atomics
        aktualisieren — Repaint nur bei sichtbarer Änderung. */
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Toast-Ausgabe des Editors (Einzel-Capture-Feedback). */
    std::function<void (const juce::String&)> onToast;

private:
    //==========================================================================
    /** Eine Kanal-Zeile: Status-LED, Mini-Pegel (RMS-Füllung, Peak-Strich)
        mit Noise-Floor-Marker, Einzel-Capture-Button. Reine Anzeige plus
        Button — gefüttert über setDisplayState() (quantisierte Werte als
        Repaint-Schwelle). */
    class ChannelRow : public juce::Component
    {
    public:
        /** captureIndex = Kanal-Index beim Service (Hardware oder
            virtueller Slot); -1 = Tap ohne Puffer (nur Name + idle-LED). */
        ChannelRow (int captureIndexToUse, juce::String nameToUse,
                    std::function<void (int, juce::String)> onCaptureToUse);

        struct DisplayState
        {
            CaptureChannel::State state = CaptureChannel::State::idle;
            int rmsSteps = 0;    // dB-Position [-80..0] auf 64 Stufen
            int peakSteps = 0;
            int floorSteps = 0;
            bool operator== (const DisplayState&) const = default;
        };

        void setDisplayState (const DisplayState& newState);

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        int captureIndex;
        juce::String name;
        DisplayState display;
        juce::TextButton captureButton { "CAP" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
    };

    /** Soll-Zustand der Zeilenliste — Hardware-Kanäle plus genutzte
        Tap-Slots; Vergleichsbasis für den Rebuild (Repaint-Disziplin). */
    struct RowSpec
    {
        int captureIndex = -1;
        juce::String name;
        bool isTap = false;
        bool operator== (const RowSpec&) const = default;
    };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void syncControls();
    void chooseExportDirectory();
    void applyRingSlider (juce::Slider& slider, bool isBufferMinutes);
    [[nodiscard]] std::vector<RowSpec> makeRowSpecs() const;
    void rebuildChannelRows();
    void layoutChannelRows();
    void captureSingleChannel (int captureIndex, const juce::String& rowName);

    CaptureSettings& settings;
    CaptureService& service;

    juce::Slider thresholdSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider holdSlider      { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider preRollSlider   { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider bufferSlider    { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    // Umlaute als escaped UTF-8 — MSVC liest BOM-lose Quellen als CP1252
    juce::ToggleButton autoCalibrateToggle      { "Auto-Schwelle" };
    juce::ToggleButton releaseAfterExportToggle {
        juce::String::fromUTF8 ("Nach Export freigeben (mit R\xc3\xbc" "ckfrage)") };
    juce::ComboBox bitDepthCombo;
    juce::TextButton directoryButton { "Ordner..." };
    juce::Label directoryLabel;
    juce::Label ramWarningLabel;

    // Kanal-Zeilen im Viewport (Kanalzahl kann die Panel-Höhe übersteigen)
    juce::Viewport channelViewport;
    juce::Component channelContainer;
    std::vector<std::unique_ptr<ChannelRow>> channelRows;
    std::vector<RowSpec> currentRowSpecs;     // parallel zu channelRows
    juce::Label tapsHeaderLabel { {}, "Taps" };
    juce::Rectangle<int> channelArea;

    // Muss den async Callback überleben (JUCE_MODAL_LOOPS_PERMITTED=0)
    std::unique_ptr<juce::FileChooser> directoryChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturePanel)
};

} // namespace conduit
