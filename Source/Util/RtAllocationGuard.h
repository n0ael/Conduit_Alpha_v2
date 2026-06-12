#pragma once

#include <cstdint>

// Dev-Build-Schalter — CMake setzt =1 für Debug-Konfigurationen beider
// Targets. Nur dann ersetzt RtAllocationGuard.cpp die globalen
// operator new/delete; ohne den Schalter bleibt vom Audit nur das
// (billige) thread_local-Flag übrig.
#ifndef CONDUIT_RT_ALLOCATION_CHECKS
 #define CONDUIT_RT_ALLOCATION_CHECKS 0
#endif

namespace conduit::rt
{

//==============================================================================
/**
    RT-Audit-Werkzeug: erkennt Heap-Allokationen in Echtzeit-Abschnitten
    (CLAUDE.md 3.1 — Lock-free & Allocation-free, Non-Negotiable).

    In Dev-Builds (CONDUIT_RT_ALLOCATION_CHECKS=1) ersetzt die zugehörige
    .cpp die globalen operator new/delete: Läuft der aktuelle Thread
    innerhalb einer ScopedRealtimeSection, zählt jede (De-)Allokation als
    Violation (globaler atomarer Zähler) und hält unter einem angehängten
    Debugger sofort an — der Callstack zeigt die verbotene Allokation direkt.

    Verwendung (Audio-Callback oder simulierter Audio-Thread in Tests):

        const rt::ScopedRealtimeSection rtAudit;
        captureService.processInputTap (buffer, numInputs);

    Grenzen: erfasst wird new/delete (damit std-Container, juce::String,
    make_unique, ...), NICHT rohes malloc/free (juce::HeapBlock alloziert
    bewusst daran vorbei) und keine Locks/OS-Calls — dafür bleiben
    TSan (CLAUDE.md 13.4) und Code-Review zuständig.

    Wichtig in Tests: Catch2-Makros allokieren — Assertions IMMER außerhalb
    der Section ausführen (Zähler vorher/nachher vergleichen).

    Threading: das Flag ist thread_local — Sections verschiedener Threads
    beeinflussen sich nicht. Der Violation-Zähler ist global-atomar und von
    jedem Thread lesbar; die Hooks selbst allokieren und locken nie.

    Wiederverwendbar für bestehende Modul-Pfade: jede processBlock()-
    Implementierung (oder ein Teilabschnitt) lässt sich mit einer Section
    auditieren, ohne das Modul zu ändern.
*/

/** true, wenn der AKTUELLE Thread in einer ScopedRealtimeSection läuft. */
[[nodiscard]] bool isRealtimeSection() noexcept;

/** Globaler Violation-Zähler — nur in Dev-Builds kann er wachsen.
    Tests vergleichen vorher/nachher statt auf 0 zu bestehen (andere
    Testfälle desselben Laufs könnten bereits gezählt haben). */
[[nodiscard]] std::uint64_t getAllocationViolations() noexcept;

//==============================================================================
/** RAII: markiert den umschlossenen Code als Echtzeit-Abschnitt. Nestbar —
    der Destruktor stellt den vorherigen Zustand wieder her. */
class ScopedRealtimeSection
{
public:
    ScopedRealtimeSection() noexcept;
    ~ScopedRealtimeSection() noexcept;

    ScopedRealtimeSection (const ScopedRealtimeSection&) = delete;
    ScopedRealtimeSection& operator= (const ScopedRealtimeSection&) = delete;

private:
    bool previous;
};

//==============================================================================
/** RAII: hebt eine umgebende Section gezielt auf — erlaubte Insel für
    Mess-/Testinfrastruktur innerhalb eines auditierten Abschnitts. */
class ScopedAllocationAllowance
{
public:
    ScopedAllocationAllowance() noexcept;
    ~ScopedAllocationAllowance() noexcept;

    ScopedAllocationAllowance (const ScopedAllocationAllowance&) = delete;
    ScopedAllocationAllowance& operator= (const ScopedAllocationAllowance&) = delete;

private:
    bool previous;
};

} // namespace conduit::rt
