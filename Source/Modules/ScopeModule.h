#pragma once

#include "AnalysisModule.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/** Ein Anzeige-Bin des Scopes: min/max über binSize Samples — korrekt für
    langsames CV (Linie) wie für Audio (Band), ohne Aliasing-Lügen. */
struct ScopeSample
{
    float minValue = 0.0f;
    float maxValue = 0.0f;
};

//==============================================================================
/**
    Oszilloskop für CV- und Audio-Signale (CLAUDE.md 4.1 / 10).

    Mono Pass-Through (1 in → 1 thru): hängt IN einer Kabelstrecke, das
    Signal läuft unverändert weiter. Der Audio Thread dezimiert auf
    min/max-Bins à 64 Samples (750 Bins/s @ 48 kHz — genug für mehrere
    Sekunden CV-Verlauf) und pusht sie in die lock-freie SpscQueue.

    Konsument der Queue ist GENAU EINE ScopeDisplay-Component (SPSC!),
    die mit 30 fps zieht. Ist keine UI da (Queue voll), verwirft push()
    die ältesten Verläufe nicht — neue Bins werden gedroppt; das Display
    holt nach dem Anhängen schnell wieder auf.

    Thread-Ownership:
      - processBlock()    → Audio Thread: lock-free, allocation-free
      - getScopeQueue()   → Referenz stabil; pop() NUR vom Message Thread
        (eine ScopeDisplay), push() NUR vom Audio Thread
*/
class ScopeModule final : public AnalysisModule
{
public:
    ScopeModule();

    static constexpr const char* staticModuleId = "scope";
    static constexpr int binSize = 64;

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    //==========================================================================
    [[nodiscard]] SpscQueue<ScopeSample>& getScopeQueue() noexcept { return scopeQueue; }

    //==========================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

private:
    // ~5,5 s Verlauf @ 48 kHz — Allokation einmalig im Konstruktor
    SpscQueue<ScopeSample> scopeQueue { 4096 };

    // Bin-Akkumulator über Blockgrenzen (Blöcke können < binSize sein) —
    // nur Audio Thread
    float binMin = 0.0f;
    float binMax = 0.0f;
    int binCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeModule)
};

} // namespace conduit
