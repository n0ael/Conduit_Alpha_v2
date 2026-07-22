#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shobjidl.h>
#endif

namespace touchlab
{

//==============================================================================
/**
    Schlanke Kopie von Conduits applyPerformanceTouchSetup()
    (Source/UI/PerformanceWindowSetup.cpp): Edge-Gesten, Touch-Feedback,
    Press-&-Hold-Rechtsklick und Flicks aus. NÖTIG für den NATIV-Arm der
    Probe, damit er Conduits *echtes* Verhalten misst (Conduit ruft dasselbe
    nach setVisible). Ohne diese Parität vergliche man Äpfel mit Birnen.

    Nach setVisible aufrufen — der native Peer muss existieren.
*/
inline void applyTouchSuppression (juce::Component& windowComponent)
{
#if JUCE_WINDOWS
    auto* peer = windowComponent.getPeer();
    if (peer == nullptr)
        return;

    const auto hwnd = reinterpret_cast<HWND> (peer->getNativeHandle());
    if (hwnd == nullptr)
        return;

    // Edge-Gesten (PKEY manuell, propkey.h-unabhängig)
    IPropertyStore* store = nullptr;
    if (SUCCEEDED (SHGetPropertyStoreForWindow (hwnd, IID_PPV_ARGS (&store))) && store != nullptr)
    {
        PROPERTYKEY key;
        key.fmtid = { 0x32CE38B2, 0x2C9A, 0x41B1,
                      { 0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44 } };
        key.pid = 2;

        PROPVARIANT value;
        value.vt = VT_BOOL;
        value.boolVal = VARIANT_TRUE;

        store->SetValue (key, value);
        store->Commit();
        store->Release();
    }

    // Touch-Feedback (Win8+, dynamisch geladen)
    using SetFeedbackFn = BOOL (WINAPI*) (HWND, int, DWORD, UINT32, const void*);
    if (auto* user32 = GetModuleHandleW (L"user32.dll"))
    {
        if (auto setFeedback = reinterpret_cast<SetFeedbackFn> (
                reinterpret_cast<void*> (GetProcAddress (user32, "SetWindowFeedbackSetting"))))
        {
            const BOOL enabled = FALSE;
            for (int feedbackType = 1; feedbackType <= 11; ++feedbackType)
                setFeedback (hwnd, feedbackType, 0, sizeof (enabled), &enabled);
        }
    }

    // Press-&-Hold + Flicks (MICROSOFT_TABLETPENSERVICE_PROPERTY-Flags)
    constexpr ULONG_PTR disablePressAndHold      = 0x00000001;
    constexpr ULONG_PTR disablePenTapFeedback    = 0x00000008;
    constexpr ULONG_PTR disablePenBarrelFeedback = 0x00000010;
    constexpr ULONG_PTR disableFlicks            = 0x00010000;

    SetPropW (hwnd, L"MicrosoftTabletPenServiceProperty",
              reinterpret_cast<HANDLE> (disablePressAndHold | disablePenTapFeedback
                                        | disablePenBarrelFeedback | disableFlicks));
#else
    juce::ignoreUnused (windowComponent);
#endif
}

} // namespace touchlab
