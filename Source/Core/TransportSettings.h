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

    /** Taps, die das Tempo erfassen — der (n+1)-te Tap committet zur Session
        (Tap-and-Commit, User-Entwurf 2026-07-02). Clamp 2..8, Default 4. */
    [[nodiscard]] int getTapCount() const noexcept { return tapCount; }
    void setTapCount (int taps);

    /** Metronom (Link-synchroner Click): an/aus + Stereo-Anker der
        Ziel-Kanäle (Paar n = Kanäle 2n/2n+1, z. B. 1 = Headphones). */
    [[nodiscard]] bool isMetronomeEnabled() const noexcept { return metronome; }
    void setMetronomeEnabled (bool enabled);

    [[nodiscard]] int getMetronomeAnchor() const noexcept { return metronomeAnchor; }
    void setMetronomeAnchor (int pairIndex);  // Clamp 0..31

private:
    void loadFromFile();
    void writeValue (const char* key, const juce::var& value);

    juce::ApplicationProperties applicationProperties;

    bool   startStopSync = true;
    double clockOffsetMs = 0.0;
    bool   automate      = false;
    bool   fixedLength   = false;
    int    tapCount      = 4;
    bool   metronome        = false;
    int    metronomeAnchor  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportSettings)
};

} // namespace conduit
