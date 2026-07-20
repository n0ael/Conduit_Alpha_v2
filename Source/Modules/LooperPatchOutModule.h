#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Interfaces/ILooperAudioClient.h"
#include "IOModule.h"

namespace conduit
{

//==============================================================================
/**
    Looper patch OUT (ADR 012 „Big Looper Out", umbenannt ADR 013):
    das Standard-Ausgangsmodul des Loopers —
    seine Ausgänge folgen AUTOMATISCH der Looper-Struktur (numLoopers/
    numTracks aus den LooperSettings), keine manuelle Slot-Pflege.
    Reihenfolge der Stereo-Slots (alle fix 2 Kanäle, User-Spezifikation
    19.07.2026):

        Track-Outs (geflattet: Looper 1 Track 1..n₁, Looper 2 ...)
        Bus-Outs   (Post-Fader-Bus Looper 1..numLoopers)
        Send-Outs  (Send 1–4, IMMER alle 4 — stabile Kanal-Indizes)
        Master     (Master-Mix, respektiert „an Master senden")

    Track-Outs sind POST-Fader (Gain/Pan/Mute) — Mono-Clips kommen dank
    der Panning-Sektion stereo heraus. Pre-Abgriffe und Mono-Modi des
    früheren kompakten looper_out sind mit ADR 013 ersatzlos entfallen
    (Sends bieten Pre/Post pro Track).

    Struktur-Änderungen schreibt der GraphManager als frische <Outputs>-
    Liste (syncLooperPatchOutConfigs, undo-frei — Struktur ist App-Zustand);
    die numOutputChannels-Änderung re-materialisiert den Node gefadet,
    Kabel auf überlebende Slots werden umgeschrieben (Spec-Identität).

    Audio-Pfad [Audio Thread, 3.1]: allocation-/lock-frei — kopiert nur
    die im selben Callback gerenderten Busse (LooperBank::renderBlock).
*/
class LooperPatchOutModule final : public IOModule,
                                   public ILooperAudioClient
{
public:
    LooperPatchOutModule();

    static constexpr const char* staticModuleId = "looper_patch_out";
    static constexpr int slotWidth = 2;   // alle Slots stereo

    enum class Kind : int { track = 0, bus, send, master };
    static constexpr const char* kindTrack  = "track";
    static constexpr const char* kindBus    = "bus";
    static constexpr const char* kindSend   = "send";
    static constexpr const char* kindMaster = "master";

    /** Ein Stereo-Slot. looper/track/send sind 1-basiert (wie die UI). */
    struct OutputSpec
    {
        Kind kind = Kind::master;
        int looper = 0;   // bei track/bus
        int track = 0;    // bei track
        int send = 0;     // bei send

        bool operator== (const OutputSpec&) const = default;
    };

    /** Logische Looper-Struktur (Quelle: LooperSettings). */
    struct Structure
    {
        int numLoopers = 1;
        std::array<int, 4> numTracks { 1, 1, 1, 1 };

        bool operator== (const Structure&) const = default;
    };

    /** Papierkorb-Referenz eines entfernten Kabels: SPEC-relativ (nie
        roher Kanal) — beim Restore wird der Kanal aus der dann gültigen
        Slot-Liste neu berechnet (channelOffsetOf + lr). */
    struct PatchOutCableRef
    {
        juce::String patchOutUuid;
        OutputSpec spec;
        int lr = 0;               // 0 = links, 1 = rechts
        juce::String destNodeId;
        int destChannel = 0;
    };

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    /** Minimal-Default (1 Looper / 1 Track) — der GraphManager synct
        direkt nach dem Anlegen auf die echte Struktur. */
    [[nodiscard]] juce::ValueTree createState() override;

    //==========================================================================
    // Schema-Helfer (static, pure) — UI und GraphManager nutzen sie

    /** Slot-Liste aus der Struktur (Reihenfolge siehe Klassendoku). */
    [[nodiscard]] static std::vector<OutputSpec> buildSpecs (const Structure& structure);

    /** Schreibt die <Outputs>-Liste + numInput-/numOutputChannels frisch
        in den Node-Tree. Leere Liste → Master. */
    static void applyOutputConfig (juce::ValueTree nodeTree,
                                   const std::vector<OutputSpec>& specs,
                                   juce::UndoManager* undo = nullptr);

    /** Leitet die Slots aus dem Node-Tree ab (Materialisierung). */
    [[nodiscard]] static std::vector<OutputSpec> readOutputConfig (const juce::ValueTree& nodeTree);

    /** Kanal-Offset (linker Kanal) des Slots in der Liste — −1 = fehlt. */
    [[nodiscard]] static int channelOffsetOf (const std::vector<OutputSpec>& specs,
                                              const OutputSpec& spec) noexcept;

    /** UI-Label, z. B. "Looper 2 · Track 3", "Looper 1 · Bus",
        "Send 2", "Master". */
    [[nodiscard]] static juce::String outputLabel (const OutputSpec& spec);

    /** Stabile Meter-Kanal-Zuordnung im 4er-Raster — UNABHÄNGIG von der
        aktiven Struktur (Layout des looperOutLevels-Meters und der
        globalen Track-Nummerierung, User-Skizze 19.07.2026):
        Tracks ((l−1)·4 + t−1)·2 → 0..31, Busse 32..39, Sends 40..47,
        Master 48/49. Liefert den LINKEN Kanal; −1 bei ungültiger Spec. */
    [[nodiscard]] static int meterChannelOf (const OutputSpec& spec) noexcept;
    static constexpr int meterChannelCount = 50;

    /** Globale Track-Nummer im 4er-Raster (1-basiert): Looper 2 beginnt
        immer bei Track 5, Looper 4 bei Track 13 — Lücken erlaubt.
        Seit 07/2026 nutzen auch Looper-Page-Strips, Statuszeile und
        MIDI-Ziel-Namen diese Zählung. */
    [[nodiscard]] static constexpr int globalTrackNumber (int looper1Based,
                                                          int track1Based) noexcept
    {
        return (looper1Based - 1) * 4 + track1Based;
    }

    [[nodiscard]] static int globalTrackNumber (const OutputSpec& spec) noexcept
    {
        return globalTrackNumber (spec.looper, spec.track);
    }

    [[nodiscard]] static Kind kindFromString (const juce::String& text) noexcept;
    [[nodiscard]] static juce::String toString (Kind kind);

    //==========================================================================
    // ILooperAudioClient [Message Thread, vor prepareForGraph]
    void setLooperAudioSource (LooperBank* bank) override;

    /** [Message Thread, vor prepareForGraph] Slots aus dem Tree — setzt
        das Ausgangs-Bus-Layout (2 Kanäle pro Slot). */
    void applyOutputSpecs (const std::vector<OutputSpec>& specs);

    //==========================================================================
    // AudioProcessor
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==========================================================================
    // Message Thread — Diagnose/Tests
    [[nodiscard]] int getNumSlots() const noexcept { return (int) specs.size(); }

private:
    // Message Thread (fix ab Materialisierung, Audio liest die Kopie)
    std::vector<OutputSpec> specs;
    int totalChannels = 2;

    // Audio Thread liest; Injektion auf dem MT vor der Graph-Aufnahme.
    // Die Bank überlebt den Graph (EngineProcessor-Deklarationsreihenfolge).
    std::atomic<LooperBank*> rtBank { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPatchOutModule)
};

} // namespace conduit
