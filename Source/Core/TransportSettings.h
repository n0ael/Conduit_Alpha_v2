#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
/**
    Transport-/Link-Einstellungen des Push-Headers — App-Zustand, KEIN
    Patch-Zustand (eigene "Conduit/Transport.settings", überlebt Preset-Load,
    kein Undo — gleiche Trennung wie MeterSettings/ChannelNames).

    Felder:
      - startStopSyncEnabled: Link Start/Stop-Sync (Play-Kachel wirkt in die
        ganze Session; aus = nur lokaler Transport-Zustand)
      - clockOffsetMs: Versatz der Link-Beat-Ablesung in Millisekunden
        (±100 ms) — gleicht Interface-/Peer-Latenz an (Muster 8.3);
        der EngineProcessor speist ihn in die LinkClock
      - automate / fixedLength: vorbereitete Toggles für den Rückwärts-
        Looper (Endless-Meilenstein) — Zustand persistiert bereits

    Threading: Setter/Getter Message Thread; ChangeBroadcaster benachrichtigt
    den EngineProcessor (applyTransportSettings → LinkClock) und die UI.
*/
class TransportSettings : public juce::ChangeBroadcaster
{
public:
    static constexpr double maxClockOffsetMs = 100.0;

    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit TransportSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~TransportSettings() override;

    [[nodiscard]] bool isStartStopSyncEnabled() const noexcept { return startStopSync; }
    void setStartStopSyncEnabled (bool enabled);

    [[nodiscard]] double getClockOffsetMs() const noexcept { return clockOffsetMs; }
    void setClockOffsetMs (double offsetMs);  // geclamped auf ±maxClockOffsetMs

    [[nodiscard]] bool isAutomateEnabled() const noexcept { return automate; }
    void setAutomateEnabled (bool enabled);

    [[nodiscard]] bool isFixedLengthEnabled() const noexcept { return fixedLength; }
    void setFixedLengthEnabled (bool enabled);

private:
    void loadFromFile();
    void writeValue (const char* key, const juce::var& value);

    juce::ApplicationProperties applicationProperties;

    bool   startStopSync = true;
    double clockOffsetMs = 0.0;
    bool   automate      = false;
    bool   fixedLength   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportSettings)
};

} // namespace conduit
