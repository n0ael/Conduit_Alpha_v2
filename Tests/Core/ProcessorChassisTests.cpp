#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Modules/AirwindowsDensityModule.h"
#include "Modules/ChassisSchema.h"
#include "Modules/ProcessorModule.h"
#include "Util/RtAllocationGuard.h"

using Catch::Approx;

namespace
{

//==============================================================================
/** Minimales Chassis-Modul: 1 DSP-Parameter, Audio-Passthrough — isoliert
    Gains/CV/Meter des Chassis von jeder konkreten DSP. */
class PassthroughChassisModule final : public conduit::ProcessorModule
{
public:
    PassthroughChassisModule()
        : ProcessorModule ({ { "amount", 0.25f, 0.0f, 1.0f } })
    {
    }

    [[nodiscard]] juce::String getModuleId() const override          { return "test_chassis"; }
    [[nodiscard]] juce::String getModuleDisplayName() const override { return "Test Chassis"; }
    [[nodiscard]] int getStateVersion() const override               { return chassisStateVersion; }

    float lastEffective = 0.0f;

protected:
    void prepareCore (double, int) override {}

    void processCore (juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        lastEffective = effectiveParam (0);
    }
};

void fillChannel (juce::AudioBuffer<float>& buffer, int channel, float value)
{
    auto* data = buffer.getWritePointer (channel);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] = value;
}

[[nodiscard]] juce::StringArray parameterIdsOf (const juce::ValueTree& nodeTree)
{
    juce::StringArray ids;
    const auto params = nodeTree.getChildWithName (conduit::id::parameters);

    for (int i = 0; i < params.getNumChildren(); ++i)
        ids.add (params.getChild (i).getProperty (conduit::id::paramId).toString());

    return ids;
}

/** v1-Node wie vor dem Chassis: nur DSP-Parameter, kein role, 2 Kanäle. */
[[nodiscard]] juce::ValueTree makeLegacyDensityNode()
{
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId,            "legacy-uuid",        nullptr);
    node.setProperty (conduit::id::type,              "Processor",          nullptr);
    node.setProperty (conduit::id::factoryId,         "airwindows_density", nullptr);
    node.setProperty (conduit::id::moduleId,          "airwindows_density", nullptr);
    node.setProperty (conduit::id::stateVersion,      1,                    nullptr);
    node.setProperty (conduit::id::numInputChannels,  2,                    nullptr);
    node.setProperty (conduit::id::numOutputChannels, 2,                    nullptr);

    juce::ValueTree params (conduit::id::parameters);

    for (const auto* paramId : { "density", "highpass", "out_level", "dry_wet" })
    {
        juce::ValueTree p (conduit::id::parameter);
        p.setProperty (conduit::id::paramId,      paramId, nullptr);
        p.setProperty (conduit::id::paramValue,   0.8,     nullptr);  // User-Wert ≠ Default
        p.setProperty (conduit::id::paramMin,     0.0,     nullptr);
        p.setProperty (conduit::id::paramMax,     1.0,     nullptr);
        p.setProperty (conduit::id::paramDefault, 0.5,     nullptr);
        params.appendChild (p, nullptr);
    }

    node.appendChild (params, nullptr);
    return node;
}

} // namespace

//==============================================================================
TEST_CASE ("FX-Chassis: createState liefert Gains + Attenuverter mit Rollen", "[chassis]")
{
    PassthroughChassisModule module;
    const auto node = module.createState();

    // Kanal-Layout: Audio 0..1 + 1 CV-Kanal für "amount"
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels)  == 3);
    REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 2);
    REQUIRE (node.hasProperty (conduit::id::linkSendEnabled));
    REQUIRE ((bool) node.getProperty (conduit::id::linkSendEnabled) == false);

    const auto params = node.getChildWithName (conduit::id::parameters);
    REQUIRE (params.getNumChildren() == 4);

    const juce::StringArray expectedIds   { "input_gain", "output_gain", "amount", "amount_cv_amt" };
    const juce::StringArray expectedRoles { "chassis", "chassis", "dsp", "cvAmount" };

    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto p = params.getChild (i);
        REQUIRE (p.getProperty (conduit::id::paramId).toString() == expectedIds[i]);
        REQUIRE (conduit::ChassisSchema::roleOf (p) == expectedRoles[i]);
    }

    // Ranges: Gains in dB, Attenuverter bipolar, DSP hart 0..1
    const auto inputGain = params.getChildWithProperty (conduit::id::paramId, "input_gain");
    REQUIRE ((double) inputGain.getProperty (conduit::id::paramMin) == Approx (-60.0));
    REQUIRE ((double) inputGain.getProperty (conduit::id::paramMax) == Approx (6.0));

    const auto cvAmt = params.getChildWithProperty (conduit::id::paramId, "amount_cv_amt");
    REQUIRE ((double) cvAmt.getProperty (conduit::id::paramMin)     == Approx (-1.0));
    REQUIRE ((double) cvAmt.getProperty (conduit::id::paramMax)     == Approx (1.0));
    REQUIRE ((double) cvAmt.getProperty (conduit::id::paramDefault) == Approx (0.0));
}

TEST_CASE ("FX-Chassis: getParameterTarget kennt Chassis- und DSP-Ziele", "[chassis]")
{
    PassthroughChassisModule module;

    REQUIRE (module.getParameterTarget ("input_gain")    != nullptr);
    REQUIRE (module.getParameterTarget ("output_gain")   != nullptr);
    REQUIRE (module.getParameterTarget ("amount")        != nullptr);
    REQUIRE (module.getParameterTarget ("amount_cv_amt") != nullptr);
    REQUIRE (module.getParameterTarget ("unbekannt")     == nullptr);
}

//==============================================================================
TEST_CASE ("FX-Chassis: 0 dB ist unity, -60 dB ist Stille", "[chassis]")
{
    PassthroughChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (3, 512);
    juce::MidiBuffer midi;

    SECTION ("Unity-Default: Audio bleibt bit-identisch")
    {
        buffer.clear();
        fillChannel (buffer, 0, 0.5f);
        fillChannel (buffer, 1, -0.25f);

        module.processBlock (buffer, midi);

        REQUIRE (buffer.getSample (0, 0)   == 0.5f);
        REQUIRE (buffer.getSample (0, 511) == 0.5f);
        REQUIRE (buffer.getSample (1, 100) == -0.25f);
    }

    SECTION ("-60 dB: nach der 5-ms-Rampe exakt 0")
    {
        module.getParameterTarget ("output_gain")->store (-60.0f, std::memory_order_relaxed);

        // Block 1: Rampe (240 Samples bei 48 kHz) läuft innerhalb des Blocks aus
        buffer.clear();
        fillChannel (buffer, 0, 1.0f);
        fillChannel (buffer, 1, 1.0f);
        module.processBlock (buffer, midi);

        // Block 2: Gain steht auf 0.0 (dB-Floor = -inf)
        buffer.clear();
        fillChannel (buffer, 0, 1.0f);
        fillChannel (buffer, 1, 1.0f);
        module.processBlock (buffer, midi);

        REQUIRE (buffer.getMagnitude (0, 0, 512) == 0.0f);
        REQUIRE (buffer.getMagnitude (1, 0, 512) == 0.0f);
    }
}

TEST_CASE ("FX-Chassis: Gain-Rampe ist klickfrei (monoton, kleine Schritte)", "[chassis]")
{
    PassthroughChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (3, 512);
    juce::MidiBuffer midi;

    module.getParameterTarget ("input_gain")->store (-60.0f, std::memory_order_relaxed);
    buffer.clear();
    fillChannel (buffer, 0, 1.0f);
    module.processBlock (buffer, midi);

    const auto* data = buffer.getReadPointer (0);
    bool monotonic = true;
    float maxStep = 0.0f;

    for (int i = 1; i < 512; ++i)
    {
        monotonic = monotonic && data[i] <= data[i - 1] + 1.0e-6f;
        maxStep = juce::jmax (maxStep, std::abs (data[i] - data[i - 1]));
    }

    REQUIRE (monotonic);
    REQUIRE (maxStep < 0.02f);   // 5-ms-Rampe: ~1/240 pro Sample
}

//==============================================================================
TEST_CASE ("FX-Chassis: CV-Blockmittel moduliert bipolar mit Hard-Clamp", "[chassis]")
{
    PassthroughChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 256).wasOk());

    juce::AudioBuffer<float> buffer (3, 256);
    juce::MidiBuffer midi;

    auto* cvAmt = module.getParameterTarget ("amount_cv_amt");

    SECTION ("Attenuverter 0 (Default): CV wirkungslos")
    {
        buffer.clear();
        fillChannel (buffer, 2, 0.5f);   // CV-Kanal des Parameters 0
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.25f));
    }

    SECTION ("Attenuverter +1: base + cv * range")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        fillChannel (buffer, 2, 0.5f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.75f));
    }

    SECTION ("Attenuverter -1: invertiert, Hard-Clamp auf hardMin")
    {
        cvAmt->store (-1.0f, std::memory_order_relaxed);
        buffer.clear();
        fillChannel (buffer, 2, 0.5f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.0f));   // 0.25 - 0.5 → clamp
    }

    SECTION ("Unverbundener CV-Kanal (genullt): Effektivwert = Basiswert")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.25f));
    }

    SECTION ("Blockmittel: alternierendes ±1-CV mittelt sich zu 0")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        auto* cv = buffer.getWritePointer (2);

        for (int i = 0; i < 256; ++i)
            cv[i] = (i % 2 == 0) ? 1.0f : -1.0f;

        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.25f));
    }
}

TEST_CASE ("ChassisSchema::computeEffective clamped hart", "[chassis]")
{
    using S = conduit::ChassisSchema;

    REQUIRE (S::computeEffective (0.5f, 0.0f, 1.0f, 0.0f, 1.0f)  == Approx (0.5f));
    REQUIRE (S::computeEffective (0.5f, 1.0f, 1.0f, 0.0f, 1.0f)  == Approx (1.0f));   // Clamp oben
    REQUIRE (S::computeEffective (0.5f, -1.0f, 1.0f, 0.0f, 1.0f) == Approx (0.0f));   // Clamp unten
    REQUIRE (S::computeEffective (0.5f, 0.25f, -1.0f, 0.0f, 1.0f) == Approx (0.25f)); // invertiert
    REQUIRE (S::computeEffective (2.0f, 0.5f, 0.5f, 0.0f, 4.0f)  == Approx (3.0f));   // Range-Skalierung
}

//==============================================================================
TEST_CASE ("FX-Chassis: processBlock ist allocation-frei", "[chassis]")
{
    PassthroughChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 256).wasOk());

    juce::AudioBuffer<float> buffer (3, 256);
    juce::MidiBuffer midi;
    buffer.clear();
    fillChannel (buffer, 0, 0.5f);

    module.processBlock (buffer, midi);   // Warm-up außerhalb des Audits

    const auto before = conduit::rt::getAllocationViolations();

    {
        const conduit::rt::ScopedRealtimeSection audit;

        for (int block = 0; block < 8; ++block)
            module.processBlock (buffer, midi);
    }

    const auto after = conduit::rt::getAllocationViolations();

    if (conduit::rt::isHookActive())
        REQUIRE (after == before);
}

TEST_CASE ("FX-Chassis: Meter messen post-Gain", "[chassis]")
{
    PassthroughChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 512).wasOk());

    juce::AudioBuffer<float> buffer (3, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    fillChannel (buffer, 0, 0.5f);
    fillChannel (buffer, 1, 0.5f);

    module.processBlock (buffer, midi);

    REQUIRE (module.getInputMeter().getPeak (0)  == Approx (0.5f));
    REQUIRE (module.getOutputMeter().getPeak (1) == Approx (0.5f));

    // Output-Gain -60 dB: Output-Meter fällt, Input-Meter bleibt bei 0.5
    module.getParameterTarget ("output_gain")->store (-60.0f, std::memory_order_relaxed);

    for (int block = 0; block < 40; ++block)   // Peak-Release-Ballistik abwarten
    {
        buffer.clear();
        fillChannel (buffer, 0, 0.5f);
        fillChannel (buffer, 1, 0.5f);
        module.processBlock (buffer, midi);
    }

    REQUIRE (module.getInputMeter().getPeak (0) == Approx (0.5f));
    REQUIRE (module.getOutputMeter().getPeak (0) < 0.4f);
}

//==============================================================================
TEST_CASE ("FX-Chassis: Link-Send ohne LinkClock bleibt offline und crashfrei", "[chassis]")
{
    PassthroughChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 256).wasOk());

    REQUIRE (module.getLinkSendStatus() == conduit::LinkSendTaps::Status::offline);

    module.setSendEnabled (true);   // ohne setLinkAudioContext: kein Tap
    REQUIRE (module.isSendEnabled());
    REQUIRE (module.getLinkSendStatus() == conduit::LinkSendTaps::Status::offline);

    juce::AudioBuffer<float> buffer (3, 256);
    juce::MidiBuffer midi;
    buffer.clear();
    module.processBlock (buffer, midi);

    module.setSendEnabled (false);
    REQUIRE_FALSE (module.isSinkRetirePending());

    module.releaseSessionResources();   // Phase 1 auch ohne Tap harmlos
}

//==============================================================================
TEST_CASE ("ChassisSchema::migrate hebt einen v1-Node aufs Chassis-Schema", "[chassis]")
{
    auto node = makeLegacyDensityNode();
    conduit::ChassisSchema::migrate (node);

    // Reihenfolge: Gains vorn, Attenuverter direkt hinter seinem DSP-Parameter
    const juce::StringArray expected {
        "input_gain", "output_gain",
        "density", "density_cv_amt",
        "highpass", "highpass_cv_amt",
        "out_level", "out_level_cv_amt",
        "dry_wet", "dry_wet_cv_amt"
    };
    REQUIRE (parameterIdsOf (node) == expected);

    // Bestehende User-Werte überleben
    const auto params  = node.getChildWithName (conduit::id::parameters);
    const auto density = params.getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE ((double) density.getProperty (conduit::id::paramValue) == Approx (0.8));
    REQUIRE (conduit::ChassisSchema::roleOf (density) == "dsp");

    // Kanal-Layout + Metadaten
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 6);   // 2 Audio + 4 CV
    REQUIRE ((int) node.getProperty (conduit::id::stateVersion)     == 2);
    REQUIRE ((bool) node.getProperty (conduit::id::linkSendEnabled) == false);
}

TEST_CASE ("ChassisSchema::migrate ist idempotent", "[chassis]")
{
    auto node = makeLegacyDensityNode();
    conduit::ChassisSchema::migrate (node);
    const auto afterFirst = node.toXmlString();

    conduit::ChassisSchema::migrate (node);
    REQUIRE (node.toXmlString() == afterFirst);
}

TEST_CASE ("ChassisSchema::migrate erzeugt dasselbe Parameter-Layout wie createState", "[chassis]")
{
    auto legacy = makeLegacyDensityNode();
    conduit::ChassisSchema::migrate (legacy);

    conduit::AirwindowsDensityModule fresh;
    const auto freshNode = fresh.createState();

    REQUIRE (parameterIdsOf (legacy) == parameterIdsOf (freshNode));
    REQUIRE ((int) legacy.getProperty (conduit::id::numInputChannels)
             == (int) freshNode.getProperty (conduit::id::numInputChannels));

    // Frisch angelegte Nodes sind bereits chassis-förmig — migrate ist no-op
    auto freshCopy = freshNode.createCopy();
    conduit::ChassisSchema::migrate (freshCopy);
    REQUIRE (parameterIdsOf (freshCopy) == parameterIdsOf (freshNode));
}
