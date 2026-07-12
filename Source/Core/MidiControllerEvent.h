#pragma once

namespace conduit::midi
{

//==============================================================================
/** Vereinheitlichtes Controller-Event des MIDI-Rig-Subsystems (ADR 006 E4).
    Reist als POD durch die SpscQueue pro Eingangsport (MIDI-System-Thread →
    Message Thread). M1b füllt nur kind=cc; nrpn/programChange kommen mit M2
    (NRPN-Assembler pro Port VOR dem Queue-Push) — die Felder sind bewusst
    vollständig angelegt, damit der Event-Typ dann nicht migriert werden muss. */
struct ControllerEvent
{
    enum class Kind : int { cc = 0, nrpn, programChange };

    Kind kind      = Kind::cc;
    int  channel   = 1;      // 1..16
    int  number    = 0;      // CC-Nummer | NRPN-Parameter | Program-Nummer
    int  value     = 0;      // 7-bit (cc/pc) oder 14-bit (nrpn, is14Bit)
    bool is14Bit   = false;
    bool isRelative = false; // Relative-Encoder-Modi (M2+)
};

//==============================================================================
/** Note-Event (Noten-Echo u. Ä., früherer Grid-Block H4).
    velocity 0..127; isOn=false deckt auch NoteOn mit Velocity 0 ab. */
struct NoteEvent
{
    int  note     = 0;
    int  velocity = 0;
    bool isOn     = false;
};

} // namespace conduit::midi
