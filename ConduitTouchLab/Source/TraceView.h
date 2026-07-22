#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchSample.h"

namespace touchlab
{

//==============================================================================
/**
    Prototyp des späteren Conduit-Diagnose-Panels. Überlagert Rohpunkte
    (Nativ + Raw-Pointer, farbcodiert) und die verarbeitete Spur (nach
    Filterkette). Lokaler Ringpuffer je Spur — Vorbild Conduit
    Source/UI/ScopeDisplay: paint() liest NUR die lokalen Puffer, blockiert
    nie. Refresh treibt der FramePacer im Hub (repaint nur bei Änderung).
*/
class TraceView final : public juce::Component
{
public:
    enum class Lane { NativeRaw = 0, RawRaw, NativeFiltered, RawFiltered };

    TraceView();

    /** startsStroke = true beim Phase::Down (unterbricht die gezeichnete Linie). */
    void addPoint (Lane lane, juce::Point<float> p, bool startsStroke);
    void clearTrails();

    void setBlindMode (bool active) { blindActive = active; }

    /** Vom Hub-FramePacer gerufen: repaint nur wenn seit dem letzten Frame
        neue Punkte kamen. */
    void refreshIfDirty();

    void paint (juce::Graphics& g) override;

private:
    struct Pt { float x = 0.0f, y = 0.0f; bool startsStroke = false; };

    static constexpr int cap = 8192;

    struct LaneBuf
    {
        std::vector<Pt> pts { (size_t) cap };
        int  writeIndex = 0;
        bool filled = false;
    };

    void drawDots (juce::Graphics& g, const LaneBuf& buf, juce::Colour c) const;
    void drawLine (juce::Graphics& g, const LaneBuf& buf, juce::Colour c) const;

    LaneBuf lanes[4];
    bool blindActive = false;
    bool dirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TraceView)
};

} // namespace touchlab
