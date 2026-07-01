#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace conduit
{

class EngineProcessor;

//==============================================================================
/**
    App-Layer-Bündelung des Audio-Geräte-Handlings (CLAUDE.md 9 — Plattform-
    Setup gehört in die App, nicht in DSP-Module). Besitzt den
    juce::AudioDeviceManager und den juce::AudioProcessorPlayer, der die
    Hardware-Callbacks an den EngineProcessor bridged.

    Motiv für die eigene Klasse: dieselbe Glue-Logik (ChannelNames-Kontext +
    audioSetupWarning) muss beim Start UND bei jeder Geräteänderung über das
    Settings-UI laufen. Der AudioDeviceManager ist ein ChangeBroadcaster — der
    Controller lauscht als ChangeListener und wendet die Logik erneut an.

    Persistenz: eigene juce::PropertiesFile (Conduit/AudioDevice.settings),
    App-Zustand wie ChannelNames/CaptureSettings — überlebt Preset-Load, kein
    Undo. Der Force auf 48 kHz / 32 Samples (CLAUDE.md 3.2) passiert nur beim
    allerersten Start ohne gespeicherten Zustand; eine bewusste Nutzerwahl
    (z. B. 64 Samples) bleibt danach erhalten.

    Threading: alle Methoden laufen auf dem Message Thread.
*/
class AudioDeviceController final : private juce::ChangeListener
{
public:
    /** Eigene Datei neben Conduit.settings / ChannelNames.settings. */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit AudioDeviceController (const juce::PropertiesFile::Options& options = defaultOptions());
    ~AudioDeviceController() override;

    //==========================================================================
    /** Audio starten: gespeicherten Zustand laden (sonst Defaults + Force
        48k/32), Player mit dem Engine-Prozessor verbinden, Listener
        registrieren und den aktiven Device-Kontext einmal anwenden. */
    void initialise (EngineProcessor& engine);

    /** Callback/Listener lösen, Prozessor abkoppeln — vor Engine-Destruktion. */
    void shutdown();

    /** Für die AudioSettingsComponent (juce::AudioDeviceSelectorComponent). */
    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    //==========================================================================
    /** Reiner Helfer (testbar ohne Device): leerer String = alles im Ziel
        (48000/44100 Hz, Buffer ≤ 64), sonst der Warntext für die Toolbar. */
    [[nodiscard]] static juce::String computeWarning (double sampleRate, int bufferSize);

private:
    // juce::ChangeListener [Message Thread] — Device-Setup hat sich geändert
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** ChannelNames-Kontext setzen, audioSetupWarning setzen/löschen, Zustand
        persistieren — aus initAudio() extrahiert, läuft bei Start + Wechsel. */
    void applyActiveDevice();
    void saveDeviceState();

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    juce::ApplicationProperties applicationProperties;

    EngineProcessor* engine = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceController)
};

} // namespace conduit
