#include "RtAllocationGuard.h"

#include <atomic>
#include <cstdlib>
#include <new>

#if CONDUIT_RT_ALLOCATION_CHECKS && defined (_MSC_VER)
 #include <intrin.h>
 extern "C" __declspec (dllimport) int __stdcall IsDebuggerPresent();
#endif

namespace
{

// Pro Thread: läuft gerade ein auditierter Echtzeit-Abschnitt?
// Constant-initialisiert — sicher auch für Allokationen während der
// statischen Initialisierung und vor dem TLS-Setup neuer Threads.
thread_local bool realtimeSectionActive = false;

std::atomic<std::uint64_t> violationCount { 0 };

#if CONDUIT_RT_ALLOCATION_CHECKS

void noteViolation() noexcept
{
    violationCount.fetch_add (1, std::memory_order_relaxed);

   #if defined (_MSC_VER)
    // Unter angeschlossenem Debugger sofort anhalten — der Callstack zeigt
    // die verbotene Allokation direkt. Ohne Debugger nur zählen (Tests/CI).
    // Kein jassert: dessen Logging allokiert selbst (Endlos-Rekursion).
    if (IsDebuggerPresent() != 0)
        __debugbreak();
   #endif
}

void* checkedAllocate (std::size_t size) noexcept
{
    if (realtimeSectionActive)
        noteViolation();

    return std::malloc (size > 0 ? size : 1);
}

void checkedFree (void* pointer) noexcept
{
    if (pointer == nullptr)
        return;

    // free ist im RT-Pfad genauso verboten wie malloc (CLAUDE.md 3.1)
    if (realtimeSectionActive)
        noteViolation();

    std::free (pointer);
}

#endif // CONDUIT_RT_ALLOCATION_CHECKS

} // namespace

namespace conduit::rt
{

bool isRealtimeSection() noexcept
{
    return realtimeSectionActive;
}

std::uint64_t getAllocationViolations() noexcept
{
    return violationCount.load (std::memory_order_relaxed);
}

ScopedRealtimeSection::ScopedRealtimeSection() noexcept
    : previous (realtimeSectionActive)
{
    realtimeSectionActive = true;
}

ScopedRealtimeSection::~ScopedRealtimeSection() noexcept
{
    realtimeSectionActive = previous;
}

ScopedAllocationAllowance::ScopedAllocationAllowance() noexcept
    : previous (realtimeSectionActive)
{
    realtimeSectionActive = false;
}

ScopedAllocationAllowance::~ScopedAllocationAllowance() noexcept
{
    realtimeSectionActive = previous;
}

} // namespace conduit::rt

#if CONDUIT_RT_ALLOCATION_CHECKS

//==============================================================================
// Globale operator-new/delete-Ersetzungen (nur Dev-Builds, binärweite
// Wirkung inkl. JUCE/Catch2). Allokation via malloc, Freigabe via free —
// in sich konsistent, verträgt sich mit ASan (malloc bleibt interceptiert)
// und TSan (unsere starken Definitionen ersetzen die schwachen Interceptor-
// Varianten, malloc wird weiterhin instrumentiert).
// Aligned-Varianten (align_val_t) werden bewusst nicht ersetzt: deren
// Default-Implementierung bildet ein eigenes konsistentes Paar.

// Clang meldet -Wmissing-prototypes für die unsized operator-delete-
// Ersetzungen (libstdc++-Deklarations-Eigenheit) — Ersetzungen brauchen
// per Standard keine vorherige Deklaration
#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

void* operator new (std::size_t size)
{
    if (auto* pointer = checkedAllocate (size))
        return pointer;

    throw std::bad_alloc {};
}

void* operator new[] (std::size_t size)
{
    if (auto* pointer = checkedAllocate (size))
        return pointer;

    throw std::bad_alloc {};
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    return checkedAllocate (size);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    return checkedAllocate (size);
}

void operator delete (void* pointer) noexcept                   { checkedFree (pointer); }
void operator delete[] (void* pointer) noexcept                 { checkedFree (pointer); }
void operator delete (void* pointer, std::size_t) noexcept      { checkedFree (pointer); }
void operator delete[] (void* pointer, std::size_t) noexcept    { checkedFree (pointer); }
void operator delete (void* pointer, const std::nothrow_t&) noexcept   { checkedFree (pointer); }
void operator delete[] (void* pointer, const std::nothrow_t&) noexcept { checkedFree (pointer); }

#if defined (__clang__)
 #pragma clang diagnostic pop
#endif

#endif // CONDUIT_RT_ALLOCATION_CHECKS
