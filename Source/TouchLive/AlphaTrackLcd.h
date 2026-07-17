#pragma once

#include <array>
#include <functional>

#include <juce_audio_basics/juce_audio_basics.h>

namespace conduit
{

//==============================================================================
/** 2x16-LCD-Treiber des Frontier AlphaTrack (Live-Remote-Bridge, 07/2026).

    Wire-Format (offizielle Native-Mode-Doku v1.0): SysEx
    `f0 00 01 40 20 00 <pos> <ascii...> f7`, pos 0x00..0x0f = Zeile 1,
    0x10..0x1f = Zeile 2; ein Send darf hoechstens bis zum Display-Ende
    (0x20 - pos Zeichen) reichen.

    Diff-Prinzip (Muster PositionFeedbackRouter): `setLine` schreibt nur den
    SOLL-Frame; `tick()` (60-Hz-Hub-Drain) difft gegen den zuletzt
    GESENDETEN Frame und schickt pro Zeile hoechstens EIN SysEx ueber den
    geaenderten Bereich -- kein 32-Zeichen-Dauerstrom, kein Flackern.
    Nicht-ASCII wird zu '?' (das Display kann nur den HD44780-aehnlichen
    Basissatz), Zeilen werden auf 16 Zeichen gepolstert/gekappt.

    Headless, deterministisch testbar. Message Thread. */
class AlphaTrackLcd
{
public:
    static constexpr int kColumns = 16;
    static constexpr int kLines   = 2;

    explicit AlphaTrackLcd (std::function<void (const juce::MidiMessage&)> sendMidiToUse);

    /** SOLL-Text einer Zeile (0|1) setzen -- gesendet wird erst im tick(). */
    void setLine (int line, const juce::String& text);

    /** Diff senden (max. ein SysEx pro Zeile und Tick). */
    void tick();

    /** Gesendeten Stand verwerfen (Geraet neu verbunden/Native-Mode-Force):
        der naechste tick() zeichnet das komplette Display neu. */
    void forceRedraw();

    /** `f0 00 01 40 20 01 00 f7` -- zwingt den AlphaTrack-Treiber in den
        Native Mode (ersetzt das Treiber-Applet). */
    [[nodiscard]] static juce::MidiMessage nativeModeForce();

private:
    std::array<char, kColumns * kLines> desired;
    std::array<char, kColumns * kLines> sent;
    bool sentValid = false;

    std::function<void (const juce::MidiMessage&)> sendMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlphaTrackLcd)
};

} // namespace conduit
