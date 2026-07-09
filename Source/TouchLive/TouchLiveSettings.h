#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
/**
    Einstellungen der TouchLive-Remote (docs/TouchLive.md) — App-Zustand,
    KEIN Patch-Zustand (eigene juce::PropertiesFile "Conduit/TouchLive.settings",
    überlebt Preset-Load, kein Undo — gleiche Trennung wie MeterSettings/
    OscSendSettings).

    Host = Rechner mit Ableton Live + ConduitRemote-Script. Command-Port 9010
    (dorthin sendet Conduit), Listen-Port 9011 (dort antwortet das Script an
    die Absender-IP) — bewusst getrennt von Conduits eigenem OSC (9000/9001).
    Enabled default aus.

    Threading: Setter/Getter auf dem Message Thread. UI und TouchLiveClient
    lauschen über juce::ChangeBroadcaster.
*/
class TouchLiveSettings : public juce::ChangeBroadcaster
{
public:
    static constexpr int defaultCommandPort = 9010;
    static constexpr int defaultListenPort  = 9011;
    static constexpr const char* defaultHost = "127.0.0.1";

    // Kanalzug-Breite der Mixer-Ansicht (User-Entscheidung 09.07.2026:
    // einstellbar, wie viele Tracks parallel sichtbar sind)
    static constexpr int defaultChannelWidth = 96;
    static constexpr int minChannelWidth     = 56;
    static constexpr int maxChannelWidth     = 200;

    /** Eigene Datei neben Meter.settings / OscSend.settings. */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit TouchLiveSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~TouchLiveSettings() override;

    [[nodiscard]] juce::String getHost() const noexcept { return host; }
    void setHost (const juce::String& newHost);

    [[nodiscard]] int getCommandPort() const noexcept { return commandPort; }
    void setCommandPort (int newPort);

    [[nodiscard]] int getListenPort() const noexcept { return listenPort; }
    void setListenPort (int newPort);

    [[nodiscard]] bool isEnabled() const noexcept { return enabled; }
    void setEnabled (bool shouldConnect);

    /** Breite eines Mixer-Kanalzugs in px (geklemmt [min,max]) — die
        dB-Skala des Faders passt ihre Label-Dichte daran an. */
    [[nodiscard]] int getChannelWidth() const noexcept { return channelWidth; }
    void setChannelWidth (int newWidth);

private:
    void loadFromFile();
    void store (const char* key, const juce::var& value);

    juce::ApplicationProperties applicationProperties;

    juce::String host { defaultHost };
    int commandPort = defaultCommandPort;
    int listenPort  = defaultListenPort;
    bool enabled = false;
    int channelWidth = defaultChannelWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveSettings)
};

} // namespace conduit
