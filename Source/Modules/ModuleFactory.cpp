#include "ModuleFactory.h"

#include "DSP/Airwindows/AirwindowsRegistry.h"

#include "AirwindowsDensityModule.h"
#include "AirwindowsSlewModule.h"
#include "AirwindowsSpiralModule.h"
#include "AirwindowsAir4Module.h"
#include "AirwindowsCansModule.h"
#include "AirwindowsCansAWModule.h"
#include "AirwindowsConsole0BussModule.h"
#include "AirwindowsConsole0ChannelModule.h"
#include "AirwindowsConsoleLABussModule.h"
#include "AirwindowsConsoleMCBussModule.h"
#include "AirwindowsDeBessModule.h"
#include "AirwindowsDeBezModule.h"
#include "AirwindowsDeRez3Module.h"
#include "AirwindowsDigitalBlackModule.h"
#include "AirwindowsDiscontapeityModule.h"
#include "AirwindowsDistance3Module.h"
#include "AirwindowsDubSub2Module.h"
#include "AirwindowsDubly3Module.h"
#include "AirwindowsFatEQModule.h"
#include "AirwindowsFlutter2Module.h"
#include "AirwindowsGatelopeModule.h"
#include "AirwindowsGlitchShifterModule.h"
#include "AirwindowsHypersoftModule.h"
#include "AirwindowsInflamerModule.h"
#include "AirwindowsIsolator3Module.h"
#include "AirwindowsMackityModule.h"
#include "AirwindowsOneCornerClipModule.h"
#include "AirwindowsParametricModule.h"
#include "AirwindowsPearEQModule.h"
#include "AirwindowsPockey2Module.h"
#include "AirwindowsPointyGuitarModule.h"
#include "AirwindowsPop2Module.h"
#include "AirwindowsSilkenModule.h"
#include "AirwindowsSingleEndedTriodeModule.h"
#include "AirwindowsSmoothModule.h"
#include "AirwindowsSmoothEQ3Module.h"
#include "AirwindowsSoftGateModule.h"
#include "AirwindowsStoneFireCompModule.h"
#include "AirwindowsStonefireModule.h"
#include "AirwindowsSweetenModule.h"
#include "AirwindowsTakeCareModule.h"
#include "AirwindowsTapeDelay2Module.h"
#include "AirwindowsTapeDustModule.h"
#include "AirwindowsTapeHack2Module.h"
#include "AirwindowsToneSlantModule.h"
#include "AirwindowsTremoSquareModule.h"
#include "AirwindowsTrianglizerModule.h"
#include "AirwindowsTube2Module.h"
#include "AirwindowsVibratoModule.h"
#include "AirwindowsWeightModule.h"
#include "AirwindowsWiderModule.h"
#include "AirwindowsChamberModule.h"
#include "AirwindowsGalacticModule.h"
#include "AirwindowsVerbTinyModule.h"
#include "AirwindowsKBeyondModule.h"
#include "AirwindowsKCathedral5Module.h"
#include "AirwindowsKWoodRoomModule.h"
#include "AttenuatorModule.h"
#include "AudioEndpointModule.h"
#include "CaptureTapModule.h"
#include "LfoModule.h"
#include "LinkAudioReceiveModule.h"
#include "LinkAudioSendModule.h"
#include "LooperPatchOutModule.h"
#include "LooperPatchInModule.h"
#include "ScopeModule.h"
#include "StepSequencerModule.h"

namespace conduit
{

void ModuleFactory::registerModule (ModuleDescriptor descriptor, Creator creator)
{
    jassert (creator != nullptr);
    jassert (descriptor.id.isNotEmpty());
    jassert (descriptor.displayName.isNotEmpty());
    jassert (descriptor.category.isNotEmpty());

    const auto key = descriptor.id;
    entries[key] = { std::move (descriptor), std::move (creator) };
}

bool ModuleFactory::isRegistered (const juce::String& moduleId) const
{
    return entries.contains (moduleId);
}

std::unique_ptr<ConduitModule> ModuleFactory::create (const juce::String& moduleId) const
{
    if (const auto it = entries.find (moduleId); it != entries.end())
        return (it->second.creator)();

    return nullptr;
}

std::vector<ModuleDescriptor> ModuleFactory::getDescriptors() const
{
    std::vector<ModuleDescriptor> descriptors;
    descriptors.reserve (entries.size());

    for (const auto& [key, entry] : entries)
        descriptors.push_back (entry.descriptor);

    std::sort (descriptors.begin(), descriptors.end(),
               [] (const ModuleDescriptor& a, const ModuleDescriptor& b)
               { return a.displayName.compareIgnoreCase (b.displayName) < 0; });

    return descriptors;
}

ModuleDescriptor ModuleFactory::getDescriptor (const juce::String& moduleId) const
{
    if (const auto it = entries.find (moduleId); it != entries.end())
        return it->second.descriptor;

    return {};
}

//==============================================================================
namespace
{
    /** Descriptor eines CV/Control-Moduls (Handschrift pro Modul). */
    ModuleDescriptor cvDescriptor (const char* factoryKey, const char* displayName,
                                   const char* category, const char* tags)
    {
        ModuleDescriptor descriptor;
        descriptor.id          = factoryKey;
        descriptor.displayName = displayName;
        descriptor.branch      = ModuleDescriptor::Branch::cvControl;
        descriptor.category    = category;
        descriptor.tags.addTokens (juce::String (tags), " ", {});
        return descriptor;
    }

    /** Descriptor eines Airwindows-Wrappers — Name/Kategorie/Tags kommen
        aus der AirwindowsRegistry (Single Source, nie duplizieren). */
    ModuleDescriptor airwindowsDescriptor (const char* staticModuleId)
    {
        const juce::String factoryKey (staticModuleId);
        const auto pluginId = factoryKey.fromFirstOccurrenceOf ("airwindows_", false, false);

        for (const auto& entry : airwindows::getRegisteredPlugins())
        {
            if (pluginId == entry.id)
            {
                ModuleDescriptor descriptor;
                descriptor.id          = factoryKey;
                descriptor.displayName = entry.name;
                descriptor.branch      = ModuleDescriptor::Branch::audioFx;
                descriptor.category    = entry.category;
                descriptor.tags.addTokens (juce::String (entry.tags), " ", {});
                return descriptor;
            }
        }

        // Wrapper ohne Registry-Eintrag — darf nicht vorkommen (Test deckt ab)
        jassertfalse;
        ModuleDescriptor descriptor;
        descriptor.id          = factoryKey;
        descriptor.displayName = factoryKey;
        descriptor.branch      = ModuleDescriptor::Branch::audioFx;
        descriptor.category    = "Utility";
        return descriptor;
    }

    template <typename ModuleType>
    void registerAirwindows (ModuleFactory& factory)
    {
        factory.registerModule (airwindowsDescriptor (ModuleType::staticModuleId),
                                [] { return std::make_unique<ModuleType>(); });
    }
} // namespace

//==============================================================================
void registerDefaultModules (ModuleFactory& factory)
{
    // Hardware-I/O als reguläre Browser-Module (ADR 009) — Mehrfach-
    // Instanzen erlaubt, der Graph summiert auf denselben Pin nativ
    factory.registerModule (
        cvDescriptor (audioInputModuleId, "Audio-Eingang", "I/O",
                      "audio input eingang hardware interface kanal"),
        [] { return std::make_unique<AudioEndpointModule> (AudioEndpointModule::Direction::input); });
    factory.registerModule (
        cvDescriptor (audioOutputModuleId, "Audio-Ausgang", "I/O",
                      "audio output ausgang hardware interface kanal master"),
        [] { return std::make_unique<AudioEndpointModule> (AudioEndpointModule::Direction::output); });

    factory.registerModule (
        cvDescriptor (AttenuatorModule::staticModuleId, "Attenuator", "Utility",
                      "attenuator gain level cv abschwaecher"),
        [] { return std::make_unique<AttenuatorModule>(); });
    factory.registerModule (
        cvDescriptor (LfoModule::staticModuleId, "LFO", "LFO",
                      "lfo oscillator modulation rate sine"),
        [] { return std::make_unique<LfoModule>(); });
    factory.registerModule (
        cvDescriptor (ScopeModule::staticModuleId, "Scope", "Analyse",
                      "scope oscilloscope analyse waveform visual"),
        [] { return std::make_unique<ScopeModule>(); });
    factory.registerModule (
        cvDescriptor (StepSequencerModule::staticModuleId, "Sequencer", "Sequencer",
                      "sequencer steps cv gate pattern clock"),
        [] { return std::make_unique<StepSequencerModule>(); });
    factory.registerModule (
        cvDescriptor (LinkAudioSendModule::staticModuleId, "Link Send", "I/O",
                      "link audio send network ableton stream"),
        [] { return std::make_unique<LinkAudioSendModule>(); });
    factory.registerModule (
        cvDescriptor (LinkAudioReceiveModule::staticModuleId, "Link Receive", "I/O",
                      "link audio receive empfang network ableton stream"),
        [] { return std::make_unique<LinkAudioReceiveModule>(); });
    factory.registerModule (
        cvDescriptor (CaptureTapModule::staticModuleId, "Capture Tap", "Utility",
                      "capture tap record aufnahme export"),
        [] { return std::make_unique<CaptureTapModule>(); });
    factory.registerModule (
        cvDescriptor (LooperPatchInModule::staticModuleId, "Looper patch IN", "I/O",
                      "looper patch in eingang quelle source aufnahme record loop"),
        [] { return std::make_unique<LooperPatchInModule>(); });
    factory.registerModule (
        cvDescriptor (LooperPatchOutModule::staticModuleId, "Looper patch OUT", "I/O",
                      "looper patch out ausgang tracks sends bus master playback loop"),
        [] { return std::make_unique<LooperPatchOutModule>(); });

    registerAirwindows<AirwindowsDensityModule> (factory);
    registerAirwindows<AirwindowsSlewModule> (factory);
    registerAirwindows<AirwindowsSpiralModule> (factory);
    registerAirwindows<AirwindowsAir4Module> (factory);
    registerAirwindows<AirwindowsCansModule> (factory);
    registerAirwindows<AirwindowsCansAWModule> (factory);
    registerAirwindows<AirwindowsConsole0BussModule> (factory);
    registerAirwindows<AirwindowsConsole0ChannelModule> (factory);
    registerAirwindows<AirwindowsConsoleLABussModule> (factory);
    registerAirwindows<AirwindowsConsoleMCBussModule> (factory);
    registerAirwindows<AirwindowsDeBessModule> (factory);
    registerAirwindows<AirwindowsDeBezModule> (factory);
    registerAirwindows<AirwindowsDeRez3Module> (factory);
    registerAirwindows<AirwindowsDigitalBlackModule> (factory);
    registerAirwindows<AirwindowsDiscontapeityModule> (factory);
    registerAirwindows<AirwindowsDistance3Module> (factory);
    registerAirwindows<AirwindowsDubSub2Module> (factory);
    registerAirwindows<AirwindowsDubly3Module> (factory);
    registerAirwindows<AirwindowsFatEQModule> (factory);
    registerAirwindows<AirwindowsFlutter2Module> (factory);
    registerAirwindows<AirwindowsGatelopeModule> (factory);
    registerAirwindows<AirwindowsGlitchShifterModule> (factory);
    registerAirwindows<AirwindowsHypersoftModule> (factory);
    registerAirwindows<AirwindowsInflamerModule> (factory);
    registerAirwindows<AirwindowsIsolator3Module> (factory);
    registerAirwindows<AirwindowsMackityModule> (factory);
    registerAirwindows<AirwindowsOneCornerClipModule> (factory);
    registerAirwindows<AirwindowsParametricModule> (factory);
    registerAirwindows<AirwindowsPearEQModule> (factory);
    registerAirwindows<AirwindowsPockey2Module> (factory);
    registerAirwindows<AirwindowsPointyGuitarModule> (factory);
    registerAirwindows<AirwindowsPop2Module> (factory);
    registerAirwindows<AirwindowsSilkenModule> (factory);
    registerAirwindows<AirwindowsSingleEndedTriodeModule> (factory);
    registerAirwindows<AirwindowsSmoothModule> (factory);
    registerAirwindows<AirwindowsSmoothEQ3Module> (factory);
    registerAirwindows<AirwindowsSoftGateModule> (factory);
    registerAirwindows<AirwindowsStoneFireCompModule> (factory);
    registerAirwindows<AirwindowsStonefireModule> (factory);
    registerAirwindows<AirwindowsSweetenModule> (factory);
    registerAirwindows<AirwindowsTakeCareModule> (factory);
    registerAirwindows<AirwindowsTapeDelay2Module> (factory);
    registerAirwindows<AirwindowsTapeDustModule> (factory);
    registerAirwindows<AirwindowsTapeHack2Module> (factory);
    registerAirwindows<AirwindowsToneSlantModule> (factory);
    registerAirwindows<AirwindowsTremoSquareModule> (factory);
    registerAirwindows<AirwindowsTrianglizerModule> (factory);
    registerAirwindows<AirwindowsTube2Module> (factory);
    registerAirwindows<AirwindowsVibratoModule> (factory);
    registerAirwindows<AirwindowsWeightModule> (factory);
    registerAirwindows<AirwindowsWiderModule> (factory);
    registerAirwindows<AirwindowsChamberModule> (factory);
    registerAirwindows<AirwindowsGalacticModule> (factory);
    registerAirwindows<AirwindowsVerbTinyModule> (factory);
    registerAirwindows<AirwindowsKBeyondModule> (factory);
    registerAirwindows<AirwindowsKCathedral5Module> (factory);
    registerAirwindows<AirwindowsKWoodRoomModule> (factory);
}

} // namespace conduit
