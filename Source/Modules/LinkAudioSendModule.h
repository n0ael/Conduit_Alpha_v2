#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Core/LinkClock.h"
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

    Sink-Lifecycle [Message Thread]:
      - applySendConfig injiziert die Eingangs-Struktur + Bus-Layout; die Sinks
        entstehen in prepareToPlay (Größe in SAMPLES: samplesPerBlock × Breite).
      - moduleIdRenamed → alle Sink-Namen folgen live (Präfix-Wechsel).
      - Delete Phase 1 (releaseSessionResources, 5.3): alle rtSinks sofort per
        Atomic vom Audio-Thread getrennt, Refcount freigegeben; die Destruktion
        aller Sinks folgt nach EINEM gemeinsamen Epoch-Handshake.

    Epoch-Handshake (Sink-Teardown vs. processBlock): identisch zum bisherigen
    Einzel-Sink-Muster, nur über alle Sinks gebündelt — processBlock
    inkrementiert blocksProcessed (seq_cst) vor dem rtSink-Load; Phase 1 stored
    nullptr und liest die Epoche; die Destruktion wartet per AsyncUpdater-
    Self-Re-Dispatch, bis ein neuer Block den nullptr gesehen hat (100-ms-
    Deadline für gestopptes Audio).

    Audio-Pfad [Audio Thread, 3.1]: allocation-/lock-frei; pro Slot Gain
    (SmoothedValue) → TPDF-Dither (inline LCG) in vorallokierte Member-Buffer →
    commit über Sink::commitFromClockState mit dem ClockState des Blocks.
*/
class LinkAudioSendModule final : public NetworkIOModule,
                                  public ILinkAudioClient,
                                  public ISendConfigClient,
                                  public IClockSlave,
                                  private juce::AsyncUpdater
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
    /** Float → Int16 mit TPDF-Dither (CLAUDE.md 3.1/7.2): LCG-basiert,
        deterministisch pro Seed, ±1 LSB Dreieck um 0. Static + pure für die
        Dither-Statistik-Tests (13.4). */
    static void convertToInt16Tpdf (const float* const* channelData,
                                    int numChannels, int numFrames,
                                    std::int16_t* dest,
                                    std::uint32_t& ditherState) noexcept;

private:
    void handleAsyncUpdate() override;
    void disableAudioOnce();
    void updateAggregateStatus() noexcept;

    //==========================================================================
    /** Ein Eingang: Kanal-Bereich am Bus, eigener Sink, Attenuator, Dither. */
    struct InputSlot
    {
        juce::String inputId;
        juce::String effectiveName;   // ohne Präfix
        int offset = 0;               // Start-Kanal im Bus
        int width  = 1;               // 1 mono / 2 stereo
        juce::String gainParamId;

        std::unique_ptr<LinkClock::Sink> sink;             // Message Thread owned
        std::atomic<LinkClock::Sink*>    rtSink { nullptr }; // Audio Thread
        std::atomic<float>               gainTarget { 1.0f }; // Ziel (Dual-State)
        juce::SmoothedValue<float>       smoothedGain { 1.0f };
        std::uint32_t                    ditherState = 0x6c078965u;
        std::atomic<int>                 status { static_cast<int> (SendStatus::offline) };
    };

    [[nodiscard]] juce::String sinkNameFor (const juce::String& effectiveName) const;

    //==========================================================================
    // Message Thread
    juce::WeakReference<LinkClock> linkClock;
    juce::String currentModuleId;
    std::vector<SendInputConfig> pendingConfig;   // aus applySendConfig, für prepareToPlay
    bool audioEnabled = false;

    // Slots leben über die Modul-Lebensdauer (in prepareToPlay aufgebaut,
    // nie im Audio-Callback realloziert). unique_ptr, weil InputSlot atomics/
    // SmoothedValue hält (nicht kopier-/verschiebbar) und der Vektor stabile
    // Adressen für gainTarget/rtSink braucht.
    std::vector<std::unique_ptr<InputSlot>> slots;

    // Retire-Handshake (module-weit über alle Sinks)
    std::vector<std::unique_ptr<LinkClock::Sink>> retiredSinks;
    std::uint64_t retireEpoch = 0;
    std::uint32_t retireDeadlineMs = 0;
    static constexpr std::uint32_t retireGraceMs = 100;

    // Audio Thread liest; Injektion vor Graph-Aufnahme (4.2)
    const ClockBus* clockBus = nullptr;

    // Handshake-Paar (seq_cst)
    std::atomic<std::uint64_t> blocksProcessed { 0 };

    // Nur Audio Thread — vorallokiert in prepareToPlay
    std::vector<float>        scratchLeft;     // Gain-Scratch, max width × block
    std::vector<float>        scratchRight;
    std::vector<std::int16_t> interleavedBuffer;

    std::atomic<int> aggregateStatus { static_cast<int> (SendStatus::offline) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkAudioSendModule)
};

} // namespace conduit
