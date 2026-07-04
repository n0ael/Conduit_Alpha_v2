#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Looper/LooperWaveformTap.h"

namespace conduit
{

//==============================================================================
/**
    Gestauchter 4-Segment-Wellenform-Strip des Retro-Loopers (Baustein B4) —
    das Endlesss-Kernstück: rechts läuft die Gegenwart ein, jedes linke
    Segment zeigt die doppelte Zeitspanne in gleicher Breite ("8 Bars |
    4 Bars | 2 Bars | 1 Bar", Mathe in LooperMath).

    Datenpfad: der LooperWaveformTap (Audio Thread) pusht beat-indizierte
    Min/Max-Bins in seine SPSC-Queue; der Strip zieht sie pro VBlank-Frame
    in einen lokalen History-Ring (KONSUMENTENROLLE EXKLUSIV — genau eine
    Strip-Instanz pro Tap, Muster ScopeDisplay) und malt pro Pixelspalte
    das Aggregat der getroffenen Bins. beatNow kommt pro Frame vom Editor-
    Callback (LinkClock::getBeatPosition, inkl. Clock-Offset) — die
    Wellenform gleitet framegenau, paint() blockiert nie (liest nur den
    lokalen Ring).

    Klick/Tap auf ein Segment meldet die Commit-Länge (8/4/2/1 Takte) über
    onSegmentClicked — der Commit selbst kommt in Baustein B5.
*/
class LooperWaveformStrip final : public juce::Component
{
public:
    LooperWaveformStrip();

    /** [Editor] Bin-Quelle (nicht owned — der EngineProcessor überlebt
        den Editor samt Strip). nullptr = Anzeige friert ein. */
    void setDataSource (LooperWaveformTap* tapToUse) noexcept { tap = tapToUse; }

    /** [Editor] Session-Beat für die rechte Kante (LinkClock, Message
        Thread) — ohne Callback steht der Strip. */
    std::function<double()> getBeatNow;

    /** Klick auf Segment → Commit-Länge in Takten (8/4/2/1). */
    std::function<void (int bars)> onSegmentClicked;

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    //==========================================================================
    // Test-Seams (der VBlank-Tick ruft dieselben Schritte)

    /** Queue → History-Ring (UI-Thread). */
    void pullBins();

    /** Direkt in den History-Ring schreiben (identischer Pfad wie
        pullBins) — Tests ohne Audio-Rig. */
    void ingestBinForTest (const LooperWaveformTap::Bin& bin) { store (bin); }

    /** Aggregat einer Pixelspalte über den History-Ring: false, wenn kein
        einziger Bin des Spalten-Bereichs vorliegt. */
    [[nodiscard]] bool aggregateColumn (int x, double beatAtRightEdge,
                                        float& minOut, float& maxOut) const;

    void setBeatNowForTest (double beat) noexcept { beatNow = beat; }

private:
    void tick();  // VBlank: beatNow + Bins nachziehen, repaint
    void store (const LooperWaveformTap::Bin& bin);

    struct Entry
    {
        std::int64_t index = -1;  // −1 = leer; sonst Bin-Index (Tag wie BarSampleAnchors)
        float minValue = 0.0f;
        float maxValue = 0.0f;
    };

    static constexpr int historySize = 2048;  // > 8 Takte × 32 Beats-Bins (1024)
    static constexpr float labelRowHeight = 22.0f;

    std::array<Entry, static_cast<std::size_t> (historySize)> history {};

    LooperWaveformTap* tap = nullptr;
    double beatNow = 0.0;
    int hoveredSegment = -1;

    juce::VBlankAttachment vblank { this, [this] { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperWaveformStrip)
};

} // namespace conduit
