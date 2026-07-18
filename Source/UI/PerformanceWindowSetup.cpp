#include "UI/PerformanceWindowSetup.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shobjidl.h>
#endif

namespace conduit
{

#if JUCE_WINDOWS

namespace
{
    // MICROSOFT_TABLETPENSERVICE_PROPERTY-Flags (wintab-frei, dokumentierte
    // Konstanten — Header-Verfügbarkeit schwankt je SDK)
    constexpr ULONG_PTR tabletDisablePressAndHold    = 0x00000001;
    constexpr ULONG_PTR tabletDisablePenTapFeedback  = 0x00000008;
    constexpr ULONG_PTR tabletDisablePenBarrelFeedback = 0x00000010;
    constexpr ULONG_PTR tabletDisableFlicks          = 0x00010000;

    void disableEdgeGestures (HWND hwnd)
    {
        // System.EdgeGesture.DisableTouchWhenFullscreen — PKEY manuell
        // (propkey.h-unabhängig): {32CE38B2-2C9A-41B1-9BC5-B3784394AA44}, 2
        IPropertyStore* store = nullptr;

        if (SUCCEEDED (SHGetPropertyStoreForWindow (hwnd, IID_PPV_ARGS (&store)))
            && store != nullptr)
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
    }

    void disableTouchFeedback (HWND hwnd)
    {
        // SetWindowFeedbackSetting dynamisch (Win8+) — FEEDBACK_TYPE 1..11
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
    }
} // namespace

void applyPerformanceTouchSetup (juce::Component& windowComponent)
{
    if (auto* peer = windowComponent.getPeer())
    {
        const auto hwnd = reinterpret_cast<HWND> (peer->getNativeHandle());

        if (hwnd == nullptr)
            return;

        disableEdgeGestures (hwnd);
        disableTouchFeedback (hwnd);

        // Press-and-Hold-Rechtsklick + Flicks aus — Touch-Drags auf dem
        // Canvas dürfen keine Kontextmenü-Gesten auslösen
        SetPropW (hwnd, L"MicrosoftTabletPenServiceProperty",
                  reinterpret_cast<HANDLE> (tabletDisablePressAndHold
                                            | tabletDisablePenTapFeedback
                                            | tabletDisablePenBarrelFeedback
                                            | tabletDisableFlicks));
    }
}

#else

void applyPerformanceTouchSetup (juce::Component&)
{
    // iOS (deferred edges) lebt im iOS-Fenster-Setup; LinkBox braucht
    // nichts — der Kiosk besitzt den Input komplett (ADR 008)
}

#endif

} // namespace conduit
