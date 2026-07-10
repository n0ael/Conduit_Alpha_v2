#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_dsp/juce_dsp.h>

#include "Core/LinkClock.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/**
    Spektrum-Zubringer für die TouchLive-EQ-Anzeige (M5-Politur,
    docs/TouchLive.md §10k): „heimlich" mithören, ohne den Patch zu
    berühren — die FFT läuft komplett auf dem MESSAGE Thread (das
    Spektrum ist reine Anzeige, der Audio-Thread liefert höchstens
    Kopien ab).

    Quellen (SourceMode):
    - linkAudio: abonniert den ERSTEN verfügbaren Peer-Kanal der
      Link-Session (LinkClock::Source, Auto-Rebind bei ChannelsChanged).
      Der Link-Thread konvertiert int16→float mono und pusht in die
      SpscQueue — fürs UI-Spektrum ist der Link-Thread nicht RT-kritisch
      (anders als der Receive-MODUL-Pfad, docs/LinkAudio.md).
    - audioInput: der EngineProcessor pusht die rohen Hardware-Inputs
      (pushAudioBlock, RT-safe: nur memcpy/Mixdown, Drop bei voller Queue).

    SPSC-Disziplin: es pusht IMMER NUR EINE Quelle (Umschalten stoppt
    erst die alte Quelle, dann leert der Message Thread die Queue).

    Averaging = exponentielle Glättung der dB-Bins (der „Avg"-Wunsch des
    Users — LOKAL, bewusst nicht an Lives Analyzer-Regler gebunden).

    LinkClock IMMER als WeakReference halten (Rule linkaudio).
*/
class LiveSpectrumTap final : private juce::Timer,
                              private juce::ChangeListener
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;       // 2048
    static constexpr int numBins = fftSize / 2;

    explicit LiveSpectrumTap (LinkClock* clockToUse);
    ~LiveSpectrumTap() override;

    //==========================================================================
    // Message Thread

    enum class SourceMode { off, linkAudio, audioInput };

    void setMode (SourceMode newMode);
    [[nodiscard]] SourceMode getMode() const noexcept { return mode; }

    /** Anzeige-Label der aktiven Quelle („Peer · Kanal" / „Input" / leer). */
    [[nodiscard]] juce::String getSourceLabel() const;

    /** 0 = roh, 1 = maximal träge (exponentielle dB-Glättung). */
    void setAveraging (double amount01);
    [[nodiscard]] double getAveraging() const noexcept { return averaging; }

    /** Geglättete Magnituden in dBFS (−90-Floor); Index → Frequenz über
        binToHz(). revision zählt bei jedem Analyse-Schritt hoch. */
    [[nodiscard]] const std::array<float, numBins>& getMagnitudesDb() const noexcept
    {
        return magnitudesDb;
    }

    [[nodiscard]] juce::uint32 getRevision() const noexcept { return revision; }
    [[nodiscard]] double binToHz (int bin) const noexcept;

    /** true, wenn in den letzten ~600 ms Daten ankamen. */
    [[nodiscard]] bool isReceiving() const noexcept;

    /** [Tests] Ein Analyse-Schritt sofort (der 30-Hz-Timer ruft dasselbe). */
    void analyseNow();

    //==========================================================================
    // Audio Thread (nur Mode audioInput)

    /** RT-safe: mischt die ersten beiden Kanäle mono und pusht Chunks;
        volle Queue → Drop (Anzeige verliert nur Frames). */
    void pushAudioBlock (const float* const* channels, int numChannels,
                         int numSamples) noexcept;

    void setAudioSampleRate (double newSampleRate) noexcept;

private:
    //==========================================================================
    struct Chunk
    {
        static constexpr int maxSamples = 512;
        float samples[maxSamples];
        int numSamples = 0;
        float sampleRate = 48000.0f;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void bindFirstAvailableChannel();
    void drainQueueInto (bool discard);

    juce::WeakReference<LinkClock> linkClock;   // Rule linkaudio
    std::unique_ptr<LinkClock::Source> linkSource;
    juce::String sourceLabel;

    SourceMode mode = SourceMode::off;
    double averaging = 0.5;

    SpscQueue<Chunk> queue { 64 };
    std::atomic<bool> inputTapEnabled { false };
    std::atomic<float> audioSampleRate { 48000.0f };

    // Analyse-Zustand (nur Message Thread)
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window {
        fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> ring {};
    int ringWrite = 0;
    int ringFill = 0;
    int samplesSinceAnalysis = 0;
    double analysisSampleRate = 48000.0;
    std::array<float, fftSize * 2> fftScratch {};
    std::array<float, numBins> magnitudesDb {};
    juce::uint32 revision = 0;
    juce::uint32 lastDataMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveSpectrumTap)
};

} // namespace conduit
