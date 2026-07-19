#pragma once

#include <atomic>
#include <vector>

#include "Interfaces/ILooperAudioClient.h"
#include "IOModule.h"

namespace conduit
{

//==============================================================================
/**
    Looper Audio Out (Looper-I/O 07/2026): macht die Looper-Busse der
    LooperBank im Node-Graph verfügbar. Dynamische Ausgangs-Slots — jeder
    Slot greift ein Ziel ab (Master-Mix oder Looper 1–4) in einem Modus
    (stereo = 2 Kanäle | sum = LR-Summe | left | right = je 1 Kanal),
    wahlweise Pre- oder Post-Fader (Gain/Pan/Mute/Solo der Track-Regler;
    User-Entscheidung 18.07.2026: pro Ausgang umschaltbar).

    Default-Bestückung: Master (stereo, post) + Looper 1–4 (stereo, post).
    Der Master-Mix respektiert die „an Master senden"-Flags pro Looper;
    die globale Anker-Ausgabe („Kein Master-Out" = Anker −1) ist davon
    unabhängig — der Abgriff hier läuft parallel.

    Slot-Struktur im Node-Tree (<Outputs>): Slots sind FIX pro
    Materialisierung — der GraphManager injiziert sie via readOutputConfig
    VOR prepareForGraph; eine Kanalzahl-Änderung re-materialisiert den
    Node im nächsten gefadeten Swap (Muster Looper In).

    Audio-Pfad [Audio Thread, 3.1]: allocation-/lock-frei — die Busse sind
    im selben Callback VOR dem Graph gerendert (LooperBank::renderBlock),
    processBlock kopiert nur. numSamples-Mismatch oder fehlende Bank →
    Stille (definiertes Verhalten in Test-Rigs/beim Prepare).
*/
class LooperOutModule final : public IOModule,
                              public ILooperAudioClient
{
public:
    LooperOutModule();

    static constexpr const char* staticModuleId = "looper_out";

    enum class Mode : int { stereo = 0, sum, left, right };
    static constexpr const char* modeStereo = "stereo";
    static constexpr const char* modeSum    = "sum";
    static constexpr const char* modeLeft   = "left";
    static constexpr const char* modeRight  = "right";

    /** Ein Abgriff: target 0 = Master, 1..4 = Looper n. */
    struct OutputSpec
    {
        int  target = 0;
        Mode mode = Mode::stereo;
        bool pre = false;
    };

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    /** Node-Skelett mit der Default-Bestückung (Master + Looper 1–4). */
    [[nodiscard]] juce::ValueTree createState() override;

    //==========================================================================
    // Schema-Helfer (static, pure) — UI und GraphManager nutzen sie

    /** Schreibt die <Outputs>-Liste + numOutputChannels (Σ Breiten) frisch
        in den Node-Tree. Leere Liste → Master stereo. */
    static void applyOutputConfig (juce::ValueTree nodeTree,
                                   const std::vector<OutputSpec>& specs,
                                   juce::UndoManager* undo = nullptr);

    /** Leitet die Slots aus dem Node-Tree ab (Materialisierung). */
    [[nodiscard]] static std::vector<OutputSpec> readOutputConfig (const juce::ValueTree& nodeTree);

    /** Hängt EINEN Slot an (undo-fähig) — Pfad des „+"-Buttons. */
    static void appendOutput (juce::ValueTree nodeTree, OutputSpec spec,
                              juce::UndoManager* undo);

    /** Entfernt den Slot an Position index (undo-fähig); der letzte Slot
        bleibt IMMER stehen. */
    static void removeOutput (juce::ValueTree nodeTree, int index,
                              juce::UndoManager* undo);

    /** Kanalbreite eines Modus (stereo = 2, sonst 1). */
    [[nodiscard]] static int widthOf (Mode mode) noexcept
    {
        return mode == Mode::stereo ? 2 : 1;
    }

    /** UI-Label eines Slots, z. B. "Master", "Looper 2", "Looper 3 · L",
        "Looper 1 · Summe" — Pre-Zusatz macht die UI selbst. */
    [[nodiscard]] static juce::String outputLabel (const OutputSpec& spec);

    [[nodiscard]] static Mode modeFromString (const juce::String& text) noexcept;
    [[nodiscard]] static juce::String toString (Mode mode);

    //==========================================================================
    // ILooperAudioClient [Message Thread, vor prepareForGraph]
    void setLooperAudioSource (LooperBank* bank) override;

    /** [Message Thread, vor prepareForGraph] Slots aus dem Tree — setzt
        das Ausgangs-Bus-Layout (Σ Breiten). */
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperOutModule)
};

} // namespace conduit
