#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Core/LinkClock.h"
#include "Core/LinkSendTaps.h"
#include "Interfaces/IClockSlave.h"
#include "Interfaces/ILinkAudioClient.h"
#include "Interfaces/ISendConfigClient.h"
#include "NetworkIOModule.h"

namespace conduit
{

//==============================================================================
/**
    Multi-Input Link Audio Send (CLAUDE.md 7.2): announced pro Eingang einen
    eigenen Audio-Kanal in der Link-Session und sendet dessen (attenuiertes)
    Signal als interleaved 16-bit-Audio mit TPDF-Dither. Reiner Sender —
    KEINE Ausgänge (Sink-Endpunkt wie audio_output); der Signalfluss zum
    eigentlichen Ziel läuft per Fan-out am Ausgang der Quelle.

    Jeder Eingang ist mono (1 Kanal) oder stereo (2 Kanäle), hat einen eigenen
    Attenuator (Gain 0..1) und einen eigenen Link-Kanal. Kanal-Name =
    {moduleId}/{effektiverEingangsName} — über mehrere Send-Nodes eindeutig.
    Die Eingangszahl ist FIX beim Anlegen (kein dynamischer Bus-Umbau): der
    GraphManager injiziert das Kanal-Layout via ISendConfigClient VOR
    prepareForGraph.

    Die Sink-Mechanik (rtSink-Atomics, TPDF-Dither, Epoch-Retire-Handshake,
    enableAudio-Refcount) lebt in Core/LinkSendTaps — pro Eingang ein Tap.
    Das Modul behält Schema, Attenuator und Namens-Präfix:

    Sink-Lifecycle [Message Thread]:
      - applySendConfig injiziert die Eingangs-Struktur + Bus-Layout; die Taps
        entstehen in prepareToPlay (Sink-Kapazität block × 2 SAMPLES).
      - moduleIdRenamed → alle Kanal-Namen folgen live (Präfix-Wechsel).
      - Delete Phase 1 (releaseSessionResources, 5.3): taps.retireAll() —
        Audio-Thread sofort getrennt, Refcount freigegeben, Sink-Destruktion
        nach dem Epoch-Handshake (Doku in LinkSendTaps).

    Audio-Pfad [Audio Thread, 3.1]: allocation-/lock-frei; noteBlockBegin
    einmal pro Callback, dann pro Slot Gain (SmoothedValue) in vorallokierte
    Scratch-Buffer → Tap::commit mit dem ClockState des Blocks.
*/
class LinkAudioSendModule final : public NetworkIOModule,
                                  public ILinkAudioClient,
                                  public ISendConfigClient,
                                  public IClockSlave
{
public:
    LinkAudioSendModule();
    ~LinkAudioSendModule() override;

    static constexpr const char* staticModuleId = "link_audio_send";

    // Kanalbreite eines Eingangs
    enum class InputMode { mono, stereo };
    static constexpr const char* modeMono   = "mono";
    static constexpr const char* modeStereo = "stereo";

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    /** Baut das Node-Skelett mit dem Default-Eingang (1 Stereo). Die konkrete
        Anlege-Konfiguration schreibt der GraphManager per applyInputConfig. */
    [[nodiscard]] juce::ValueTree createState() override;

    //==========================================================================
    // Schema-Helfer (static, pure) — vom GraphManager genutzt

    /** Schreibt die <Inputs>-Liste + in{n}_gain-Parameter + numInputChannels
        (=Σ Breiten) / numOutputChannels (=0) frisch in den Node-Tree.
        Bestehende <Inputs> und in*_gain-Parameter werden ersetzt. */
    static void applyInputConfig (juce::ValueTree nodeTree,
                                  const std::vector<InputMode>& modes);

    /** Leitet die injizierbare Kanal-Struktur aus dem Node-Tree ab
        (effektiver Name = userName ?: autoName ?: "input{n}"). */
    [[nodiscard]] static std::vector<SendInputConfig> readInputConfig (const juce::ValueTree& nodeTree);

    /** Effektiver Name eines Eingangs: userName ?: autoName ?: "input{index+1}".
        index ist die Position in <Inputs>. */
    [[nodiscard]] static juce::String effectiveInputName (const juce::ValueTree& inputTree, int index);

    /** Migration Alt→Neu (stateVersion < 2): fester Stereo-Send ohne <Inputs>
        → 1 Stereo-Eingang, autoName = alte moduleId, numOutputChannels = 0.
        Idempotent. */
    static void migrate (juce::ValueTree nodeTree);

    static constexpr int stateVersion = 2;

    //==========================================================================
    // ISendConfigClient [Message Thread]
    void applySendConfig (const std::vector<SendInputConfig>& inputs) override;
    void inputNameChanged (const juce::String& inputId, const juce::String& effectiveName) override;

    // ILinkAudioClient [Message Thread]
    void setLinkAudioContext (LinkClock* clock, const juce::String& initialModuleId) override;
    void moduleIdRenamed (const juce::String& newModuleId) override;
    void releaseSessionResources() override;

    // IClockSlave [Message Thread, vor Graph-Aufnahme]
    void setClockBus (const ClockBus* bus) noexcept override;

    //==========================================================================
    // AudioProcessor
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // Echtzeit-Parameter-Ziel (Dual-State 6.1): "in{n}_gain" → Slot-Gain
    [[nodiscard]] std::atomic<float>* getParameterTarget (const juce::String& parameterId) noexcept override;

    //==========================================================================
    enum class SendStatus : int { offline = 0, announced = 1, streaming = 2 };

    /** Aggregierter Modul-Status (beliebiger Thread, atomic). */
    [[nodiscard]] SendStatus getSendStatusForUi() const noexcept;

    /** Per-Slot-Status für die UI (beliebiger Thread). offline, wenn der Index
        außerhalb der aktuellen Slot-Zahl liegt. */
    [[nodiscard]] SendStatus getSlotStatusForUi (int slotIndex) const noexcept;

    [[nodiscard]] int getNumSlots() const noexcept;

    //==========================================================================
    // Message Thread — Diagnose/Tests
    [[nodiscard]] juce::StringArray getSinkNames() const;         // effektive Kanal-Namen (mit Präfix)
    [[nodiscard]] bool isSinkRetirePending() const noexcept;      // Phase 1 lief, Handshake offen
    void flushPendingSinkRetirement();                            // Handshake synchron abschließen

    //==========================================================================
    /** Float → Int16 mit TPDF-Dither — delegiert an LinkSendTaps (Signatur
        unverändert für die Dither-Statistik-Tests, 13.4). */
    static void convertToInt16Tpdf (const float* const* channelData,
                                    int numChannels, int numFrames,
                                    std::int16_t* dest,
                                    std::uint32_t& ditherState) noexcept;

private:
    void updateAggregateStatus() noexcept;

    //==========================================================================
    /** Ein Eingang: Kanal-Bereich am Bus, eigener Tap (Link-Kanal), Attenuator. */
    struct InputSlot
    {
        juce::String inputId;
        juce::String effectiveName;   // ohne Präfix
        int offset = 0;               // Start-Kanal im Bus
        int width  = 1;               // 1 mono / 2 stereo
        juce::String gainParamId;

        // Nicht-besitzend: Pool-Eintrag in taps, Adresse stabil bis zur
        // Modul-Destruktion (LinkSendTaps-Doku). nullptr ohne Link-Kontext.
        LinkSendTaps::Tap* tap = nullptr;

        std::atomic<float>         gainTarget { 1.0f };   // Ziel (Dual-State)
        juce::SmoothedValue<float> smoothedGain { 1.0f };
    };

    [[nodiscard]] juce::String sinkNameFor (const juce::String& effectiveName) const;

    //==========================================================================
    // Message Thread
    juce::String currentModuleId;
    std::vector<SendInputConfig> pendingConfig;   // aus applySendConfig, für prepareToPlay

    // Sink-Mechanik (rtSink-Atomics, Dither, Retire-Handshake, Refcount)
    LinkSendTaps taps;

    // Slots leben über die Modul-Lebensdauer (in prepareToPlay aufgebaut,
    // nie im Audio-Callback realloziert). unique_ptr, weil InputSlot atomics/
    // SmoothedValue hält (nicht kopier-/verschiebbar) und der Vektor stabile
    // Adressen für gainTarget braucht.
    std::vector<std::unique_ptr<InputSlot>> slots;

    // Audio Thread liest; Injektion vor Graph-Aufnahme (4.2)
    const ClockBus* clockBus = nullptr;

    // Nur Audio Thread — vorallokiert in prepareToPlay
    std::vector<float> scratchLeft;     // Gain-Scratch, block Samples
    std::vector<float> scratchRight;

    std::atomic<int> aggregateStatus { static_cast<int> (SendStatus::offline) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkAudioSendModule)
};

} // namespace conduit
