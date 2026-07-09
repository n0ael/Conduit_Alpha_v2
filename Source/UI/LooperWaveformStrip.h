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

    Spektrum-View (S2): umschaltbares Spektrogramm (Fire-Palette) auf
    demselben Segment-Layout. Datenpfad: Spektral-Spalten des Taps landen
    in einem ring-adressierten Beat-Raum-Image (spectrumRingColumns breit,
    spectrumBands hoch, Spalte = index % Ringbreite, Band 63 = oben) plus
    Tag-Array; tick() schwärzt veraltete Spalten im sichtbaren Fenster
    (Startup/Queue-Lücken/Ring-Wrap). paint() blittet pro Segment den
    Beat-Bereich als skalierte drawImageTransformed-Züge (Ring-Wrap ⇒
    max. 2 Blits pro Segment, Sub-Spalten-genau — kein Pro-Pixel-Malen).
    Segment-Grenzen, Labels, Hover und Commit-Klick sind View-unabhängig.
*/
class LooperWaveformStrip final : public juce::Component
{
public:
    enum class View { waveform, spectrum };

    LooperWaveformStrip();

    /** [Editor] Ansicht umschalten (Persistenz: TransportSettings). */
    void setView (View viewToShow) noexcept
    {
        if (view != viewToShow)
        {
            view = viewToShow;
            repaint();
        }
    }

    [[nodiscard]] View getView() const noexcept { return view; }

    /** [Editor] Bin-Quelle (nicht owned — der EngineProcessor überlebt
        den Editor samt Strip). nullptr = Anzeige friert ein. */
    void setDataSource (LooperWaveformTap* tapToUse) noexcept { tap = tapToUse; }

    /** [Editor] Farbe der gewählten Quelle (Kanal-/Node-Farbe, 08.07.2026):
        färbt die Wellenform und tönt die Spektrum-Palette (schwarz → Farbe
        → hell). Transparent (= keine Farbe) stellt LED-Grün + Fire-Palette
        wieder her. Bereits gemalte Spektrum-Spalten behalten ihre Farbe —
        die History der alten Quelle bleibt visuell die alte. */
    void setSourceColour (juce::Colour colour);

    [[nodiscard]] juce::Colour getSourceColour() const noexcept { return sourceColour; }

    /** [Editor] Session-Beat für die rechte Kante (LinkClock, Message
        Thread) — ohne Callback steht der Strip. */
    std::function<double()> getBeatNow;

    /** Klick auf Segment → Commit-Länge in Takten (8/4/2/1). */
    std::function<void (int bars)> onSegmentClicked;

    /** [Editor, direkt nach dem Commit] Die committeten Takte als „Tinte
        auf transparent"-Bild der AKTUELLEN View (User-Idee 09.07.2026):
        Waveform als schwarze Min/Max-Spalten, Spektrum als Schwarz mit
        Intensitäts-Alpha (Helligkeit der LUT-Pixel als Pegel-Proxy — die
        Paletten sind monoton hell). Die Slot-Zelle legt das Bild auf ihre
        Quellfarben-Fläche — die Strip-Optik invertiert. Zeitnah rufen:
        History-Ring und Spektrum-Ring halten nur ~16 Takte. */
    [[nodiscard]] juce::Image renderCommitThumbnail (double startBeat, double endBeat,
                                                     int width, int height) const;

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

    /** Spektral-Spalte direkt ins Ring-Image schreiben (identischer Pfad
        wie pullBins) — Tests ohne Audio-Rig. */
    void ingestSpectrumForTest (const LooperWaveformTap::SpectralColumn& column)
    {
        juce::Image::BitmapData pixels { spectrumImage, juce::Image::BitmapData::writeOnly };
        storeSpectrum (column, pixels);
    }

    /** Aggregat einer Pixelspalte über den History-Ring: false, wenn kein
        einziger Bin des Spalten-Bereichs vorliegt. */
    [[nodiscard]] bool aggregateColumn (int x, double beatAtRightEdge,
                                        float& minOut, float& maxOut) const;

    void setBeatNowForTest (double beat) noexcept { beatNow = beat; }

    /** Stale-Clear des sichtbaren Fensters (der tick()-Schritt). */
    void clearStaleSpectrumColumnsForTest() { clearStaleSpectrumColumns(); }

    [[nodiscard]] std::int64_t getSpectrumTagForTest (int slot) const
    {
        return spectrumTags[static_cast<std::size_t> (slot)];
    }

    [[nodiscard]] juce::Colour getSpectrumPixelForTest (int slot, int band) const
    {
        return spectrumImage.getPixelAt (slot, looper::spectrumBands - 1 - band);
    }

    static constexpr int spectrumRingColumns = 1024;  // > 512 sichtbare Spalten

private:
    void tick();  // VBlank: beatNow + Bins nachziehen, Stale-Clear, repaint
    void rebuildSpectrumLut();

    /** Min/Max-Aggregat der Bins im Beat-Bereich [beatLo, beatHi] —
        gemeinsamer Kern von aggregateColumn (gestauchte Strip-Achse) und
        renderCommitThumbnail (lineare Achse). */
    [[nodiscard]] bool aggregateBeatRange (double beatLo, double beatHi,
                                           float& minOut, float& maxOut) const;
    void store (const LooperWaveformTap::Bin& bin);
    void storeSpectrum (const LooperWaveformTap::SpectralColumn& column,
                        juce::Image::BitmapData& pixels);
    void clearStaleSpectrumColumns();
    void paintWaveform (juce::Graphics& g, juce::Rectangle<float> wave);
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> wave);

    struct Entry
    {
        std::int64_t index = -1;  // −1 = leer; sonst Bin-Index (Tag wie BarSampleAnchors)
        float minValue = 0.0f;
        float maxValue = 0.0f;
    };

    static constexpr int historySize = 2048;  // > 8 Takte × 32 Beats-Bins (1024)
    static constexpr float labelRowHeight = 22.0f;

    std::array<Entry, static_cast<std::size_t> (historySize)> history {};

    // Spektrum-View: Beat-Raum-Image (Ringspalte = index % Ringbreite) +
    // Tags; die Farb-LUT (Fire-Palette) wird im Ctor vorberechnet
    juce::Image spectrumImage { juce::Image::ARGB, spectrumRingColumns,
                                looper::spectrumBands, true };
    std::array<std::int64_t, static_cast<std::size_t> (spectrumRingColumns)> spectrumTags;
    std::array<juce::Colour, 256> spectrumLut;

    LooperWaveformTap* tap = nullptr;
    View view = View::waveform;
    double beatNow = 0.0;
    int hoveredSegment = -1;
    juce::Colour sourceColour;   // transparent = Default (LED-Grün / Fire)

    juce::VBlankAttachment vblank { this, [this] { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperWaveformStrip)
};

} // namespace conduit
