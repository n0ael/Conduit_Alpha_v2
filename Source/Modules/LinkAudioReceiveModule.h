#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_events/juce_events.h>

#include "Core/LinkClock.h"
#include "Core/LinkReceiveStream.h"
#include "Interfaces/IClockSlave.h"
#include "Interfaces/ILinkAudioClient.h"
#include "NetworkIOModule.h"

namespace conduit
{

//==============================================================================
/**
    Link Audio Receive (docs/LinkAudio.md, Empfangen Phase 2): abonniert
    einen Audio-Kanal der Link-Session und gibt ihn beat-aligned als
    Stereo-Quelle in den Graph (keine Eingänge, 2 Ausgänge; mono-Kanäle
    laufen auf beide Seiten).

    Kanal-Bindung [Message Thread]: persistiert wird NUR der Wunsch
    (targetPeer/targetChannel als Namen, Schema 6.2) — nie ChannelKeys
    (session-transient, CLAUDE.md 6). Der GraphManager spiegelt die
    Node-Properties per setTargetChannel() hierher; rebind() matcht beim
    ChannelsChanged-Broadcast gegen availableChannels(). Die Liste
    enthält NUR Peer-Kanäle (eigene Sinks announced Link nicht an sich
    selbst) — Loopback braucht einen echten Peer, z.B. Ableton Live.

    Audio-Pfad [Audio Thread, 3.1]: der Link-Thread-Callback pusht
    beat-gestempelte Buffer in den LinkReceiveStream (SpscQueue);
    processBlock rendert das Latenzfenster (latency_ms-Parameter,
    Dual-State 6.1). Die effektive Hörlatenz IST latency_ms.

    Lifecycle: Delete Phase 1 (releaseSessionResources, 5.3) resettet die
    Source auf dem Message Thread — ~LinkAudioSource stoppt den Callback,
    danach schreibt kein Link-Thread mehr (Member-Reihenfolge: stream VOR
    source). enableAudio-Refcount wird gehalten, solange der Link-Kontext
    gesetzt ist (Discovery braucht aktives Link Audio).
*/
class LinkAudioReceiveModule final : public NetworkIOModule,
                                     public ILinkAudioClient,
                                     public IClockSlave,
                                     private juce::ChangeListener
{
public:
    LinkAudioReceiveModule();
    ~LinkAudioReceiveModule() override;

    static constexpr const char* staticModuleId = "link_audio_receive";
    static constexpr const char* latencyParamId = "latency_ms";

    static constexpr float latencyMinMs     = 20.0f;
    static constexpr float latencyMaxMs     = 500.0f;
    static constexpr float latencyDefaultMs = 150.0f;

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;
    [[nodiscard]] juce::ValueTree createState() override;

    //==========================================================================
    // ILinkAudioClient [Message Thread]
    void setLinkAudioContext (LinkClock* clock, const juce::String& initialModuleId) override;
    void moduleIdRenamed (const juce::String& newModuleId) override;   // announced nichts — no-op
    void releaseSessionResources() override;

    // IClockSlave [Message Thread, vor Graph-Aufnahme]
    void setClockBus (const ClockBus* bus) noexcept override;

    //==========================================================================
    /** Kanal-Wunsch setzen [Message Thread] — der GraphManager spiegelt die
        Node-Properties targetPeer/targetChannel hierher (Materialisierung
        UND Live-Änderung durch die UI/Undo). Leere Namen lösen die Bindung. */
    void setTargetChannel (const juce::String& peer, const juce::String& channel);

    /** Pure Match-Logik des rebind() (Test-Zugang): erster Kanal mit
        passendem peerName + name, 0 wenn keiner passt. */
    [[nodiscard]] static LinkClock::ChannelKey findChannelKey (
        const std::vector<LinkClock::ChannelInfo>& channels,
        const juce::String& peer, const juce::String& channel) noexcept;

    //==========================================================================
    // AudioProcessor
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // Echtzeit-Parameter-Ziel (Dual-State 6.1): latency_ms
    [[nodiscard]] std::atomic<float>* getParameterTarget (const juce::String& parameterId) noexcept override;

    //==========================================================================
    enum class ReceiveStatus : int
    {
        offline   = 0,   // kein Link-Kontext oder kein Kanal-Wunsch
        searching = 1,   // Wunsch gesetzt, Kanal (noch) nicht in der Session
        waiting   = 2,   // abonniert — Latenzfenster füllt sich / kein Traffic
        streaming = 3    // rendert Audio
    };

    /** Aggregierter Status (beliebiger Thread, atomic). */
    [[nodiscard]] ReceiveStatus getReceiveStatusForUi() const noexcept;

    /** Gepufferte Sekunden hinter der Leseposition (Latenz-Tuning-Hilfe). */
    [[nodiscard]] float getBufferedSecondsForUi() const noexcept { return stream.getBufferedSeconds(); }

    /** Diagnose (Dev-Statuszeile): Drops, Sequenzlücken, Render-Resets. */
    [[nodiscard]] std::uint32_t getDroppedPushesForUi() const noexcept { return stream.getDroppedPushes(); }
    [[nodiscard]] std::uint32_t getSequenceGapsForUi()  const noexcept { return stream.getSequenceGaps(); }
    [[nodiscard]] std::uint32_t getRenderResetsForUi()  const noexcept { return stream.getRenderResets(); }

    //==========================================================================
    // Message Thread — Diagnose/Tests
    [[nodiscard]] bool hasActiveSource() const noexcept { return source != nullptr; }
    [[nodiscard]] juce::String getTargetPeer() const    { return targetPeer; }
    [[nodiscard]] juce::String getTargetChannel() const { return targetChannel; }

    /** Test-Seam: direkter Zugriff auf den Empfangs-Strom (Buffer-Injektion
        ohne echte Link-Session). */
    [[nodiscard]] LinkReceiveStream& getStreamForTest() noexcept { return stream; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void rebind();

    //==========================================================================
    // Member-Reihenfolge (Teardown): stream VOR source deklarieren — der
    // Link-Thread-Callback schreibt in stream; ~Source stoppt ihn zuerst.
    LinkReceiveStream stream;
    std::unique_ptr<LinkClock::Source> source;

    // Message Thread
    LinkClock*   linkClock = nullptr;
    juce::String targetPeer, targetChannel;
    bool         audioEnabledHeld = false;   // enableAudio-Refcount gehalten?

    // Audio Thread liest; Injektion vor Graph-Aufnahme (4.2)
    const ClockBus* clockBus = nullptr;

    std::atomic<float> latencyTarget { latencyDefaultMs };

    // 0 = offline, 1 = searching, 2 = gebunden (Message Thread schreibt)
    std::atomic<int> bindState { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkAudioReceiveModule)
};

} // namespace conduit
