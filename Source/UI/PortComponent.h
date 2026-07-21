#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DragCursorHider.h"

namespace conduit
{

//==============================================================================
/** Identität eines Ports — reicht zusammen mit dem Tree, um ein Kabel
    (Connection, Schema 6.2) zu beschreiben.

    span == 2: Stereo-Paar-Port (Kanäle channel, channel+1) — ein Drag erzeugt
    zwei Connections (GraphManager::addConnectionPair), die als Doppel-Linie
    am selben Port ankommen. Das Pairing selbst lebt in ChannelNames. */
struct PortInfo
{
    juce::String nodeUuid;
    bool isInput = false;
    int channel = 0;
    int span = 1;
};

//==============================================================================
/**
    Ein Anschlusspunkt am Rand einer Node-Kachel. Leitet Kabel-Gesten
    (Drag von Port zu Port) an den NodeCanvas weiter — selbst zustandslos,
    kennt nur seine PortInfo.

    Die Component ist 24×24px (Hit-Zone), gezeichnet wird ein kleinerer
    Kreis; der Canvas erweitert die Treffer-Toleranz beim Drop zusätzlich
    auf Touch-Maß (CLAUDE.md 10).

    SettableTooltipClient: an den I/O-Endpunkt-Nodes setzt die NodeComponent
    das ChannelNames-Label als Tooltip (Maus-Hover; Touch sieht stattdessen
    die in die Kachel gemalten Labels).
*/
class PortComponent final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    explicit PortComponent (PortInfo portInfo);
    ~PortComponent() override { cursorHider.end(); }

    static constexpr int hitSize = 24;

    // Halten vor der ersten Bewegung länger als dies → Einzel-(Mono-)Kabel
    // trotz Stereo-Quelle (Dwell-Geste, User 05.07.).
    static constexpr int dwellMs = 400;

    [[nodiscard]] const PortInfo& getInfo() const noexcept { return info; }

    /** Signalfarbe des Steckers (Kabelfarbe des Kanals; neutral = unverbunden). */
    void setSignalColour (juce::Colour newColour);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    const PortInfo info;

    juce::Colour signalColour { 0xff5a6170 };  // neutral (unverbunden)

    // Dwell-Erkennung: Zeitpunkt des Drückens; erste Bewegung entscheidet
    juce::int64 pointerDownMs = 0;
    bool dragResolved = false;

    // Kabelziehen: Cursor wird zum Fadenkreuz „+" (zielt aufs Port)
    ui::DragCursorHider cursorHider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PortComponent)
};

} // namespace conduit
