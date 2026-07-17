#include "AlphaTrackLcd.h"

#include <vector>

namespace conduit
{

namespace
{
    // Frontier-Design-Group-Header (Native-Mode-Doku v1.0); F0/F7 ergaenzt
    // juce::MidiMessage::createSysExMessage selbst.
    constexpr juce::uint8 kLcdHeader[] = { 0x00, 0x01, 0x40, 0x20, 0x00 };
}

AlphaTrackLcd::AlphaTrackLcd (std::function<void (const juce::MidiMessage&)> sendMidiToUse)
    : sendMidi (std::move (sendMidiToUse))
{
    desired.fill (' ');
    sent.fill (' ');
}

void AlphaTrackLcd::setLine (int line, const juce::String& text)
{
    if (line < 0 || line >= kLines)
        return;

    const auto offset = line * kColumns;

    for (int column = 0; column < kColumns; ++column)
    {
        const auto character = column < text.length() ? text[column] : juce::juce_wchar (' ');
        desired[(size_t) (offset + column)] =
            character >= 0x20 && character <= 0x7e ? (char) character : '?';
    }
}

void AlphaTrackLcd::forceRedraw()
{
    sentValid = false;
}

void AlphaTrackLcd::tick()
{
    if (sendMidi == nullptr)
        return;

    for (int line = 0; line < kLines; ++line)
    {
        const auto offset = line * kColumns;

        // Geaenderten Bereich der Zeile finden (erste..letzte Abweichung).
        int first = -1;
        int last  = -1;
        for (int column = 0; column < kColumns; ++column)
        {
            const auto index = (size_t) (offset + column);
            if (! sentValid || desired[index] != sent[index])
            {
                if (first < 0)
                    first = column;
                last = column;
            }
        }

        if (first < 0)
            continue;   // Zeile unveraendert

        // EIN SysEx ueber den geaenderten Bereich: Header + pos + Zeichen.
        std::vector<juce::uint8> payload (kLcdHeader, kLcdHeader + 5);
        payload.push_back ((juce::uint8) (offset + first));
        for (int column = first; column <= last; ++column)
            payload.push_back ((juce::uint8) desired[(size_t) (offset + column)]);

        sendMidi (juce::MidiMessage::createSysExMessage (payload.data(),
                                                         (int) payload.size()));

        for (int column = first; column <= last; ++column)
        {
            const auto index = (size_t) (offset + column);
            sent[index] = desired[index];
        }
    }

    sentValid = true;
}

juce::MidiMessage AlphaTrackLcd::nativeModeForce()
{
    constexpr juce::uint8 payload[] = { 0x00, 0x01, 0x40, 0x20, 0x01, 0x00 };
    return juce::MidiMessage::createSysExMessage (payload, 6);
}

} // namespace conduit
