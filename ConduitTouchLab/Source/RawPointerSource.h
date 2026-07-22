#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchSample.h"

namespace touchlab
{

//==============================================================================
/**
    RAW-Arm (#if JUCE_WINDOWS): hängt sich per SetWindowSubclass an die
    WndProc des Fenster-HWND und behandelt WM_POINTERDOWN/UPDATE/UP. Liest
    aus GetPointerTouchInfo die UNPRÄDIZIERTE Digitizer-Koordinate
    POINTER_INFO.ptPixelLocationRaw (WPF nutzt exakt diese) und mappt sie in
    die lokalen Koordinaten der Referenz-Component (Trace-Fläche).

    HWND-Bezug: window.getPeer()->getNativeHandle() — Vorbild Conduit
    PerformanceWindowSetup.cpp. Auf nicht-Windows ein No-Op-Stub.

    WM_TOUCH-Falle: Registriert JUCE das Fenster für WM_TOUCH, liefert
    Windows KEINE WM_POINTER-Nachrichten (die beiden schließen sich aus).
    Bleibt der Raw-Arm leer, setForcePointerMode(true) aufrufen — meldet
    WM_TOUCH ab, sodass der Pointer-Stack wieder zustellt (Single-Finger-
    Native läuft dann über Maus-Promotion weiter, genau Leons Fahr-Fall).
*/
class RawPointerSource
{
public:
    RawPointerSource (juce::Component& windowComponent,
                      juce::Component& referenceComponent,
                      TouchSink& sinkToUse);
    ~RawPointerSource();

    /** Nach setVisible aufrufen (Peer/HWND muss existieren). */
    void attach();

    /** true = WM_TOUCH abmelden, damit WM_POINTER zugestellt wird. */
    void setForcePointerMode (bool shouldForce);

    [[nodiscard]] bool isAttached() const noexcept { return attached; }

    /** Von der (plattformspezifischen) WndProc gerufen — public, weil der
        C-Callback keinen Member-Zugriff hat. */
    void handlePointer (unsigned int pointerId, Phase phase);

private:
    void detach();

    juce::Component& window;
    juce::Component& reference;
    TouchSink& sink;
    bool attached = false;
    void* hwnd = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RawPointerSource)
};

} // namespace touchlab
