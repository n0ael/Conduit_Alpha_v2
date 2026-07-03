#include <catch2/catch_test_macros.hpp>

#include "Modules/AirwindowsDensityModule.h"
#include "Modules/AirwindowsSlewModule.h"
#include "Modules/AirwindowsSpiralModule.h"
#include "Modules/ChassisSchema.h"
#include "Modules/ModuleFactory.h"

namespace
{

void fillWithRamp (juce::AudioBuffer<float>& buffer, float start)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = start + 0.001f * (float) i * (channel == 0 ? 1.0f : -1.0f);
    }
}

[[nodiscard]] bool hasNanOrInf (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return true;
    }

    return false;
}

} // namespace

[[nodiscard]] juce::StringArray dspParameterIdsOf (const juce::ValueTree& nodeTree)
{
    juce::StringArray ids;
    const auto parameters = nodeTree.getChildWithName (conduit::id::parameters);

    for (int i = 0; i < parameters.getNumChildren(); ++i)
    {
        const auto parameter = parameters.getChild (i);

        if (conduit::ChassisSchema::roleOf (parameter) == juce::String (conduit::ChassisSchema::roleDsp))
            ids.add (parameter.getProperty (conduit::id::paramId).toString());
    }

    return ids;
}

//==============================================================================
TEST_CASE ("AirwindowsDensityModule: createState liefert die 4 Airwindows-Parameter im Chassis", "[airwindows]")
{
    conduit::AirwindowsDensityModule density;
    const auto node = density.createState();

    // Chassis-Layout (4.6): 2 Audio-Eingänge + 1 CV-Kanal pro DSP-Parameter
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels)  == 6);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 2);

    // Gains + 4×(dsp, cv_amt)
    const auto parameters = node.getChildWithName (conduit::id::parameters);
    REQUIRE (parameters.getNumChildren() == 10);

    REQUIRE (dspParameterIdsOf (node) == juce::StringArray { "density", "highpass", "out_level", "dry_wet" });

    for (const auto& dspId : dspParameterIdsOf (node))
    {
        const auto parameter = parameters.getChildWithProperty (conduit::id::paramId, dspId);
        REQUIRE (juce::exactlyEqual ((double) parameter.getProperty (conduit::id::paramMin), 0.0));
        REQUIRE (juce::exactlyEqual ((double) parameter.getProperty (conduit::id::paramMax), 1.0));
    }
}

TEST_CASE ("AirwindowsSlewModule: createState liefert genau 1 DSP-Parameter (clamping)", "[airwindows]")
{
    conduit::AirwindowsSlewModule slew;
    const auto node = slew.createState();

    REQUIRE (dspParameterIdsOf (node) == juce::StringArray { "clamping" });
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 3);   // 2 Audio + 1 CV
}

TEST_CASE ("AirwindowsSpiralModule: createState liefert 0 DSP-Parameter, trotzdem stereo", "[airwindows]")
{
    conduit::AirwindowsSpiralModule spiral;
    const auto node = spiral.createState();

    // Ohne DSP-Parameter kein CV-Bus — reines Stereo-Modul mit Chassis-Gains
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels)  == 2);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 2);
    REQUIRE (dspParameterIdsOf (node).isEmpty());
    REQUIRE (node.getChildWithName (conduit::id::parameters).getNumChildren() == 2);   // nur Gains
}

//==============================================================================
TEST_CASE ("AirwindowsProcessorModule: getParameterTarget kennt nur eigene Ids", "[airwindows]")
{
    conduit::AirwindowsDensityModule density;

    REQUIRE (density.getParameterTarget ("density")   != nullptr);
    REQUIRE (density.getParameterTarget ("highpass")  != nullptr);
    REQUIRE (density.getParameterTarget ("out_level") != nullptr);
    REQUIRE (density.getParameterTarget ("dry_wet")   != nullptr);
    REQUIRE (density.getParameterTarget ("gain")      == nullptr);

    conduit::AirwindowsSlewModule slew;
    REQUIRE (slew.getParameterTarget ("clamping") != nullptr);
    REQUIRE (slew.getParameterTarget ("density")  == nullptr);

    conduit::AirwindowsSpiralModule spiral;
    REQUIRE (spiral.getParameterTarget ("anything") == nullptr);

    // Chassis-Ziele kommen bei ALLEN Processor-Modulen dazu (4.6)
    REQUIRE (density.getParameterTarget ("input_gain")      != nullptr);
    REQUIRE (density.getParameterTarget ("output_gain")     != nullptr);
    REQUIRE (density.getParameterTarget ("density_cv_amt")  != nullptr);
    REQUIRE (spiral.getParameterTarget ("input_gain")       != nullptr);
}

//==============================================================================
TEST_CASE ("AirwindowsProcessorModule: processBlock bleibt über einen Parameter-Sweep endlich", "[airwindows]")
{
    conduit::AirwindowsDensityModule density;
    REQUIRE (density.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    auto* densityTarget  = density.getParameterTarget ("density");
    auto* highpassTarget = density.getParameterTarget ("highpass");

    for (int step = 0; step <= 10; ++step)
    {
        const auto value = (float) step / 10.0f;
        densityTarget->store (value, std::memory_order_relaxed);
        highpassTarget->store (value, std::memory_order_relaxed);

        fillWithRamp (buffer, value - 0.5f);
        density.processBlock (buffer, midi);

        REQUIRE_FALSE (hasNanOrInf (buffer));
    }
}

TEST_CASE ("AirwindowsSlewModule: processBlock bleibt über einen Parameter-Sweep endlich", "[airwindows]")
{
    conduit::AirwindowsSlewModule slew;
    REQUIRE (slew.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    auto* clampingTarget = slew.getParameterTarget ("clamping");

    for (int step = 0; step <= 10; ++step)
    {
        clampingTarget->store ((float) step / 10.0f, std::memory_order_relaxed);
        fillWithRamp (buffer, 0.3f);
        slew.processBlock (buffer, midi);

        REQUIRE_FALSE (hasNanOrInf (buffer));
    }
}

TEST_CASE ("AirwindowsSpiralModule: processBlock bleibt endlich (keine Parameter)", "[airwindows]")
{
    conduit::AirwindowsSpiralModule spiral;
    REQUIRE (spiral.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
    {
        fillWithRamp (buffer, 0.3f * (float) block);
        spiral.processBlock (buffer, midi);

        REQUIRE_FALSE (hasNanOrInf (buffer));
    }
}

//==============================================================================
TEST_CASE ("ModuleFactory registriert alle drei Airwindows-Module", "[airwindows]")
{
    conduit::ModuleFactory factory;
    conduit::registerDefaultModules (factory);

    REQUIRE (factory.isRegistered (conduit::AirwindowsDensityModule::staticModuleId));
    REQUIRE (factory.isRegistered (conduit::AirwindowsSlewModule::staticModuleId));
    REQUIRE (factory.isRegistered (conduit::AirwindowsSpiralModule::staticModuleId));

    auto density = factory.create (conduit::AirwindowsDensityModule::staticModuleId);
    REQUIRE (density != nullptr);
    REQUIRE (dynamic_cast<conduit::AirwindowsDensityModule*> (density.get()) != nullptr);
}
