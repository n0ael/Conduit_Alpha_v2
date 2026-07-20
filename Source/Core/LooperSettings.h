#pragma once

#include <array>

#include <juce_data_structures/juce_data_structures.h>

#include "LaunchQuantization.h"
#include "Looper/LooperClipMath.h"

namespace conduit
{

//==============================================================================
/**
    Persistenter Zustand der Looper-Page (M5) — App-Zustand, KEIN
    Patch-Zustand (Muster MeterSettings/ChannelNames: loadPreset() ersetzt
    den Root-Tree, die Looper-Struktur bleibt, kein Undo). Clips selbst
    sind bewusst session-flüchtig (User-Entscheidung 05.07.2026 — Sichern
    einzelner Clips übernimmt die Save-Geste, M9).

    STRUKTURIERT als ValueTree↔XML in einer EIGENEN PropertiesFile
    (Conduit/Looper.settings, Muster ChannelNames — Skalar-Flut skaliert
    nicht über 4 Looper × 4 Tracks):

        LooperState (globale Menü-Optionen als Attribute)
          └── Looper[i]: sourceKey, spectrum, sendMaster, numTracks
                └── Track[t]: gain, pan, mute, solo, variQuantized,
                              send0..send3 (Level), sendPre

    Globale Menü-Optionen (LooperSettingsMenu, M6):
      launchQuant   — Start/Stop-Raster (app-weites Enum, Commit bleibt sofort)
      tapMode       — Tap auf spielenden Clip: Retrigger vs. Toggle-Stop
      halveMode     — ÷2: erste vs. aktuelle Hälfte
      reverseMode   — Reverse sofort (Spiegelung) vs. an der Loop-Grenze
      variRaster    — Rastmodus des VARI-Knobs: Halbtöne vs. Session-Skala
      variScope     — Frei/Gerastert-Schalter wirkt pro Track vs. pro Looper
      soloScope     — Solo pro Looper (Default) vs. global
      visibleSlots  — sichtbare Slot-Zeilen 4–12 (reine Anzeige-Größe,
                      das Datenmodell cappt Clips NICHT darauf)
      deleteLatch   — Delete-Kachel als Latch-Toggle (Nicht-Touch)
      autoAdvance   — Target rückt nach Commit auf den nächsten freien Slot

    Migration: beim ersten Lauf ohne gespeicherten Zustand übernimmt
    migrateFromLegacy() die alten TransportSettings-Schlüssel
    (looperSource → Looper 0, looperSpectrum) — die alten Keys bleiben
    dort liegen, werden aber nicht mehr geschrieben. looperAnchor
    (globaler Output) bleibt bewusst in den TransportSettings
    (User-Entscheidung: Output global, pro-Track-Outputs später übers
    LooperModule).

    Threading: alle Methoden Message Thread; UI-Benachrichtigung über
    juce::ChangeBroadcaster (async), der EngineProcessor spiegelt per
    applyLooperSettings() in Bank/Modell.
*/
class LooperSettings : public juce::ChangeBroadcaster
{
public:
    static constexpr int maxLoopers = 4;
    static constexpr int maxTracks  = 4;
    static constexpr int minVisibleSlots = 4;
    static constexpr int maxVisibleSlots = 12;
    static constexpr int defaultVisibleSlots = 8;

    enum class TapMode : int { retrigger = 0, toggleStop, legato };
    enum class ReverseMode : int { immediate = 0, boundary, quantized };
    enum class VariRaster : int { semitones = 0, sessionScale };
    enum class VariScope : int { perTrack = 0, perLooper, globalScope };
    enum class SoloScope : int { perLooper = 0, globalScope };

    /** Anzeige der VARI-Rastung (Panel-Option „VARI Display (Quant)"):
        Halbtöne („+3 st") oder Skalen-Stufen („♭3") — reine UI-Option. */
    enum class VariDisplay : int { semitones = 0, scaleDegrees };

    /** Eigene Datei neben Conduit.settings (Muster ChannelNames). */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit LooperSettings (const juce::PropertiesFile::Options& options = defaultOptions());
    ~LooperSettings() override;

    /** true, wenn die Datei bereits einen Looper-Zustand trug (steuert
        die Einmal-Migration der TransportSettings-Schlüssel). */
    [[nodiscard]] bool hasStoredState() const noexcept { return storedStateLoaded; }

    /** Einmal-Migration der Legacy-Schlüssel (nur wirksam, solange kein
        Zustand gespeichert wurde) — schreibt und broadcastet NICHT. */
    void migrateFromLegacy (const juce::String& legacyLooperSource,
                            bool legacySpectrumView);

    //==========================================================================
    // Globale Menü-Optionen

    [[nodiscard]] LaunchQuant getLaunchQuant() const noexcept { return launchQuant; }
    void setLaunchQuant (LaunchQuant quant);

    [[nodiscard]] TapMode getTapMode() const noexcept { return tapMode; }
    void setTapMode (TapMode mode);

    [[nodiscard]] looper::HalveMode getHalveMode() const noexcept { return halveMode; }
    void setHalveMode (looper::HalveMode mode);

    [[nodiscard]] ReverseMode getReverseMode() const noexcept { return reverseMode; }
    void setReverseMode (ReverseMode mode);

    [[nodiscard]] VariRaster getVariRaster() const noexcept { return variRaster; }
    void setVariRaster (VariRaster raster);

    [[nodiscard]] VariScope getVariScope() const noexcept { return variScope; }
    void setVariScope (VariScope scope);

    [[nodiscard]] SoloScope getSoloScope() const noexcept { return soloScope; }
    void setSoloScope (SoloScope scope);

    [[nodiscard]] int getVisibleSlots() const noexcept { return visibleSlots; }
    void setVisibleSlots (int slots);

    [[nodiscard]] bool isDeleteLatchEnabled() const noexcept { return deleteLatch; }
    void setDeleteLatchEnabled (bool enabled);

    [[nodiscard]] bool isAutoAdvanceEnabled() const noexcept { return autoAdvance; }
    void setAutoAdvanceEnabled (bool enabled);

    [[nodiscard]] VariDisplay getVariDisplay() const noexcept { return variDisplay; }
    void setVariDisplay (VariDisplay display);

    /** Sichtbare Send-Anzahl 0..4 (MIXER-Tab „+ Send / −") — reine
        UI-Option, die Engine rendert immer alle 4 Send-Busse (ADR 012). */
    [[nodiscard]] int getSendCount() const noexcept { return sendCount; }
    void setSendCount (int count);

    // DISPLAY-Optionen (UI-only): Mixer-Elemente ausblendbar —
    // Use-Case „erst MIDI-mappen, dann verstecken"
    [[nodiscard]] bool isShowStopAll() const noexcept { return showStopAll; }
    void setShowStopAll (bool show);

    [[nodiscard]] bool isHideMuteSolo() const noexcept { return hideMuteSolo; }
    void setHideMuteSolo (bool hide);

    [[nodiscard]] bool isHideMixerXy() const noexcept { return hideMixerXy; }
    void setHideMixerXy (bool hide);

    /** Collapse-Bitmaske der Seitenpanel-Sektionen (VIEW-Zustand,
        Muster id::outCollapsed — Bit gesetzt = Sektion zu). */
    [[nodiscard]] int getPanelCollapsedMask() const noexcept { return panelCollapsed; }
    void setPanelCollapsedMask (int mask);

    [[nodiscard]] int getNumLoopers() const noexcept { return numLoopers; }
    void setNumLoopers (int count);

    //==========================================================================
    // Pro Looper / Track

    [[nodiscard]] juce::String getSourceKey (int looperIndex) const noexcept;
    void setSourceKey (int looperIndex, const juce::String& key);

    [[nodiscard]] bool isSpectrumView (int looperIndex) const noexcept;
    void setSpectrumView (int looperIndex, bool spectrum);

    /** „An Master senden" (Looper-I/O 07/2026, Default true): Looper in der
        Master-Summe des Anker-Paars — unabhängig von Looper-Out-Abgriffen
        (die laufen parallel). Der EngineProcessor spiegelt in die Bank. */
    [[nodiscard]] bool isSendToMaster (int looperIndex) const noexcept;
    void setSendToMaster (int looperIndex, bool enabled);

    [[nodiscard]] int getNumTracks (int looperIndex) const noexcept;
    void setNumTracks (int looperIndex, int count);

    [[nodiscard]] float getTrackGain (int looperIndex, int trackIndex) const noexcept;
    void setTrackGain (int looperIndex, int trackIndex, float gain);

    [[nodiscard]] float getTrackPan (int looperIndex, int trackIndex) const noexcept;
    void setTrackPan (int looperIndex, int trackIndex, float pan);

    [[nodiscard]] bool isTrackMuted (int looperIndex, int trackIndex) const noexcept;
    void setTrackMuted (int looperIndex, int trackIndex, bool muted);

    [[nodiscard]] bool isTrackSolo (int looperIndex, int trackIndex) const noexcept;
    void setTrackSolo (int looperIndex, int trackIndex, bool solo);

    /** VARI-Rastung des Frei/Gerastert-Schalters (Scope siehe variScope). */
    [[nodiscard]] bool isTrackVariQuantized (int looperIndex, int trackIndex) const noexcept;
    void setTrackVariQuantized (int looperIndex, int trackIndex, bool quantized);

    /** Send-LEVEL des Tracks (Mixer 07/2026): 0..1 pro Send-Bus 1..4 —
        ersetzt die frühere An/Aus-Bitmaske (Alt-Dateien migrieren beim
        Laden Bit→1.0, exakte Verhaltens-Parität). */
    [[nodiscard]] float getTrackSendLevel (int looperIndex, int trackIndex,
                                           int sendIndex) const noexcept;
    void setTrackSendLevel (int looperIndex, int trackIndex, int sendIndex,
                            float level01);

    /** ABGELEITETE Send-Bitmaske (Bit gesetzt = Level > 0) — Kompatibilität
        für Farb-Resolver/Hash/Send-Dialog; Setter mappt Bit→1.0 / 0.0. */
    [[nodiscard]] int getTrackSends (int looperIndex, int trackIndex) const noexcept;
    void setTrackSends (int looperIndex, int trackIndex, int mask);

    /** Send-Abgriff des Tracks: true = pre (vor Gain/Pan/Mute), Default post. */
    [[nodiscard]] bool isTrackSendPre (int looperIndex, int trackIndex) const noexcept;
    void setTrackSendPre (int looperIndex, int trackIndex, bool pre);

    /** Distanz des Tracks (XY-Panner-Y, 0..1 — „FAR" = 1). */
    [[nodiscard]] float getTrackDistance (int looperIndex, int trackIndex) const noexcept;
    void setTrackDistance (int looperIndex, int trackIndex, float distance01);

    //==========================================================================
    // Distanz-Globals (MIXER-Tab · DISTANCE · GLOBAL — gelten für alle
    // Looper-Tracks; Semantik siehe Looper/LooperDistance.h)

    struct DistanceState
    {
        float hiDumpDb   = 9.0f;      // Shelf-Absenkung bei d=1 (0..18 dB)
        float hiCutHz    = 8000.0f;   // Tiefpass-Ziel bei d=1 (500..16000 Hz)
        float baseFreqHz = 2000.0f;   // Shelf-Eckfrequenz (200..4000 Hz)
        float width01    = 0.5f;      // verbleibende Breite bei d=1 (0..1)
        bool  volDumpOn  = true;      // Pegel-Kurve aktiv (Default AN, 20.07.2026)
        float volDumpDb  = 12.0f;     // dB-Neigung der Vol-Kurve (0..24)
        float smoothMs   = 20.0f;     // Distanz-Slew (0..500 ms)
        float ySens      = 1.0f;      // Y Sensitivity: Wirkung des Pad-Wegs (0..1)
    };

    [[nodiscard]] DistanceState getDistance() const noexcept { return distanceState; }
    void setDistance (const DistanceState& state);

    /** Y-Link: GENAU EIN Send (0..3) folgt der Distanz-Y-Achse — Puck
        nach oben = mehr Send (Poti-Level wirkt als Untergrenze);
        −1 = kein Send verlinkt (Default). */
    [[nodiscard]] int getYLinkSend() const noexcept { return yLinkSend; }
    void setYLinkSend (int sendIndex);

    //==========================================================================
    /** [Message Thread] Ausstehende Änderungen sofort auf Platte schreiben. */
    void flush();

private:
    struct TrackState
    {
        float gain = 1.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
        bool variQuantized = false;   // Default frei (Drift ist Feature)
        std::array<float, 4> sendLevel {};   // Send-Level 1..4 (0..1, Default 0)
        bool sendPre = false;         // Abgriff pre statt post (Default post)
        float distance = 0.0f;        // XY-Panner-Y (0..1, „FAR" = 1)
    };

    struct LooperState
    {
        juce::String sourceKey;       // leer = keine Quelle
        bool spectrum = false;
        bool sendMaster = true;       // „an Master senden" (Looper-I/O)
        int numTracks = 1;
        std::array<TrackState, static_cast<std::size_t> (maxTracks)> tracks;
    };

    [[nodiscard]] bool validLooper (int l) const noexcept
    {
        return l >= 0 && l < maxLoopers;
    }
    [[nodiscard]] bool validTrack (int l, int t) const noexcept
    {
        return validLooper (l) && t >= 0 && t < maxTracks;
    }

    void loadFromFile();
    void writeAndNotify();

    juce::ApplicationProperties applicationProperties;

    LaunchQuant launchQuant = LaunchQuant::bar1;
    TapMode tapMode = TapMode::retrigger;
    looper::HalveMode halveMode = looper::HalveMode::firstHalf;
    ReverseMode reverseMode = ReverseMode::immediate;
    VariRaster variRaster = VariRaster::semitones;
    VariScope variScope = VariScope::perTrack;
    SoloScope soloScope = SoloScope::perLooper;
    int visibleSlots = defaultVisibleSlots;
    bool deleteLatch = false;
    bool autoAdvance = true;
    int numLoopers = 1;
    VariDisplay variDisplay = VariDisplay::semitones;
    int sendCount = 4;
    bool showStopAll = true;
    bool hideMuteSolo = false;
    bool hideMixerXy = false;
    int panelCollapsed = 0;

    std::array<LooperState, static_cast<std::size_t> (maxLoopers)> loopers;
    DistanceState distanceState;
    int yLinkSend = -1;

    bool storedStateLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperSettings)
};

} // namespace conduit
