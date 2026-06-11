#pragma once

#include <atomic>

#include "Interfaces/IClockSlave.h"
#include "GeneratorModule.h"

namespace conduit
{

//==============================================================================
/**
    Beat-synchroner Sinus-LFO — erster getakteter Generator (CLAUDE.md 4.3).

    Mit ClockBus (Normalfall, injiziert vom GraphManager): die Phase ist eine
    reine Funktion des Session-Beats — alle LFOs im Patch und alle Link-Peers
    sind dadurch automatisch phasenstarr. Eine rate-Änderung springt deshalb
    in der Phase (Re-Sync statt Drift — Hardware-LFO-Verhalten).

    Ohne ClockBus (Tests): freilaufend, rate wird als Hz interpretiert,
    Phase akkumuliert klick-frei über Blockgrenzen.

    Parameter (Dual-State 6.1, Targets via getParameterTarget):
      - rate:  Zyklen pro Beat, 0.0625–4, default 0.25 (= 1 Zyklus pro Takt)
      - depth: Amplitude 0–1, default 1 (geglättet gegen Zipper)

    Thread-Ownership:
      - setClockBus()   → Message Thread, vor Graph-Aufnahme (IClockSlave)
      - processBlock()  → Audio Thread, lock-free, allocation-free
*/
class LfoModule final : public GeneratorModule,
                        public IClockSlave
{
public:
    LfoModule();

    static constexpr const char* staticModuleId = "lfo";

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    //==========================================================================
    // IClockSlave
    void setClockBus (const ClockBus* bus) noexcept override;

    [[nodiscard]] bool isClockSynced() const noexcept { return clockBus != nullptr; }

    //==========================================================================
    [[nodiscard]] std::atomic<float>* getParameterTarget (const juce::String& parameterId) noexcept override;

    //==========================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

protected:
    void appendParametersTo (juce::ValueTree& parameters) override;

private:
    const ClockBus* clockBus = nullptr;  // gesetzt vor Graph-Aufnahme, dann stabil

    std::atomic<float> rateTarget  { 0.25f };
    std::atomic<float> depthTarget { 1.0f };

    // Nur Audio Thread
    juce::SmoothedValue<float> smoothedDepth { 1.0f };
    double freeRunPhase = 0.0;
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoModule)
};

} // namespace conduit
