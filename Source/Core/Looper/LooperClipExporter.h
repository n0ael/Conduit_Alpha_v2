#pragma once

#include "Core/Capture/CaptureService.h"
#include "LooperClip.h"

namespace conduit
{

//==============================================================================
/**
    Looper-Clip-Export der Save-Geste (M9): ein committeter Clip wird als
    sample-alignte BWF-Dateien (_l/_r) über die CaptureWriter-Pipeline
    geschrieben — Grundstein für Loop → Datei / Drag-to-DAW (Roadmap,
    selbst NICHT Teil dieses Meilensteins).

    Mechanik:
      - Der Job liest DIREKT aus dem Clip-Buffer (Content-Region hinter dem
        Lead-in) über die TrackSource-Seam des Writers —
        `ringCapacitySamples == 0` markiert die Quelle als eingefroren
        (vom Überholschutz ausgenommen, CaptureWriter-Doku).
      - `startPosition = commitStartSample`: die Datei trägt damit dieselbe
        Zeitreferenz (bext) wie Capture-Exports — Loop-Dateien liegen
        sample-align zu allen anderen Exports der Session.
      - `exportPins` halten den Clip am Leben, bis `releaseResources` auf
        dem Writer-Thread läuft (atomarer Decrement) — Delete/prepare
        während des Exports parken den Clip im Graveyard, freigegeben wird
        erst bei Pins == 0 (LooperBank::serviceMessageThread).

    Threading: makeJob/exportClip laufen auf dem Message Thread; die
    TrackSource-Reads laufen auf dem Writer-Thread (nur const-Zugriffe auf
    Buffer + konstante Felder des gepinnten Clips).
*/
class LooperClipExporter
{
public:
    /** Job für EINEN Clip bauen (pinnt den Clip; testbar mit eigenem
        CaptureWriter). baseName wird Track-Name der Dateien
        ("{baseName}_l"/"_r"). sampleRate des Jobs = Aufnahme-Rate. */
    [[nodiscard]] static CaptureWriter::Job makeJob (LooperClip& clip,
                                                     const juce::String& baseName,
                                                     double sampleRate);

    /** [Message Thread] Clip über die Service-Pipeline exportieren
        (Bit-Tiefe/Verzeichnis/Take vom Service, Report → Toast). */
    [[nodiscard]] static juce::Result exportClip (CaptureService& capture,
                                                  LooperClip& clip,
                                                  const juce::String& baseName,
                                                  double sampleRate);
};

} // namespace conduit
