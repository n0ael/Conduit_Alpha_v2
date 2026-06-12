#pragma once

#include <array>
#include <atomic>

#include "Core/Capture/CaptureService.h"
#include "Interfaces/ICaptureTapClient.h"
#include "UtilityModule.h"

namespace conduit
{

//==============================================================================
/**
    Capture Tap (CLAUDE.md 4.1, UtilityModule): zapft effektierte Signale
    mitten im Graph für das Capture-System an. 2 In / 2 Out, der Output ist
    reines Pass-Through — das Modul ist transparent in jede Kette patchbar.

    Pro Input-Kanal registriert das Modul einen virtuellen Capture-Kanal
    beim CaptureService (Spurname = moduleId + "_l"/"_r"); processBlock
    schreibt die Blöcke via writeVirtualChannel() — die globale SampleClock
    stempelt wie beim Hardware-Tap, Capture All exportiert Hardware- und
    Tap-Spuren deshalb sample-aligned in einem Job.

    Lifecycle [Message Thread]:
      - GraphManager injiziert Service + moduleId via ICaptureTapClient VOR
        prepareForGraph; die Registrierung passiert in prepareForGraph
        (idempotent, 5.2 Schritt 1) — keine freien Slots → Result::fail →
        nodeError, das Modul kommt nicht in den Graph.
      - Rename (captureModuleIdRenamed) → Spurnamen folgen live.
      - Delete Phase 1 (releaseCaptureResources, 5.3): rtService-Atomic
        trennt den Audio-Thread sofort, die Deregistrierung beim Service
        lässt laufendes Material als "held" zurück (Export/Reclaim wie
        Hardware). Destruktor ohne Phase 1 (Preset-Load/Shutdown) teilt
        sich den Pfad. Kein Epoch-Handshake nötig: der Service überlebt
        den Graph (EngineProcessor-Deklarationsreihenfolge), ein letzter
        in-flight Block läuft gegen das writerActive-Atomic des Slots.

    Dokumentierte Grenzen:
      - Taps liegen IM Graph: Topologie-Swaps (5.2) blenden den Graph-
        Output über ~5 ms aus und wieder ein — diese Fades sind in
        Tap-Aufnahmen hörbar, in Hardware-Captures (Tap VOR dem Graph)
        nicht.
      - Plugin-/Modul-Latenzen im Signalweg werden nicht kompensiert —
        Tap-Spuren liegen um die akkumulierte Latenz ihrer Kette hinter
        den Hardware-Spuren (Folgethema).

    Audio-Pfad [Audio Thread, 3.1]: allocation-/lock-frei — nur Atomic-Load
    plus writeVirtualChannel (Meter/Gate/Ring, alles vorallokiert).
*/
class CaptureTapModule final : public UtilityModule,
                               public ICaptureTapClient
{
public:
    CaptureTapModule();
    ~CaptureTapModule() override;

    static constexpr const char* staticModuleId = "capture_tap";
    static constexpr int numTapChannels = 2;

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

    //==========================================================================
    // ICaptureTapClient [Message Thread]
    void setCaptureTapContext (CaptureService* serviceToUse,
                               const juce::String& initialModuleId) override;
    void captureModuleIdRenamed (const juce::String& newModuleId) override;
    void releaseCaptureResources() override;

    //==========================================================================
    /** Registriert die virtuellen Kanäle (idempotent) — keine freien Slots
        → failed(), der GraphManager setzt nodeError (5.2 Schritt 1). */
    [[nodiscard]] juce::Result prepareForGraph (double sampleRate, int maximumBlockSize) override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==========================================================================
    // Message Thread — Diagnose/Tests
    [[nodiscard]] bool isTapRegistered() const noexcept { return handles[0].isValid(); }

    /** Spurname eines Tap-Kanals (Suffix _l/_r) — auch der Test nutzt
        exakt diese Ableitung. */
    [[nodiscard]] static juce::String channelNameFor (const juce::String& moduleId, int channel);

private:
    /** Phase 1 und Destruktor teilen sich den Pfad (Muster
        LinkAudioSendModule::disableAudioOnce). */
    void unregisterChannels();

    //==========================================================================
    // Message Thread
    CaptureService* service = nullptr;  // Owner: EngineProcessor, überlebt den Graph
    juce::String currentModuleId;
    std::array<CaptureService::VirtualChannelHandle,
               static_cast<size_t> (numTapChannels)> handles;

    // Audio Thread liest; Message Thread trennt in Phase 1 (Klassendoku).
    // Die Slots sind als Atomics gespiegelt — Phase 1 invalidiert die
    // MT-Handles, während ein in-flight Block noch liest (kein Race).
    std::atomic<CaptureService*> rtService { nullptr };
    std::array<std::atomic<int>, static_cast<size_t> (numTapChannels)> rtSlots { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureTapModule)
};

} // namespace conduit
