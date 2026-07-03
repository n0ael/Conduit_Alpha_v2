#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "ChassisSchema.h"
#include "ConduitModule.h"
#include "Core/Capture/LevelMeter.h"
#include "Core/LinkSendTaps.h"
#include "Interfaces/IClockSlave.h"
#include "Interfaces/ILinkAudioClient.h"

namespace conduit
{

//==============================================================================
/** Beschreibung eines DSP-Parameters fürs FX-Chassis (CLAUDE.md 4.6):
    die Subklasse liefert ihre Parameter als Liste an den ProcessorModule-
    Konstruktor, das Chassis übernimmt Schema, Echtzeit-Ziele, CV-Eingänge
    und Attenuverter. */
struct ChassisParamDesc
{
    juce::String id;
    float defaultValue = 0.0f;
    float hardMin = 0.0f;   // DSP-Grenzen — der Audio-Pfad clamped IMMER hierauf
    float hardMax = 1.0f;
};

//==============================================================================
/**
    FX-Chassis: Basisklasse aller Processor-Module (CLAUDE.md 4.1/4.6) —
    Gate, EQ, Compressor, Airwindows-Wrapper, PluginModule (CLAP, v2.x).

    Jedes FX-Modul erbt hiermit AUTOMATISCH den verbindlichen Standard:
      - Input-/Output-Gain (input_gain/output_gain, −60..+6 dB, role
        "chassis") mit 5-ms-SmoothedValue (Muster AttenuatorModule).
      - 2×2-Kanal-LevelMeter (post-Input-Gain / post-Output-Gain); die UI
        liest transient über GraphManager::getModuleFor — nie Pointer cachen.
      - Link-Audio-Send-Tap am Ausgang (LinkSendTaps, Kanal-Name = moduleId,
        Node-Property linkSendEnabled = Patch-Zustand).
      - Pro DSP-Parameter einen CV-Eingang: Kanal-Layout FEST Audio 0..1,
        CV 2..N (Kanal von Parameter i = numAudioIns + i). CV blockkonstant
        als Blockmittel, Attenuverter {param}_cv_amt bipolar −1..+1:
        effective = clamp(base + cv·amt·(hardMax−hardMin), hardMin, hardMax).

    Subklassen implementieren NUR prepareCore()/processCore() (reine
    Audio-Sicht, Kanäle 0..1) und lesen ihre Parameter über
    effectiveParam(i) — NIE prepareToPlay/processBlock/appendParametersTo/
    getParameterTarget überschreiben (final).

    Signal-Reihenfolge in processBlock (Audio Thread, lock-/alloc-frei):
      noteBlockBegin → CV lesen → In-Gain → In-Meter → processCore →
      Out-Gain → Out-Meter → Link-Tap-commit.

    Thread-Ownership:
      - setLinkAudioContext/moduleIdRenamed/releaseSessionResources/
        setClockBus/setSendEnabled → Message Thread (GraphManager injiziert
        vor der Graph-Aufnahme, 5.2 Schritt 1)
      - getParameterTarget()-Ziele werden von Fremd-Threads beschrieben
        (Dual-State 6.1), processBlock liest relaxed
      - Meter/Status-Getter → beliebiger Thread (Atomics)
*/
class ProcessorModule : public ConduitModule,
                        public ILinkAudioClient,
                        public IClockSlave
{
public:
    static constexpr int maxDspParameters   = 16;  // deckt AirwindowsPlugin::maxParameters
    static constexpr int chassisStateVersion = ChassisSchema::stateVersion;

    explicit ProcessorModule (std::vector<ChassisParamDesc> dspParameterDescs,
                              int numAudioInsToUse = 2, int numAudioOutsToUse = 2);
    ~ProcessorModule() override;

    [[nodiscard]] ModuleType getType() const override { return ModuleType::processor; }

    /** Basis-Skelett + linkSendEnabled-Default (Patch-Zustand). */
    [[nodiscard]] juce::ValueTree createState() override;

    /** input_gain/output_gain → dB-Atomics, {param} → Basiswert,
        {param}_cv_amt → Attenuverter. */
    [[nodiscard]] std::atomic<float>* getParameterTarget (const juce::String& parameterId) noexcept final;

    //==========================================================================
    // AudioProcessor — final: Subklassen implementieren prepareCore/processCore
    bool isBusesLayoutSupported (const BusesLayout& layouts) const final;
    void prepareToPlay (double sampleRate, int samplesPerBlock) final;
    void releaseResources() final;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) final;

    //==========================================================================
    // ILinkAudioClient [Message Thread]
    void setLinkAudioContext (LinkClock* clock, const juce::String& initialModuleId) override;
    void moduleIdRenamed (const juce::String& newModuleId) override;
    void releaseSessionResources() override;   // Phase 1 (5.3): Tap sofort zurückziehen

    // IClockSlave [Message Thread, vor Graph-Aufnahme]
    void setClockBus (const ClockBus* bus) noexcept override;

    //==========================================================================
    // Link-Send-Tap [Message Thread] — Weiterleitung der Node-Property
    // linkSendEnabled durch den GraphManager
    void setSendEnabled (bool shouldSend);
    [[nodiscard]] bool isSendEnabled() const noexcept { return sendWanted; }

    /** Beliebiger Thread (atomic): offline / announced / streaming. */
    [[nodiscard]] LinkSendTaps::Status getLinkSendStatus() const noexcept;

    // Diagnose/Tests (Muster LinkAudioSendModule)
    [[nodiscard]] bool isSinkRetirePending() const noexcept;
    void flushPendingSinkRetirement();

    //==========================================================================
    // Meter [beliebiger Thread] — UI löst pro Tick transient über
    // GraphManager::getModuleFor auf (Zombie-UI-Regel 5.3)
    [[nodiscard]] const LevelMeter& getInputMeter() const noexcept  { return inputMeter; }
    [[nodiscard]] const LevelMeter& getOutputMeter() const noexcept { return outputMeter; }

    //==========================================================================
    [[nodiscard]] int getNumDspParameters() const noexcept
    {
        return static_cast<int> (dspParams.size());
    }

    /** CV-Eingangs-Kanal des DSP-Parameters i (FESTES Layout, 4.6). */
    [[nodiscard]] int getCvChannelFor (int dspIndex) const noexcept
    {
        return numAudioIns + dspIndex;
    }

protected:
    //==========================================================================
    // Chassis-Hooks — die gesamte Modul-DSP lebt hier
    virtual void prepareCore (double sampleRate, int maximumBlockSize) = 0;
    virtual void processCore (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midiMessages) = 0;
    virtual void releaseCore() {}

    /** [Audio Thread, nur in processCore] Blockkonstanter Effektivwert des
        DSP-Parameters i: Basiswert + CV·Attenuverter, hard-geclamped. */
    [[nodiscard]] float effectiveParam (int dspIndex) const noexcept
    {
        return effective[static_cast<size_t> (dspIndex)];
    }

    /** chassis-Gains + DSP-Parameter + Attenuverter, jeweils mit role. */
    void appendParametersTo (juce::ValueTree& parameters) final;

private:
    [[nodiscard]] static BusesProperties makeChassisBuses (int numAudioIns, int numAudioOuts,
                                                           int numDspParams);

    void createTapIfWanted();

    //==========================================================================
    // Fix seit dem Konstruktor
    const std::vector<ChassisParamDesc> dspParams;
    const int numAudioIns;
    const int numAudioOuts;

    // Dual-State-Ziele (6.1) — Adressen stabil über die Modul-Lebensdauer
    std::array<std::atomic<float>, maxDspParameters> dspBase {};
    std::array<std::atomic<float>, maxDspParameters> cvAmount {};
    std::atomic<float> inputGainDb  { static_cast<float> (ChassisSchema::gainDefaultDb) };
    std::atomic<float> outputGainDb { static_cast<float> (ChassisSchema::gainDefaultDb) };

    // Nur Audio Thread
    std::array<float, maxDspParameters> effective {};
    juce::SmoothedValue<float> smoothedInputGain  { 1.0f };
    juce::SmoothedValue<float> smoothedOutputGain { 1.0f };
    juce::AudioBuffer<float> audioView;                 // Alias auf Kanäle 0..1, alloc-frei
    std::array<float*, 8> audioViewPointers {};

    // Meter (eigene Instanzen — Atomics sind billig, 4.6)
    LevelMeter inputMeter;
    LevelMeter outputMeter;

    // Link-Send [Message Thread besitzt, Audio Thread liest rtTap]
    LinkSendTaps taps;
    std::atomic<LinkSendTaps::Tap*> rtTap { nullptr };
    juce::String currentModuleId;
    bool sendWanted = false;          // Message Thread
    int preparedBlockSize = 0;        // Message Thread schreibt (Audio steht), Audio liest

    // Takt für die Tap-Commits; Injektion vor Graph-Aufnahme (4.2)
    const ClockBus* clockBus = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorModule)
};

} // namespace conduit
