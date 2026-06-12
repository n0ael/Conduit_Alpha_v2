#pragma once

#include <juce_core/juce_core.h>

namespace conduit
{

class CaptureService;

//==============================================================================
/**
    Mixin-Interface (CLAUDE.md 4.2-Stil): Module, die virtuelle Capture-
    Kanäle beim CaptureService halten (CaptureTapModule). Alle Methoden
    laufen auf dem Message Thread — Muster ILinkAudioClient.

    Der GraphManager injiziert den Kontext bei der Materialisierung
    (5.2 Schritt 1) — VOR prepareForGraph(), denn die Kanal-Registrierung
    passiert dort und braucht Service + moduleId (Spurname == moduleId).
*/
class ICaptureTapClient
{
public:
    virtual ~ICaptureTapClient() = default;

    /** Message Thread, vor prepareForGraph(). service darf nullptr sein
        (Tests, kein Capture) — das Modul bleibt dann reines Pass-Through. */
    virtual void setCaptureTapContext (CaptureService* service,
                                       const juce::String& initialModuleId) = 0;

    /** Rename der named_id (renameNode, auch via Undo) — der Spurname der
        virtuellen Kanäle folgt live (setVirtualChannelName). Message Thread. */
    virtual void captureModuleIdRenamed (const juce::String& newModuleId) = 0;

    /** Phase 1 des zweiphasigen Deletes (5.3, Pattern OscController):
        Schreibpfad SOFORT trennen und Kanäle deregistrieren — laufendes
        Material bleibt beim Service als "held" erhalten. Message Thread. */
    virtual void releaseCaptureResources() = 0;
};

} // namespace conduit
