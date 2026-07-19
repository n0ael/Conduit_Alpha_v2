#pragma once

namespace conduit
{

class LooperBank;

//==============================================================================
/**
    Mixin-Interface (CLAUDE.md 4.2-Stil): Module, die die Looper-Busse der
    LooperBank lesen (LooperPatchOutModule). Der GraphManager injiziert die Bank
    bei der Materialisierung VOR prepareForGraph (5.2 Schritt 1).

    Thread-Ownership: setLooperAudioSource läuft auf dem Message Thread;
    das Modul liest die Bank danach NUR im Audio-Callback (getAudioView(),
    im selben Callback NACH LooperBank::renderBlock — der EngineProcessor
    rendert VOR graph.processBlock). Die Bank ist im EngineProcessor VOR
    dem Graph deklariert und überlebt dessen Destruktion (Muster
    CaptureService/LinkClock).
*/
class ILooperAudioClient
{
public:
    virtual ~ILooperAudioClient() = default;

    /** Message Thread, vor prepareForGraph(). bank darf nullptr sein
        (Tests) — das Modul gibt dann Stille aus. */
    virtual void setLooperAudioSource (LooperBank* bank) = 0;
};

} // namespace conduit
