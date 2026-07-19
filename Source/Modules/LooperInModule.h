#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "Core/Capture/CaptureService.h"
#include "Interfaces/ICaptureTapClient.h"
#include "Interfaces/ISendConfigClient.h"
#include "IOModule.h"

namespace conduit
{

//==============================================================================
/**
    Looper Audio In (Looper-I/O 07/2026): macht beliebige Graph-Signale zu
    Looper-Quellen. Dynamische Eingangs-Slots (mono | stereo, benennbar) —
    pro Slot registriert das Modul virtuelle Capture-Kanäle (Mono: ein Kanal
    "{moduleId}/{name}", Stereo: "{moduleId}/{name}_l/_r"); die Looper-
    Quellen-Combo listet die Slots ZUOBERST (Schlüssel "tap:{basisname}",
    Auflösung über den bestehenden tap:-Zweig von resolveLooperSourceKey).
    Der Output ist reines Pass-Through — transparent in jede Kette patchbar
    (Muster CaptureTapModule).

    Slot-Struktur im Node-Tree (<Inputs>, Schema des Link-Audio-Send):
    Eingangszahl/Breiten sind FIX pro Materialisierung — der GraphManager
    injiziert sie via ISendConfigClient VOR prepareForGraph; eine Änderung
    (numInputChannels/numOutputChannels-Property) re-materialisiert den Node
    im nächsten gefadeten Swap (rematerializeLooperIoNode).

    Lifecycle [Message Thread] wie CaptureTapModule: Registrierung in
    prepareForGraph (idempotent; keine freien Slots → Result::fail →
    nodeError), Rename (Modul UND Slot-Name) benennt die Spurnamen live um,
    Delete Phase 1 (releaseCaptureResources) trennt den Audio-Thread sofort
    und lässt laufendes Material als "held" beim Service.

    Audio-Pfad [Audio Thread, 3.1]: allocation-/lock-frei — Atomic-Loads
    plus writeVirtualChannel (Meter/Gate/Ring vorallokiert).
*/
class LooperInModule final : public IOModule,
                             public ICaptureTapClient,
                             public ISendConfigClient
{
public:
    LooperInModule();
    ~LooperInModule() override;

    static constexpr const char* staticModuleId = "looper_in";

    enum class InputMode { mono, stereo };
    static constexpr const char* modeMono   = "mono";
    static constexpr const char* modeStereo = "stereo";

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    /** Node-Skelett mit dem Default-Eingang (1 Stereo, autoName "In 1"). */
    [[nodiscard]] juce::ValueTree createState() override;

    //==========================================================================
    // Schema-Helfer (static, pure) — UI und GraphManager nutzen sie

    /** Schreibt die <Inputs>-Liste (inputId/mode/userName/autoName) und
        numInputChannels = numOutputChannels = Σ Breiten frisch in den
        Node-Tree. Leere Liste → 1 Stereo-Eingang. undo != nullptr macht
        die Änderung undo-fähig (Slot-Umbau aus der UI). */
    static void applyInputConfig (juce::ValueTree nodeTree,
                                  const std::vector<InputMode>& modes,
                                  juce::UndoManager* undo = nullptr);

    /** Hängt EINEN Slot an eine bestehende <Inputs>-Liste an und zieht die
        Kanalzahlen nach (undo-fähig) — Pfad des „+"-Buttons der Node-UI.
        Die Kanalzahl-Änderung re-materialisiert den Node (GraphManager). */
    static void appendInput (juce::ValueTree nodeTree, InputMode mode,
                             juce::UndoManager* undo);

    /** Entfernt den Slot an Position index (undo-fähig); der letzte Slot
        bleibt IMMER stehen (Modul ohne Kanäle wäre ungültig). */
    static void removeInput (juce::ValueTree nodeTree, int index,
                             juce::UndoManager* undo);

    /** Effektiver Slot-Name: userName ?: autoName ?: "In {n}". */
    [[nodiscard]] static juce::String effectiveInputName (const juce::ValueTree& inputTree,
                                                          int index);

    /** Tap-Basisname eines Slots = "{moduleId}/{effectiveName}" — der
        Looper-Quellen-Schlüssel ist "tap:{basisname}" (Mono-Slots
        registrieren exakt diesen Namen, Stereo-Slots _l/_r dahinter). */
    [[nodiscard]] static juce::String tapBaseName (const juce::String& moduleId,
                                                   const juce::String& effectiveName);

    //==========================================================================
    // ISendConfigClient [Message Thread] — Struktur VOR prepareForGraph
    void applySendConfig (const std::vector<SendInputConfig>& inputs) override;
    void inputNameChanged (const juce::String& inputId,
                           const juce::String& effectiveName) override;

    // ICaptureTapClient [Message Thread]
    void setCaptureTapContext (CaptureService* serviceToUse,
                               const juce::String& initialModuleId) override;
    void captureModuleIdRenamed (const juce::String& newModuleId) override;
    void releaseCaptureResources() override;

    //==========================================================================
    /** Registriert die virtuellen Kanäle aller Slots (idempotent) — keine
        freien Capture-Slots → failed() → nodeError (5.2 Schritt 1). */
    [[nodiscard]] juce::Result prepareForGraph (double sampleRate, int maximumBlockSize) override;

    // AudioProcessor
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==========================================================================
    // Message Thread — Diagnose/Tests
    [[nodiscard]] bool isTapRegistered() const noexcept
    {
        return ! handles.empty() && handles.front().isValid();
    }
    [[nodiscard]] int getNumSlots() const noexcept { return (int) slots.size(); }

private:
    /** Ein Slot: Kanalbereich am Bus + Namen der Capture-Kanäle. */
    struct Slot
    {
        juce::String inputId;
        juce::String effectiveName;
        int offset = 0;   // Start-Kanal im Bus
        int width  = 1;   // 1 mono / 2 stereo
    };

    [[nodiscard]] juce::String channelNameFor (const Slot& slot, int channelInSlot) const;
    void refreshChannelNames();
    void unregisterChannels();

    //==========================================================================
    // Message Thread
    CaptureService* service = nullptr;   // Owner: EngineProcessor, überlebt den Graph
    juce::String currentModuleId;
    std::vector<Slot> slots;
    std::vector<CaptureService::VirtualChannelHandle> handles;   // ein Handle pro KANAL
    int totalChannels = 2;

    // Audio Thread liest; Message Thread trennt in Phase 1 (Muster
    // CaptureTapModule: Slots als Atomics gespiegelt, Handles bleiben MT)
    std::atomic<CaptureService*> rtService { nullptr };
    std::array<std::atomic<int>, static_cast<std::size_t> (MAX_CAPTURE_CHANNELS)> rtSlots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperInModule)
};

} // namespace conduit
