#pragma once

#include <atomic>

#include "UtilityModule.h"

namespace conduit
{

//==============================================================================
/**
    Attenuator: skaliert das Eingangssignal mit einem Gain-Faktor 0..1.

    Erstes konkretes Modul — bewusst minimal, dient als Referenz für den
    Modul-Lifecycle (Factory → createState → Async Prepare → Graph).

    Thread-Ownership:
      - setGain()      → Message/Netzwerk-Thread (std::atomic)
      - processBlock() → Audio Thread, lock-free, allocation-free;
                         SmoothedValue glättet Gain-Sprünge click-frei
*/
class AttenuatorModule final : public UtilityModule
{
public:
    AttenuatorModule();

    static constexpr const char* staticModuleId = "attenuator";

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    //==========================================================================
    /** Thread-safe; wird später vom OSC-Dual-State (6.1) bedient. */
    void setGain (float newGain) noexcept;

    //==========================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

protected:
    void appendParametersTo (juce::ValueTree& parameters) override;

private:
    std::atomic<float> gainTarget { 1.0f };
    juce::SmoothedValue<float> smoothedGain { 1.0f };  // nur Audio Thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttenuatorModule)
};

} // namespace conduit
