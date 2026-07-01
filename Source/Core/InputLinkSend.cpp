#include "InputLinkSend.h"

#include <algorithm>

namespace conduit
{

//==============================================================================
std::vector<InputLinkSend::SendSpec> InputLinkSend::buildSpecs (const ChannelNames& names,
                                                                int numInputChannels)
{
    std::vector<SendSpec> specs;
    const auto channels = juce::jmin (numInputChannels, maxPorts);

    for (int port = 0; port < channels; ++port)
    {
        const bool pairStart = port + 1 < channels
                               && names.isPortPairStart (ChannelNames::Direction::input, port);

        if (names.isPortLinkSendEnabled (ChannelNames::Direction::input, port))
            specs.push_back ({ port, pairStart ? 2 : 1,
                               "audio_in/" + names.getLabel (ChannelNames::Direction::input, port) });

        if (pairStart)
            ++port;  // Partner-Kanal gehört zum Anker-Send (bzw. sendet nicht)
    }

    return specs;
}

//==============================================================================
void InputLinkSend::setLinkClock (LinkClock* clock)
{
    taps.setLinkClock (clock);
}

void InputLinkSend::prepare (int samplesPerBlock)
{
    taps.prepare (samplesPerBlock);
}

void InputLinkSend::applySends (const std::vector<SendSpec>& specs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // 1. Verschwundene Anker retiren (Send aus, Kanal weg, Um-Pairing hat den
    //    Anker verschoben) — Audio-Thread sofort per Atomic getrennt, die
    //    Sink-Destruktion folgt über den Epoch-Handshake (LinkSendTaps).
    for (int i = (int) slots.size(); --i >= 0;)
    {
        auto& slot = slots[(std::size_t) i];

        const bool stillWanted = std::any_of (specs.begin(), specs.end(),
                                              [&] (const SendSpec& spec)
                                              { return spec.anchorPort == slot.spec.anchorPort; });
        if (stillWanted)
            continue;

        rtSlots[(std::size_t) slot.spec.anchorPort].store (nullptr);
        taps.retireTap (slot.tap);
        slots.erase (slots.begin() + i);
    }

    // 2. Deltas am LEBENDEN Sink nachziehen bzw. neue Anker announce —
    //    Namens-/Breiten-Wechsel erzeugen NIE einen neuen Kanal (7.2,
    //    der Ableton-Stream läuft weiter).
    for (const auto& spec : specs)
    {
        if (spec.anchorPort < 0 || spec.anchorPort >= maxPorts)
            continue;

        const auto existing = std::find_if (slots.begin(), slots.end(),
                                            [&] (const Slot& slot)
                                            { return slot.spec.anchorPort == spec.anchorPort; });

        if (existing != slots.end())
        {
            if (existing->spec.channelName != spec.channelName)
                existing->tap->setName (spec.channelName);

            if (existing->spec.width != spec.width)
                existing->tap->setWidth (spec.width);

            existing->spec = spec;
            continue;
        }

        auto* tap = taps.createTap (spec.channelName, spec.width);
        if (tap == nullptr)
            continue;  // kein Link-Kontext (Tests/Standalone ohne Clock)

        slots.push_back ({ spec, tap });
        rtSlots[(std::size_t) spec.anchorPort].store (tap);
    }
}

LinkSendTaps::Tap* InputLinkSend::tapHandleForPort (int anchorPort) const
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto it = std::find_if (slots.begin(), slots.end(),
                                  [&] (const Slot& slot)
                                  { return slot.spec.anchorPort == anchorPort; });
    return it != slots.end() ? it->tap : nullptr;
}

int InputLinkSend::getNumActiveSends() const noexcept
{
    return (int) slots.size();
}

bool InputLinkSend::isRetirePending() const noexcept
{
    return taps.isRetirePending();
}

void InputLinkSend::flushPendingRetirement()
{
    taps.flushPendingRetirement();
}

//==============================================================================
void InputLinkSend::processBlock (const juce::AudioBuffer<float>& buffer, int numInputChannels,
                                  const ClockState& clock) noexcept
{
    taps.noteBlockBegin();  // Epoch-Handshake VOR den Commits (LinkSendTaps)

    const auto numFrames = buffer.getNumSamples();
    const auto channels  = juce::jmin (numInputChannels, buffer.getNumChannels(), maxPorts);

    for (int anchor = 0; anchor < maxPorts; ++anchor)
    {
        auto* tap = rtSlots[(std::size_t) anchor].load();
        if (tap == nullptr)
            continue;

        if (anchor >= channels || numFrames <= 0)
        {
            // Kanal (noch) nicht im Buffer (Device-Wechsel, MT zieht den
            // Diff nach): Kanal announced halten, kein Commit
            tap->noteIdle();
            continue;
        }

        // IMMER zwei gültige Pointer (Partner defensiv auf den Anker
        // gedoppelt): Tap::commit liest die Breite selbst — ein Wechsel
        // zwischen hier und dem Commit kann so nie out-of-range lesen.
        const float* chans[2] = {
            buffer.getReadPointer (anchor),
            anchor + 1 < channels ? buffer.getReadPointer (anchor + 1)
                                  : buffer.getReadPointer (anchor)
        };

        tap->commit (chans, numFrames, clock);
    }
}

//==============================================================================
LinkSendTaps::Status InputLinkSend::statusForPort (int anchorPort) const noexcept
{
    if (anchorPort < 0 || anchorPort >= maxPorts)
        return LinkSendTaps::Status::offline;

    const auto* tap = rtSlots[(std::size_t) anchorPort].load();
    return tap != nullptr ? tap->getStatus() : LinkSendTaps::Status::offline;
}

} // namespace conduit
