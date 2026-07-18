#pragma once

#include "Interfaces/IExternalAudioEndpoint.h"
#include "Modules/ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Hardware-Audio-I/O als reguläres Browser-Modul (ADR 009): ersetzt die
    reservierten moduleIds — Factory-Materialisierung, createState(),
    voller Delete-Pfad, Mehrfach-Instanzen (der Graph summiert mehrere
    Connections auf denselben Pin nativ).

    DSP: reines Pass-Through (leerer processBlock — der Graph liefert den
    Input im Buffer, der Output IST der Buffer). Die Anbindung an den
    AudioGraphIOProcessor des EngineProcessor übernimmt der GraphManager
    implizit über IExternalAudioEndpoint (Anker-Kabel, kein Patch-Zustand).

    Tree-Schema unverändert (6.2): audio_input-Nodes tragen 0 Eingangs- /
    N Ausgangs-PORTS (die Hardware LIEFERT Kanäle), audio_output umgekehrt
    — die Graph-Busse sind dagegen N/N (Pass-Through). Die factoryIds
    bleiben "audio_input"/"audio_output", damit UI-Sonderausstattung
    (Meter, ChannelNames-Labels, Pairing, Send-Toggles, Kabel-Quellfarben)
    und Bestandspatches unverändert funktionieren.

    Kanalzahl folgt der Hardware (EngineProcessor::syncHardwareIOChannels);
    eine Kanalzahl-Änderung re-materialisiert das Modul (GraphManager).
*/
class AudioEndpointModule final : public ConduitModule,
                                  public IExternalAudioEndpoint
{
public:
    enum class Direction { input, output };

    explicit AudioEndpointModule (Direction directionToUse);

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] ModuleType getType() const override;
    [[nodiscard]] int getStateVersion() const override;

    /** Basis-Skelett + Port-Sicht des Schemas 6.2: input → 0/N, output → N/0. */
    [[nodiscard]] juce::ValueTree createState() override;

    //==========================================================================
    // IExternalAudioEndpoint [Message Thread, vor prepareForGraph]
    [[nodiscard]] bool isInputEndpoint() const noexcept override;
    void setEndpointChannels (int numChannels) override;
    [[nodiscard]] int getEndpointChannels() const noexcept override;

    [[nodiscard]] juce::Result prepareForGraph (double sampleRate, int maximumBlockSize) override;

    //==========================================================================
    // AudioProcessor — Pass-Through
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

private:
    const Direction direction;
    int channels = 2;   // Message Thread; fix ab prepareForGraph

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEndpointModule)
};

} // namespace conduit
