#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "Interfaces/IClockSlave.h"
#include "Interfaces/IStochastic.h"
#include "GeneratorModule.h"

namespace conduit
{

//==============================================================================
/**
    Urzwerg-inspirierter Step-Sequencer: 4 Reihen × 16 Steps (CLAUDE.md 4.3).

    Modi (Reihen-Verkettung wie beim MFB Urzwerg Pro, nur größer):
      mode 0 — 4×16: vier parallele Sequenzen, gemeinsame Position
      mode 1 — 2×32: Ketten A+B und C+D
      mode 2 — 1×64: eine Kette über alle Reihen

    Output-Bus: 8 Kanäle, final (Patch-/Preset-relevant!):
      Row A: ch0 CV, ch1 Gate · B: 2/3 · C: 4/5 · D: 6/7.
      Verkettete Modi nutzen die Outs der Kettenanfangs-Reihe (A bzw. C),
      ungenutzte Kanäle liefern Stille. Accent/Velocity macht man
      Urzwerg-style mit einer freien Reihe.

    Parameter (Namen sind API — OSC z.B. /conduit/generator/sequencer_1/a3):
      rate (Steps/Beat), gate (Länge), swing (0–0.75), direction
      (0 fwd, 1 rev, 2 pendulum, 3 random), mode, length (Steps je Reihe,
      verkettete Modi multiplizieren), prob (Trigger-Chance), quantize
      (0 aus, 1 globale Session-Skala aus dem ClockState),
      Steps a1..a16, b1..b16, c1..c16, d1..d16 (CV 0–1).

    Beat-gelockt: Position ist eine reine Funktion des Session-Beats —
    phasenstarr zu allen Slaves/Link-Peers; Swing verzerrt die Zeitachse
    paarweise (ungerade Steps starten später). Ohne ClockBus: Freilauf,
    rate als Steps/Sekunde.

    Thread-Ownership:
      - setClockBus()/setRandomSeed() → Message Thread, vor Graph-Aufnahme
      - processBlock()               → Audio Thread, lock-free, allocation-free
      - getCurrentCell()             → beliebiger Thread (atomic, für die UI)
*/
class StepSequencerModule final : public GeneratorModule,
                                  public IClockSlave,
                                  public IStochastic
{
public:
    StepSequencerModule();

    static constexpr const char* staticModuleId = "sequencer";
    static constexpr int numRows = 4;
    static constexpr int stepsPerRow = 16;
    static constexpr int numSteps = numRows * stepsPerRow;

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    //==========================================================================
    // IClockSlave / IStochastic
    void setClockBus (const ClockBus* bus) noexcept override;
    void setRandomSeed (std::uint64_t seed) noexcept override;

    //==========================================================================
    [[nodiscard]] std::atomic<float>* getParameterTarget (const juce::String& parameterId) noexcept override;

    /** Aktive Zelle (row·16+col) der ersten Kette — fürs UI-Playhead (30 fps).
        Im 4×16-Modus teilen alle Reihen dieselbe Spalte. */
    [[nodiscard]] int getCurrentCell() const noexcept
    {
        return currentCellForUi.load (std::memory_order_relaxed);
    }

    /** Step-Parameter-Name für eine Zelle: row 0–3, col 0–15 → "a1".."d16". */
    [[nodiscard]] static juce::String stepParameterId (int row, int column);

    //==========================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

protected:
    void appendParametersTo (juce::ValueTree& parameters) override;

private:
    struct ChainState   // nur Audio Thread
    {
        std::int64_t lastGlobalStep = -1;
        bool triggerArmed = true;
        int randomIndex = 0;
    };

    void renderChain (juce::AudioBuffer<float>& buffer, int chainIndex, int numChains,
                      double blockStartPosition, double positionPerSample,
                      int chainLength, int direction, float gateLength, float probability,
                      bool quantizeToScale, int scaleRootNote, int scaleTypeIndex,
                      double swing);

    const ClockBus* clockBus = nullptr;

    // Parameter-Targets (Dual-State 6.1)
    std::atomic<float> rateTarget      { 2.0f };
    std::atomic<float> gateTarget      { 0.5f };
    std::atomic<float> swingTarget     { 0.0f };
    std::atomic<float> directionTarget { 0.0f };
    std::atomic<float> modeTarget      { 0.0f };
    std::atomic<float> lengthTarget    { 16.0f };
    std::atomic<float> probTarget      { 1.0f };
    std::atomic<float> quantizeTarget  { 0.0f };
    std::array<std::atomic<float>, numSteps> stepTargets {};

    // Nur Audio Thread
    std::array<ChainState, numRows> chainStates {};
    juce::Random random;
    double freeRunPosition = 0.0;
    double currentSampleRate = 48000.0;

    std::uint64_t seedValue = 1;        // Message Thread, vor Graph-Aufnahme
    std::atomic<int> currentCellForUi { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencerModule)
};

} // namespace conduit
