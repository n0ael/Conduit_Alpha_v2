#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "ChannelNames.h"
#include "LinkSendTaps.h"

namespace conduit
{

//==============================================================================
/**
    Eingebetteter Link-Send des Hardware-EINGANGS (CLAUDE.md 7.2): sendet
    rohe Input-Kanäle (mono oder als Stereo-Paar) als Link-Audio-Kanäle —
    ohne Send-Modul im Graph. Besitzer ist der EngineProcessor; der Zustand
    (welcher Kanal sendet, Pairing, Labels) lebt in ChannelNames (App-Zustand:
    läge er im Patch, würde jeder Preset-Load den Ableton-Stream abreißen).

    Kernanforderung Stream-Stabilität: applySends() arbeitet DIFF-basiert am
    lebenden Sink — reines Namens-Delta → Tap::setName (live), Breiten-Delta
    (mono ↔ Stereo-Paar am selben Anker) → Tap::setWidth; NIEMALS retire +
    create für diese Fälle. Nur verschwundene Anker (Send aus, Kanal weg,
    Anker-Verschiebung durch Um-Pairing) retiren ihren Kanal.

    Threading:
      - setLinkClock/prepare/applySends/tapHandleForPort → Message Thread
      - processBlock → Audio Thread, allocation-/lock-frei (3.1). Läuft im
        selben Callback NACH LinkClock::captureClockState (Sink-Doku) und
        VOR graph.processBlock (der Buffer trägt dort noch den rohen Input).
      - statusForPort → beliebiger Thread (atomics)
      - RT-Kopplung: rtSlots[anchor] ist ein atomarer Tap-Pointer (Index =
        Anker-Port, kann nicht tearen); die Breite liest Tap::commit selbst
        (EIN atomarer Read). processBlock übergibt IMMER zwei gültige
        Kanal-Pointer (Partner defensiv auf den Anker gedoppelt) — ein
        Breiten-Wechsel zwischen Bounds-Check und Commit kann so nie
        out-of-range lesen (statt des geplanten gepackten anchor+width-
        Atomics; der Anker steckt hier im Index).
*/
class InputLinkSend final
{
public:
    InputLinkSend() = default;

    /** Obergrenze der Anker-Ports (deckt MAX_CAPTURE_CHANNELS). */
    static constexpr int maxPorts = 64;

    //==========================================================================
    /** Gewünschter Send eines Anker-Ports (Port-Indizes der aktiven Bank). */
    struct SendSpec
    {
        int anchorPort = 0;
        int width = 1;              // 1 mono / 2 Stereo-Paar (anchor, anchor+1)
        juce::String channelName;   // vollständiger Link-Kanal-Name
    };

    /** Pure: Specs aus dem ChannelNames-Zustand ableiten — pro Port mit
        aktiviertem Send ein Spec, Stereo-Paare als EIN Spec am Anker
        (Name = Anker-Label mit "audio_in/"-Präfix). Testbar ohne Engine. */
    [[nodiscard]] static std::vector<SendSpec> buildSpecs (const ChannelNames& names,
                                                           int numInputChannels);

    //==========================================================================
    // Message Thread

    void setLinkClock (LinkClock* clock);
    void prepare (int samplesPerBlock);

    /** Diff-basiert/idempotent (Klassendoku): Name → setName, Breite →
        setWidth, verschwundener Anker → retireTap, neuer Anker → createTap.
        Ohne LinkClock entstehen keine Taps (Tests/Standalone). */
    void applySends (const std::vector<SendSpec>& specs);

    /** Test-Seam: Tap-Handle des Ankers (nullptr = kein aktiver Send). */
    [[nodiscard]] LinkSendTaps::Tap* tapHandleForPort (int anchorPort) const;

    [[nodiscard]] int getNumActiveSends() const noexcept;

    [[nodiscard]] bool isRetirePending() const noexcept;
    void flushPendingRetirement();

    //==========================================================================
    // Audio Thread

    /** Sendet alle aktiven Anker aus dem ROHEN Input-Buffer. Ein Anker
        außerhalb der aktuellen Kanalzahl hält seinen Kanal idle (announced),
        bis der Message Thread den Diff nachzieht. */
    void processBlock (const juce::AudioBuffer<float>& buffer, int numInputChannels,
                       const ClockState& clock) noexcept;

    //==========================================================================
    /** Beliebiger Thread (atomics) — für die Send-UI (Schritt 4). */
    [[nodiscard]] LinkSendTaps::Status statusForPort (int anchorPort) const noexcept;

private:
    //==========================================================================
    // Message Thread — Soll-Zustand pro Anker
    struct Slot
    {
        SendSpec spec;
        LinkSendTaps::Tap* tap = nullptr;  // Pool-Eintrag, Adresse stabil
    };

    LinkSendTaps taps;
    std::vector<Slot> slots;

    // Audio Thread liest; Message Thread published (applySends)
    std::array<std::atomic<LinkSendTaps::Tap*>, maxPorts> rtSlots {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputLinkSend)
};

} // namespace conduit
