#pragma once

#include <memory>
#include <vector>

#include "DSP/Airwindows/AirwindowsPlugin.h"
#include "ProcessorModule.h"

namespace conduit
{

//==============================================================================
/**
    Generischer Wrapper für portierte Airwindows-Effekte (Source/DSP/Airwindows).

    Kein Template — die konkrete DSP-Instanz ist bereits polymorph
    (airwindows::AirwindowsPlugin), der Wrapper arbeitet rein über
    getNumParameters()/getParameterInfo() generisch. Konkrete Subklassen
    (AirwindowsDensityModule etc.) reichen nur eine fertige Plugin-Instanz
    plus moduleId/Displayname durch — sonst identisch.

    Schema, Echtzeit-Ziele, I/O-Gains, Meter, CV-Eingänge und Link-Send
    kommen vollständig aus dem FX-Chassis (ProcessorModule, CLAUDE.md 4.6) —
    hier lebt NUR noch die Airwindows-DSP:

    Bewusst KEIN SmoothedValue auf den DSP-Parametern: AirwindowsPlugin::
    process() snapshottet Parameter bereits selbst einmal pro Block
    (block-konstant, exakt wie beim VST-Original — siehe AirwindowsPlugin.h);
    das Chassis liefert mit effectiveParam(i) ebenfalls blockkonstante Werte.

    Bus: Audio fest stereo (2 in / 2 out) + ein CV-Eingang je Parameter
    (Chassis-Layout).
*/
class AirwindowsProcessorModule : public ProcessorModule
{
public:
    AirwindowsProcessorModule (std::unique_ptr<airwindows::AirwindowsPlugin> pluginToUse,
                              juce::String moduleIdToUse,
                              juce::String displayNameToUse);

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4) — für alle Subklassen fertig implementiert
    [[nodiscard]] juce::String getModuleId() const override;
    [[nodiscard]] juce::String getModuleDisplayName() const override;
    [[nodiscard]] int getStateVersion() const override;

protected:
    //==========================================================================
    // Chassis-Hooks (Audio-Sicht: Kanäle 0..1)
    void prepareCore (double sampleRate, int maximumBlockSize) override;
    void processCore (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midiMessages) override;

private:
    [[nodiscard]] static std::vector<ChassisParamDesc> makeDescs (const airwindows::AirwindowsPlugin& plugin);

    std::unique_ptr<airwindows::AirwindowsPlugin> plugin;
    juce::String moduleIdString;
    juce::String displayNameString;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AirwindowsProcessorModule)
};

} // namespace conduit
