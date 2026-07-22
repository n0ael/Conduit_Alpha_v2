#include "RawPointerSource.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <commctrl.h>   // SetWindowSubclass / DefSubclassProc / RemoveWindowSubclass
#endif

namespace touchlab
{

#if JUCE_WINDOWS

namespace
{
    constexpr UINT_PTR kSubclassId = 0x7C11 ; // beliebig, eindeutig für dieses Subclass

    LRESULT CALLBACK rawPointerProc (HWND h, UINT msg, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref)
    {
        if (auto* self = reinterpret_cast<RawPointerSource*> (ref))
        {
            switch (msg)
            {
                case WM_POINTERDOWN:   self->handlePointer (GET_POINTERID_WPARAM (w), Phase::Down); break;
                case WM_POINTERUPDATE: self->handlePointer (GET_POINTERID_WPARAM (w), Phase::Move); break;
                case WM_POINTERUP:     self->handlePointer (GET_POINTERID_WPARAM (w), Phase::Up);   break;
                default: break;
            }
        }

        return DefSubclassProc (h, msg, w, l);
    }
}

#endif // JUCE_WINDOWS

//==============================================================================
RawPointerSource::RawPointerSource (juce::Component& windowComponent,
                                    juce::Component& referenceComponent,
                                    TouchSink& sinkToUse)
    : window (windowComponent), reference (referenceComponent), sink (sinkToUse)
{
}

RawPointerSource::~RawPointerSource()
{
    detach();
}

void RawPointerSource::attach()
{
#if JUCE_WINDOWS
    if (attached)
        return;

    auto* peer = window.getPeer();
    if (peer == nullptr)
        return;

    hwnd = peer->getNativeHandle();
    if (hwnd == nullptr)
        return;

    if (SetWindowSubclass (reinterpret_cast<HWND> (hwnd), rawPointerProc, kSubclassId,
                           reinterpret_cast<DWORD_PTR> (this)))
        attached = true;
#endif
}

void RawPointerSource::detach()
{
#if JUCE_WINDOWS
    if (attached && hwnd != nullptr)
        RemoveWindowSubclass (reinterpret_cast<HWND> (hwnd), rawPointerProc, kSubclassId);
#endif
    attached = false;
}

void RawPointerSource::setForcePointerMode (bool shouldForce)
{
#if JUCE_WINDOWS
    if (hwnd == nullptr)
        return;

    if (shouldForce)
        UnregisterTouchWindow (reinterpret_cast<HWND> (hwnd));
    else
        RegisterTouchWindow (reinterpret_cast<HWND> (hwnd), 0);
#else
    juce::ignoreUnused (shouldForce);
#endif
}

void RawPointerSource::handlePointer (unsigned int pointerId, Phase phase)
{
#if JUCE_WINDOWS
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (! GetPointerType (pointerId, &type))
        return;

    POINT raw {};
    if (type == PT_TOUCH)
    {
        POINTER_TOUCH_INFO ti {};
        if (! GetPointerTouchInfo (pointerId, &ti))
            return;
        raw = ti.pointerInfo.ptPixelLocationRaw;   // <-- die unprädizierte Koordinate
    }
    else
    {
        POINTER_INFO pi {};
        if (! GetPointerInfo (pointerId, &pi))
            return;
        raw = pi.ptPixelLocationRaw;
    }

    // Physische Screen-px -> logische Desktop-px (DPI-korrekt, multi-monitor),
    // dann -> lokal zur Trace-Referenz.
    const auto logicalGlobal = juce::Desktop::getInstance().getDisplays()
                                   .physicalToLogical (juce::Point<int> { raw.x, raw.y }).toFloat();
    const auto local = reference.getLocalPoint (nullptr, logicalGlobal);

    TouchSample s;
    s.x = local.x;
    s.y = local.y;
    s.contactId = static_cast<int> (pointerId);
    s.phase = phase;
    s.tSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    s.tag = SourceTag::RawPointer;
    sink.pushSample (s);
#else
    juce::ignoreUnused (pointerId, phase);
#endif
}

} // namespace touchlab
