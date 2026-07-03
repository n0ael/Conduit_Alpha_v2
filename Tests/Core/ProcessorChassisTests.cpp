#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphFader.h"
#include "Core/LinkClock.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AirwindowsDensityModule.h"
#include "Modules/ChassisSchema.h"
#include "Modules/ModuleFactory.h"
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

/** Zwei DSP-Parameter — für Control-Link-Tests (Quelle → Ziel). */
class DualChassisModule final : public conduit::ProcessorModule
{
public:
    DualChassisModule()
        : ProcessorModule ({ { "alpha", 0.5f, 0.0f, 1.0f },
                             { "beta",  0.8f, 0.0f, 1.0f } })
    {
    }

    [[nodiscard]] juce::String getModuleId() const override          { return "test_dual"; }
    [[nodiscard]] juce::String getModuleDisplayName() const override { return "Test Dual"; }
    [[nodiscard]] int getStateVersion() const override               { return chassisStateVersion; }

    float lastAlpha = 0.0f, lastBeta = 0.0f;

protected:
    void prepareCore (double, int) override {}

    void processCore (juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        lastAlpha = effectiveParam (0);
        lastBeta  = effectiveParam (1);
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

        REQUIRE (juce::exactlyEqual (buffer.getSample (0, 0), 0.5f));
        REQUIRE (juce::exactlyEqual (buffer.getSample (0, 511), 0.5f));
        REQUIRE (juce::exactlyEqual (buffer.getSample (1, 100), -0.25f));
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

        REQUIRE (juce::exactlyEqual (buffer.getMagnitude (0, 0, 512), 0.0f));
        REQUIRE (juce::exactlyEqual (buffer.getMagnitude (1, 0, 512), 0.0f));
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
TEST_CASE ("FX-Chassis: CV-Modulation im Richtungs-Modell (Betrag + Attenuverter-Vorzeichen)", "[chassis]")
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

    SECTION ("Attenuverter +1: vom Fader-Wert nach OBEN")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        fillChannel (buffer, 2, 0.5f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.75f));
    }

    SECTION ("Attenuverter -1: vom Fader-Wert nach UNTEN (Clamp auf rangeMin)")
    {
        cvAmt->store (-1.0f, std::memory_order_relaxed);
        buffer.clear();
        fillChannel (buffer, 2, 0.5f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.0f));   // 0.25 - 0.5 → clamp
    }

    SECTION ("Bipolare Quelle: negatives CV wirkt wie positives (Betrag)")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        fillChannel (buffer, 2, -0.5f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.75f));   // Richtung kommt vom Knob
    }

    SECTION ("Unverbundener CV-Kanal (genullt): Effektivwert = Basiswert")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.25f));
    }

    SECTION ("Gleichrichtung VOR der Mittelung: ±1-Rechteck = volle Modulation")
    {
        cvAmt->store (1.0f, std::memory_order_relaxed);
        buffer.clear();
        auto* cv = buffer.getWritePointer (2);

        for (int i = 0; i < 256; ++i)
            cv[i] = (i % 2 == 0) ? 1.0f : -1.0f;

        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (1.0f));   // |cv|-Mittel = 1 → clamp oben
    }

    SECTION ("User-Regelbereich begrenzt UND skaliert die Modulation (Dev-Modus)")
    {
        module.setParameterUserRange ("amount", 0.2f, 0.6f);
        cvAmt->store (1.0f, std::memory_order_relaxed);

        // Tiefe skaliert mit der User-Range: 0.25 + 0.5·1·0.4 = 0.45
        buffer.clear();
        fillChannel (buffer, 2, 0.5f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.45f));

        // Vollausschlag clamped auf userMax, nie darüber hinaus
        buffer.clear();
        fillChannel (buffer, 2, 1.0f);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.6f));

        // Negativ: clamped auf userMin
        cvAmt->store (-1.0f, std::memory_order_relaxed);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastEffective == Approx (0.2f));
    }
}

TEST_CASE ("FX-Chassis: Control-Link moduliert das Ziel aus der Stufe-1-Quelle", "[chassis][link]")
{
    DualChassisModule module;
    REQUIRE (module.prepareForGraph (48000.0, 128).wasOk());

    juce::AudioBuffer<float> buffer (4, 128);   // 2 Audio + 2 CV
    juce::MidiBuffer midi;

    SECTION ("Negativer Amount: Quelle hoch -> Ziel runter (User-Beispiel)")
    {
        module.setParameterLink ("beta", "alpha", -1.0f);

        buffer.clear();
        module.processBlock (buffer, midi);

        // alpha 0.5 → srcNorm 0.5; beta = clamp(0.8 − 0.5·1·1) = 0.3
        REQUIRE (module.lastAlpha == Approx (0.5f));
        REQUIRE (module.lastBeta  == Approx (0.3f));

        // Quelle auf Maximum → Ziel fällt weiter (0.8 − 1.0 → clamp 0)
        module.getParameterTarget ("alpha")->store (1.0f, std::memory_order_relaxed);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastBeta == Approx (0.0f));
    }

    SECTION ("Link folgt auch der CV-Modulation der Quelle (Stufe 1)")
    {
        module.setParameterLink ("beta", "alpha", -1.0f);
        module.getParameterTarget ("alpha")->store (0.0f, std::memory_order_relaxed);
        module.getParameterTarget ("alpha_cv_amt")->store (1.0f, std::memory_order_relaxed);

        buffer.clear();
        fillChannel (buffer, 2, 0.5f);   // CV-Kanal von alpha
        module.processBlock (buffer, midi);

        // alpha Stufe 1 = 0 + 0.5 = 0.5 → beta = 0.8 − 0.5 = 0.3
        REQUIRE (module.lastAlpha == Approx (0.5f));
        REQUIRE (module.lastBeta  == Approx (0.3f));
    }

    SECTION ("Zyklus A<->B ist harmlos: beide lesen Stufe-1-Werte")
    {
        module.setParameterLink ("beta",  "alpha", -1.0f);
        module.setParameterLink ("alpha", "beta",  -1.0f);

        buffer.clear();
        module.processBlock (buffer, midi);

        // Stufe 1: alpha 0.5, beta 0.8 → alpha = 0.5 − 0.8 → 0; beta = 0.8 − 0.5 = 0.3
        REQUIRE (module.lastAlpha == Approx (0.0f));
        REQUIRE (module.lastBeta  == Approx (0.3f));
        REQUIRE (std::isfinite (module.lastAlpha));
    }

    SECTION ("Leere Quelle loest den Link")
    {
        module.setParameterLink ("beta", "alpha", -1.0f);
        module.setParameterLink ("beta", "", 0.0f);

        buffer.clear();
        module.processBlock (buffer, midi);
        REQUIRE (module.lastBeta == Approx (0.8f));
    }

    SECTION ("Link-Response-Kurve formt die Quelle (Gain-Matching)")
    {
        module.setParameterLink ("beta", "alpha", -1.0f);

        const auto response = conduit::ChassisSchema::parseLinkResponse ("0.9 0.1 0.9 0.4");
        REQUIRE (response.has_value());
        module.setParameterLinkCurve ("beta", response);
        REQUIRE (module.hasParameterLinkCurve ("beta"));

        buffer.clear();
        module.processBlock (buffer, midi);

        // srcNorm 0.5 → durch die Response geformt, dann Richtungs-Modell
        const auto shaped = conduit::ChassisSchema::evaluateLinkResponse (*response, 0.5f);
        REQUIRE (module.lastBeta == Approx (juce::jlimit (0.0f, 1.0f, 0.8f - shaped)));

        // Kurve entfernen → wieder linear
        module.setParameterLinkCurve ("beta", std::nullopt);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastBeta == Approx (0.3f));
    }

    SECTION ("FALLENDE Link-Response dreht die Richtung in der Kurve (Auto-Gain)")
    {
        // Response 1→0 bei positivem Amount: Quelle LEISE = volle Modulation,
        // Quelle LAUT = keine — Richtung kommt aus den Response-Endpunkten
        module.setParameterLink ("beta", "alpha", 1.0f);
        module.setParameterLinkCurve ("beta",
            conduit::ChassisSchema::parseLinkResponse ("0.25 0.25 0.75 0.75 1 0"));

        buffer.clear();
        module.processBlock (buffer, midi);

        // srcNorm 0.5 → shaped = 1 − 0.5 = 0.5 → beta = clamp(0.8 + 0.5) = 1.0
        REQUIRE (module.lastBeta == Approx (1.0f));

        // Quelle auf Maximum → shaped 0 → keine Modulation mehr
        module.getParameterTarget ("alpha")->store (1.0f, std::memory_order_relaxed);
        module.processBlock (buffer, midi);
        REQUIRE (module.lastBeta == Approx (0.8f));
    }

    SECTION ("Link-Tiefe skaliert mit der User-Range des Ziels")
    {
        module.setParameterLink ("beta", "alpha", -1.0f);
        module.setParameterUserRange ("beta", 0.6f, 1.0f);

        buffer.clear();
        module.processBlock (buffer, midi);

        // beta = clamp(0.8 − 0.5·0.4, 0.6, 1.0) = 0.6
        REQUIRE (module.lastBeta == Approx (0.6f));
    }
}

TEST_CASE ("ChassisSchema::computeEffective: Richtungs-Modell mit Range-Clamp", "[chassis]")
{
    using S = conduit::ChassisSchema;

    REQUIRE (S::computeEffective (0.5f, 0.0f, 1.0f, 0.0f, 1.0f)   == Approx (0.5f));
    REQUIRE (S::computeEffective (0.5f, 1.0f, 1.0f, 0.0f, 1.0f)   == Approx (1.0f));   // Clamp oben
    REQUIRE (S::computeEffective (0.5f, 1.0f, -1.0f, 0.0f, 1.0f)  == Approx (0.0f));   // Clamp unten
    REQUIRE (S::computeEffective (0.5f, 0.25f, -1.0f, 0.0f, 1.0f) == Approx (0.25f));  // nach unten
    REQUIRE (S::computeEffective (2.0f, 0.5f, 0.5f, 0.0f, 4.0f)   == Approx (3.0f));   // Range-Skalierung
    REQUIRE (S::computeEffective (0.3f, 1.0f, 1.0f, 0.2f, 0.6f)   == Approx (0.6f));   // User-Range-Clamp
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
TEST_CASE ("FX-Chassis: CV-Kabel im Graph moduliert einen DSP-Parameter end-to-end", "[chassis]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::EngineProcessor engine;
    auto& manager = engine.getGraphManager();

    auto nodes = engine.getRootState().getChildWithName (conduit::id::nodes);
    const auto ioIn  = nodes.getChildWithProperty (conduit::id::factoryId,
                                                   juce::String (conduit::audioInputModuleId));
    const auto ioOut = nodes.getChildWithProperty (conduit::id::factoryId,
                                                   juce::String (conduit::audioOutputModuleId));

    engine.prepareToPlay (48000.0, 32);

    const auto density = manager.addModuleNode (conduit::AirwindowsDensityModule::staticModuleId, {});
    const auto uuidOf  = [] (const juce::ValueTree& n) { return n.getProperty (conduit::id::nodeId).toString(); };

    // Audio: In ch0 → Density ch0 → Out ch0; CV: In ch1 → Density-CV-Kanal 2
    // (erster DSP-Parameter "density"). In ch1 dient als CV-Quelle.
    REQUIRE (manager.addConnection (uuidOf (ioIn), 0, uuidOf (density), 0));
    REQUIRE (manager.addConnection (uuidOf (density), 0, uuidOf (ioOut), 0));
    REQUIRE (manager.addConnection (uuidOf (ioIn), 1, uuidOf (density), 2));

    // Basiswert 0, Attenuverter zunächst 0 (CV wirkungslos)
    auto params = density.getChildWithName (conduit::id::parameters);
    params.getChildWithProperty (conduit::id::paramId, "density")
          .setProperty (conduit::id::paramValue, 0.0, nullptr);
    params.getChildWithProperty (conduit::id::paramId, "density_cv_amt")
          .setProperty (conduit::id::paramValue, 0.0, nullptr);

    juce::AudioBuffer<float> buffer (2, 32);
    juce::MidiBuffer midi;

    const auto pumpBlocks = [&] (int count, juce::AudioBuffer<float>* capture = nullptr)
    {
        for (int i = 0; i < count; ++i)
        {
            juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 0.25f, 32);  // Audio
            juce::FloatVectorOperations::fill (buffer.getWritePointer (1), 1.0f, 32);   // CV-Quelle
            engine.processBlock (buffer, midi);
        }

        if (capture != nullptr)
            capture->makeCopyOf (buffer);
    };

    // Topologie-Swap mit laufendem Audio durchpumpen (Muster PatchingTests)
    manager.flushPendingTopologyUpdate();
    for (int i = 0; i < 100 && manager.isWaitingForSilence(); ++i)
    {
        pumpBlocks (1);
        manager.flushPendingTopologyUpdate();
    }
    REQUIRE_FALSE (manager.isWaitingForSilence());

    // A: cv_amt 0 → CV wirkungslos (Referenz nach eingeschwungenen Fades)
    juce::AudioBuffer<float> outputA, outputB;
    pumpBlocks (30, &outputA);

    // B: Attenuverter auf 1 → CV 1.0 hebt density auf 1.0 (Hard-Clamp)
    params.getChildWithProperty (conduit::id::paramId, "density_cv_amt")
          .setProperty (conduit::id::paramValue, 1.0, nullptr);
    pumpBlocks (30, &outputB);

    float difference = 0.0f;
    for (int i = 0; i < 32; ++i)
        difference += std::abs (outputA.getSample (0, i) - outputB.getSample (0, i));

    REQUIRE (difference > 0.01f);   // Modulation hörbar am Ausgang angekommen
}

//==============================================================================
namespace
{

[[nodiscard]] juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

/** GraphManager-Rig MIT Link-Kontext — der Send-Tap braucht die LinkClock. */
struct LinkSendRig
{
    LinkSendRig()
    {
        conduit::registerDefaultModules (factory);
        clock.prepare (48000.0);
        manager.setLinkClock (&clock);

        node = manager.addModuleNode (conduit::AirwindowsDensityModule::staticModuleId, {});
        manager.flushPendingTopologyUpdate();
    }

    [[nodiscard]] juce::String uuid() const { return node.getProperty (conduit::id::nodeId).toString(); }

    [[nodiscard]] conduit::ProcessorModule* module() const
    {
        return dynamic_cast<conduit::ProcessorModule*> (manager.getModuleFor (uuid()));
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::LinkClock clock { 120.0, "ConduitTest" };
    juce::ValueTree node;
};

} // namespace

TEST_CASE ("FX-Chassis: linkSendEnabled steuert den Tap live (an/aus/undo)", "[chassis][linkaudio]")
{
    LinkSendRig rig;
    auto* module = rig.module();
    REQUIRE (module != nullptr);
    REQUIRE_FALSE (module->hasActiveSendTap());

    // An: Property (undo-fähig) → Listener → Modul erzeugt den Tap live
    REQUIRE (rig.manager.setLinkSendEnabled (rig.uuid(), true));
    REQUIRE ((bool) rig.node.getProperty (conduit::id::linkSendEnabled));
    REQUIRE (module->hasActiveSendTap());
    REQUIRE (module->getSendSinkName() == rig.node.getProperty (conduit::id::moduleId).toString());

    // Rename propagiert live zum Sink (Kanal-Name = moduleId)
    REQUIRE (rig.manager.renameNode (rig.uuid(), "mein_density"));
    REQUIRE (module->getSendSinkName() == "mein_density");

    // Aus: Tap retired (Phase-1-Muster). Audio-Thread-Surrogat: ein Block
    // nach dem Store erfüllt den Epoch-Handshake (Muster LinkAudioSendTests)
    REQUIRE (rig.manager.setLinkSendEnabled (rig.uuid(), false));
    REQUIRE_FALSE (module->hasActiveSendTap());

    juce::AudioBuffer<float> buffer (6, 32);
    juce::MidiBuffer midi;
    buffer.clear();
    module->processBlock (buffer, midi);

    module->flushPendingSinkRetirement();
    REQUIRE_FALSE (module->isSinkRetirePending());

    // Undo (Aus rückgängig) → wieder an
    REQUIRE (rig.undoManager.undo());
    REQUIRE (module->hasActiveSendTap());
}

TEST_CASE ("FX-Chassis: persistierter Send entsteht bei der Materialisierung", "[chassis][linkaudio]")
{
    LinkSendRig rig;

    // Zweiter Node mit linkSendEnabled=true VOR der Materialisierung
    // (Preset-Load-Pfad: Property steht im Tree, Modul entsteht danach)
    auto second = rig.manager.addModuleNode (conduit::AirwindowsDensityModule::staticModuleId, {});
    second.setProperty (conduit::id::linkSendEnabled, true, nullptr);
    rig.manager.flushPendingTopologyUpdate();

    auto* module = dynamic_cast<conduit::ProcessorModule*> (
        rig.manager.getModuleFor (second.getProperty (conduit::id::nodeId).toString()));
    REQUIRE (module != nullptr);
    REQUIRE (module->isSendEnabled());
    REQUIRE (module->hasActiveSendTap());
}

TEST_CASE ("FX-Chassis: Delete Phase 1 zieht den Send-Tap sofort zurueck", "[chassis][linkaudio]")
{
    LinkSendRig rig;
    REQUIRE (rig.manager.setLinkSendEnabled (rig.uuid(), true));

    auto* module = rig.module();
    REQUIRE (module->hasActiveSendTap());

    // Phase 1 (5.3): GraphManager-Listener ruft releaseSessionResources —
    // keine Zombie-Kanäle bei den Peers (7.2)
    REQUIRE (rig.manager.requestNodeDelete (rig.uuid()));
    REQUIRE_FALSE (module->hasActiveSendTap());
}

//==============================================================================
TEST_CASE ("Dev-Modus: setParameterUserRange validiert und clamped den Wert mit", "[chassis][devmode]")
{
    LinkSendRig rig;
    auto params  = rig.node.getChildWithName (conduit::id::parameters);
    auto density = params.getChildWithProperty (conduit::id::paramId, "density");

    density.setProperty (conduit::id::paramValue, 0.9, nullptr);

    // Gültig: Range gesetzt, Wert in DERSELBEN Transaktion geclamped
    REQUIRE (rig.manager.setParameterUserRange (rig.uuid(), "density", 0.2, 0.6));
    REQUIRE ((double) density.getProperty (conduit::id::paramUserMin) == Approx (0.2));
    REQUIRE ((double) density.getProperty (conduit::id::paramUserMax) == Approx (0.6));
    REQUIRE ((double) density.getProperty (conduit::id::paramValue)   == Approx (0.6));

    // EIN Undo restauriert Range UND Wert
    REQUIRE (rig.undoManager.undo());
    REQUIRE_FALSE (density.hasProperty (conduit::id::paramUserMin));
    REQUIRE ((double) density.getProperty (conduit::id::paramValue) == Approx (0.9));

    // Ungültig: min >= max, außerhalb der Hard-Range, unbekannter Parameter
    REQUIRE_FALSE (rig.manager.setParameterUserRange (rig.uuid(), "density", 0.6, 0.6));
    REQUIRE_FALSE (rig.manager.setParameterUserRange (rig.uuid(), "density", -0.5, 0.5));
    REQUIRE_FALSE (rig.manager.setParameterUserRange (rig.uuid(), "density", 0.0, 1.5));
    REQUIRE_FALSE (rig.manager.setParameterUserRange (rig.uuid(), "nicht_da", 0.0, 1.0));
    REQUIRE_FALSE (density.hasProperty (conduit::id::paramUserMin));
}

TEST_CASE ("Dev-Modus: setParameterHidden trennt CV-Kabel in derselben Transaktion", "[chassis][devmode]")
{
    LinkSendRig rig;

    // LFO als CV-Quelle auf den density-CV-Kanal (2)
    const auto lfo = rig.manager.addModuleNode ("lfo", {});
    const auto lfoUuid = lfo.getProperty (conduit::id::nodeId).toString();
    REQUIRE (rig.manager.addConnection (lfoUuid, 0, rig.uuid(), 2));

    auto connections = rig.root.getChildWithName (conduit::id::connections);
    REQUIRE (connections.getNumChildren() == 1);

    const auto inputChannelsBefore = (int) rig.node.getProperty (conduit::id::numInputChannels);

    // Ausblenden: Property + Kabel-Trennung in EINER Transaktion
    REQUIRE (rig.manager.setParameterHidden (rig.uuid(), "density", true));
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE ((bool) density.getProperty (conduit::id::paramUiHidden));
    REQUIRE (connections.getNumChildren() == 0);   // keine Phantom-Modulation

    // Bus-Layout bleibt IMMER unverändert (4.6)
    REQUIRE ((int) rig.node.getProperty (conduit::id::numInputChannels) == inputChannelsBefore);

    // EIN Undo: Property UND Kabel zurück
    REQUIRE (rig.undoManager.undo());
    REQUIRE_FALSE ((bool) density.getProperty (conduit::id::paramUiHidden, false));
    REQUIRE (connections.getNumChildren() == 1);

    // Gains/Attenuverter sind nicht ausblendbar
    REQUIRE_FALSE (rig.manager.setParameterHidden (rig.uuid(), "input_gain", true));
    REQUIRE_FALSE (rig.manager.setParameterHidden (rig.uuid(), "density_cv_amt", true));
}

TEST_CASE ("Dev-Modus: User-Range erreicht das Modul live und bei der Materialisierung", "[chassis][devmode]")
{
    LinkSendRig rig;
    auto* module = rig.module();

    // Default: Wirkbereich = Hard-Range
    REQUIRE (module->getParameterUserRange ("density").getStart() == Approx (0.0f));
    REQUIRE (module->getParameterUserRange ("density").getEnd()   == Approx (1.0f));

    // Live: Tree-Property → Listener → Modul-Atomics (kein Rebuild)
    REQUIRE (rig.manager.setParameterUserRange (rig.uuid(), "density", 0.2, 0.6));
    REQUIRE (module->getParameterUserRange ("density").getStart() == Approx (0.2f));
    REQUIRE (module->getParameterUserRange ("density").getEnd()   == Approx (0.6f));

    // Undo restauriert den Wirkbereich (Properties weg → Hard-Range)
    REQUIRE (rig.undoManager.undo());
    REQUIRE (module->getParameterUserRange ("density").getEnd() == Approx (1.0f));

    // Materialisierungs-Pfad (Preset-Load): Range steht im Tree, BEVOR das
    // Modul entsteht
    auto second = rig.manager.addModuleNode (conduit::AirwindowsDensityModule::staticModuleId, {});
    second.getChildWithName (conduit::id::parameters)
          .getChildWithProperty (conduit::id::paramId, "highpass")
          .setProperty (conduit::id::paramUserMax, 0.5, nullptr);
    rig.manager.flushPendingTopologyUpdate();

    auto* secondModule = dynamic_cast<conduit::ProcessorModule*> (
        rig.manager.getModuleFor (second.getProperty (conduit::id::nodeId).toString()));
    REQUIRE (secondModule != nullptr);
    REQUIRE (secondModule->getParameterUserRange ("highpass").getEnd() == Approx (0.5f));
}

TEST_CASE ("Dev-Modus: setParameterLink validiert, synct live und bei Materialisierung", "[chassis][link]")
{
    LinkSendRig rig;
    auto* module = rig.module();

    // Gültig: density folgt out_level, live gespiegelt (Index 2 = out_level)
    REQUIRE (rig.manager.setParameterLink (rig.uuid(), "density", "out_level", -0.5));
    REQUIRE (module->getParameterLinkSourceIndex ("density") == 2);

    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE (density.getProperty (conduit::id::paramLinkSource).toString() == "out_level");

    // Lösen (leere Quelle) + Undo stellt den Link wieder her
    REQUIRE (rig.manager.setParameterLink (rig.uuid(), "density", "", 0.0));
    REQUIRE (module->getParameterLinkSourceIndex ("density") == -1);
    REQUIRE (rig.undoManager.undo());
    REQUIRE (module->getParameterLinkSourceIndex ("density") == 2);

    // Ungültig: Quelle == Ziel, unbekannte/nicht-dsp-Parameter
    REQUIRE_FALSE (rig.manager.setParameterLink (rig.uuid(), "density", "density", 1.0));
    REQUIRE_FALSE (rig.manager.setParameterLink (rig.uuid(), "density", "input_gain", 1.0));
    REQUIRE_FALSE (rig.manager.setParameterLink (rig.uuid(), "input_gain", "density", 1.0));
    REQUIRE_FALSE (rig.manager.setParameterLink (rig.uuid(), "density", "nicht_da", 1.0));

    // Materialisierungs-Pfad: Link steht im Tree, BEVOR das Modul entsteht
    auto second = rig.manager.addModuleNode (conduit::AirwindowsDensityModule::staticModuleId, {});
    auto secondParam = second.getChildWithName (conduit::id::parameters)
                           .getChildWithProperty (conduit::id::paramId, "highpass");
    secondParam.setProperty (conduit::id::paramLinkSource, "density", nullptr);
    secondParam.setProperty (conduit::id::paramLinkAmount, 0.5, nullptr);
    rig.manager.flushPendingTopologyUpdate();

    auto* secondModule = dynamic_cast<conduit::ProcessorModule*> (
        rig.manager.getModuleFor (second.getProperty (conduit::id::nodeId).toString()));
    REQUIRE (secondModule->getParameterLinkSourceIndex ("highpass") == 0);   // density
}

TEST_CASE ("Dev-Modus: setParameterLinkCurve validiert, synct live und ist undo-faehig", "[chassis][link]")
{
    LinkSendRig rig;
    auto* module = rig.module();

    REQUIRE (rig.manager.setParameterLink (rig.uuid(), "density", "out_level", -1.0));
    REQUIRE_FALSE (module->hasParameterLinkCurve ("density"));

    // Setzen → live gespiegelt; leerer String entfernt; Undo stellt her
    REQUIRE (rig.manager.setParameterLinkCurve (rig.uuid(), "density", "0.9 0.1 0.9 0.4"));
    REQUIRE (module->hasParameterLinkCurve ("density"));

    REQUIRE (rig.manager.setParameterLinkCurve (rig.uuid(), "density", ""));
    REQUIRE_FALSE (module->hasParameterLinkCurve ("density"));

    REQUIRE (rig.undoManager.undo());
    REQUIRE (module->hasParameterLinkCurve ("density"));

    // Ungültig: unlesbarer String, nicht-dsp-Parameter
    REQUIRE_FALSE (rig.manager.setParameterLinkCurve (rig.uuid(), "density", "kaputt"));
    REQUIRE_FALSE (rig.manager.setParameterLinkCurve (rig.uuid(), "input_gain", "0.5 0.5 0.5 0.5"));

    // 6-Token-Format (Response mit Start/Ende, auch fallend) ist gültig
    REQUIRE (rig.manager.setParameterLinkCurve (rig.uuid(), "density",
                                                "0.25 0.25 0.75 0.75 1 0"));
    REQUIRE (module->hasParameterLinkCurve ("density"));
}

TEST_CASE ("ChassisSchema::parseLinkResponse: 4- und 6-Token-Format, fallend erlaubt", "[chassis][link]")
{
    using S = conduit::ChassisSchema;

    // Altbestand (4 Tokens): Start 0 → Ende 1
    const auto legacy = S::parseLinkResponse ("0.9 0.1 0.9 0.4");
    REQUIRE (legacy.has_value());
    REQUIRE (legacy->startY == Approx (0.0f));
    REQUIRE (legacy->endY   == Approx (1.0f));

    // 6 Tokens: fallende Response, Roundtrip über linkResponseToString
    const auto falling = S::parseLinkResponse ("0.25 0.25 0.75 0.75 1 0");
    REQUIRE (falling.has_value());
    REQUIRE (falling->startY == Approx (1.0f));
    REQUIRE (falling->endY   == Approx (0.0f));
    REQUIRE (S::evaluateLinkResponse (*falling, 0.0f) == Approx (1.0f).margin (0.002));
    REQUIRE (S::evaluateLinkResponse (*falling, 1.0f) == Approx (0.0f).margin (0.002));
    REQUIRE (S::evaluateLinkResponse (*falling, 0.5f) == Approx (0.5f).margin (0.002));

    const auto roundtrip = S::parseLinkResponse (S::linkResponseToString (*falling));
    REQUIRE (roundtrip.has_value());
    REQUIRE (roundtrip->startY == Approx (1.0f).margin (0.001));

    REQUIRE_FALSE (S::parseLinkResponse ("0.1 0.2 0.3").has_value());   // 3 Tokens
    REQUIRE_FALSE (S::parseLinkResponse ("").has_value());
}

TEST_CASE ("ChassisSchema::cvChannelForParam ignoriert uiHidden (festes Layout)", "[chassis][devmode]")
{
    conduit::AirwindowsDensityModule density;
    auto node = density.createState();

    REQUIRE (conduit::ChassisSchema::cvChannelForParam (node, "density")   == 2);
    REQUIRE (conduit::ChassisSchema::cvChannelForParam (node, "dry_wet")   == 5);
    REQUIRE (conduit::ChassisSchema::cvChannelForParam (node, "input_gain") == -1);
    REQUIRE (conduit::ChassisSchema::cvChannelForParam (node, "nicht_da")  == -1);

    // uiHidden verschiebt die Kanal-Zuordnung NICHT
    node.getChildWithName (conduit::id::parameters)
        .getChildWithProperty (conduit::id::paramId, "density")
        .setProperty (conduit::id::paramUiHidden, true, nullptr);
    REQUIRE (conduit::ChassisSchema::cvChannelForParam (node, "dry_wet") == 5);
}

//==============================================================================
TEST_CASE ("ChassisSchema: Bezier-Kurven parsen, evaluieren und invertieren", "[chassis][curve]")
{
    using S = conduit::ChassisSchema;

    SECTION ("parseCurve: Roundtrip, Clamping, ungültige Strings")
    {
        const auto curve = S::parseCurve ("0.1 0.9 0.8 0.2");
        REQUIRE (curve.has_value());
        REQUIRE (curve->x1 == Approx (0.1f));
        REQUIRE (curve->y2 == Approx (0.2f));

        const auto roundtrip = S::parseCurve (S::curveToString (*curve));
        REQUIRE (roundtrip.has_value());
        REQUIRE (roundtrip->y1 == Approx (0.9f).margin (0.001));

        // Clamping auf [0,1] erzwingt Monotonie
        const auto clamped = S::parseCurve ("-1 2 0.5 0.5");
        REQUIRE (clamped.has_value());
        REQUIRE (clamped->x1 == Approx (0.0f));
        REQUIRE (clamped->y1 == Approx (1.0f));

        REQUIRE_FALSE (S::parseCurve ("").has_value());
        REQUIRE_FALSE (S::parseCurve ("0.1 0.2").has_value());
        REQUIRE_FALSE (S::parseCurve ("linear").has_value());   // 1 Token
    }

    SECTION ("evaluateCurve: Endpunkte fest, Diagonale = Identität")
    {
        const S::BezierCurve diagonal { 0.25f, 0.25f, 0.75f, 0.75f };

        REQUIRE (S::evaluateCurve (diagonal, 0.0f) == Approx (0.0f).margin (0.001));
        REQUIRE (S::evaluateCurve (diagonal, 1.0f) == Approx (1.0f).margin (0.001));
        REQUIRE (S::evaluateCurve (diagonal, 0.5f) == Approx (0.5f).margin (0.001));
        REQUIRE (S::evaluateCurve (diagonal, 0.3f) == Approx (0.3f).margin (0.001));
    }

    SECTION ("Monotonie + Invertierbarkeit (Fader-Mapping eindeutig)")
    {
        const S::BezierCurve expo { 0.9f, 0.1f, 0.9f, 0.4f };   // stark gebogene Kurve
        float previous = -1.0f;

        for (int i = 0; i <= 20; ++i)
        {
            const auto p = static_cast<float> (i) / 20.0f;
            const auto y = S::evaluateCurve (expo, p);

            REQUIRE (y >= previous);   // monoton steigend
            previous = y;

            // Inverse: Position → Wert → Position kommt zurück
            REQUIRE (S::curvePositionForValue (expo, y) == Approx (p).margin (0.002));
        }
    }
}

TEST_CASE ("ChassisSchema: parseButtons/buttonsToString Roundtrip, Limits, Robustheit", "[chassis][buttons]")
{
    using S = conduit::ChassisSchema;

    SECTION ("Roundtrip inkl. Sonderzeichen im Namen (JSON-Escaping)")
    {
        const std::vector<S::ButtonPreset> buttons {
            { "Dry",                          0.0 },
            { juce::String::fromUTF8 ("Wet \"ö\" 50%"), 0.5 },
            { "Max",                          1.0 },
        };

        const auto text = S::buttonsToString (buttons);
        const auto parsed = S::parseButtons (text);
        REQUIRE (parsed.has_value());
        REQUIRE (parsed->size() == 3);
        REQUIRE ((*parsed)[0].name == "Dry");
        REQUIRE ((*parsed)[1].name == juce::String::fromUTF8 ("Wet \"ö\" 50%"));
        REQUIRE ((*parsed)[1].value == Approx (0.5));
        REQUIRE ((*parsed)[2].value == Approx (1.0));
    }

    SECTION ("Ganzzahl-Werte werden akzeptiert (JSON kennt kein double-Literal-Zwang)")
    {
        const auto parsed = S::parseButtons ("[{\"n\":\"Eins\",\"v\":1}]");
        REQUIRE (parsed.has_value());
        REQUIRE ((*parsed)[0].value == Approx (1.0));
    }

    SECTION ("nullopt bei Muell, Nicht-Array, fehlendem n/v, Ueberlaenge")
    {
        REQUIRE_FALSE (S::parseButtons ("").has_value());
        REQUIRE_FALSE (S::parseButtons ("kaputt").has_value());
        REQUIRE_FALSE (S::parseButtons ("{\"n\":\"x\",\"v\":0}").has_value());   // Objekt statt Array
        REQUIRE_FALSE (S::parseButtons ("[{\"n\":\"x\"}]").has_value());          // v fehlt
        REQUIRE_FALSE (S::parseButtons ("[{\"v\":0.5}]").has_value());            // n fehlt
        REQUIRE_FALSE (S::parseButtons ("[{\"n\":\"x\",\"v\":\"nan\"}]").has_value());  // v kein Zahlwert

        // 11 Einträge überschreiten maxUiButtons
        std::vector<S::ButtonPreset> tooMany;
        for (int i = 0; i < S::maxUiButtons + 1; ++i)
            tooMany.push_back ({ "P" + juce::String (i + 1), 0.1 * i });
        REQUIRE_FALSE (S::parseButtons (S::buttonsToString (tooMany)).has_value());
    }

    SECTION ("Namen werden getrimmt und auf maxUiButtonNameLength gekuerzt")
    {
        const auto parsed = S::parseButtons (
            "[{\"n\":\"  ganz langer button name weit ueber dem limit  \",\"v\":0.5}]");
        REQUIRE (parsed.has_value());
        REQUIRE ((*parsed)[0].name.length() == S::maxUiButtonNameLength);
        REQUIRE ((*parsed)[0].name.startsWith ("ganz langer"));
    }

    SECTION ("isButtonMode: nur uiMode == \"buttons\"")
    {
        juce::ValueTree param (conduit::id::parameter);
        REQUIRE_FALSE (S::isButtonMode (param));

        param.setProperty (conduit::id::paramUiMode, "buttons", nullptr);
        REQUIRE (S::isButtonMode (param));

        param.setProperty (conduit::id::paramUiMode, "fader", nullptr);
        REQUIRE_FALSE (S::isButtonMode (param));
    }
}

TEST_CASE ("Dev-Modus: setParameterCurve validiert und ist undo-faehig", "[chassis][curve]")
{
    LinkSendRig rig;
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");

    REQUIRE (rig.manager.setParameterCurve (rig.uuid(), "density", "0.9 0.1 0.9 0.4"));
    REQUIRE (density.getProperty (conduit::id::paramCurve).toString() == "0.9 0.1 0.9 0.4");

    // Leerer String = linear (Property entfernt), undo-fähig
    REQUIRE (rig.manager.setParameterCurve (rig.uuid(), "density", ""));
    REQUIRE_FALSE (density.hasProperty (conduit::id::paramCurve));
    REQUIRE (rig.undoManager.undo());
    REQUIRE (density.hasProperty (conduit::id::paramCurve));

    // Ungültig: unlesbarer String, nicht-dsp-Parameter
    REQUIRE_FALSE (rig.manager.setParameterCurve (rig.uuid(), "density", "kaputt"));
    REQUIRE_FALSE (rig.manager.setParameterCurve (rig.uuid(), "input_gain", "0.5 0.5 0.5 0.5"));
}

//==============================================================================
TEST_CASE ("Dev-Modus: setParameterUiMode toggelt undo-faehig, nur dsp", "[chassis][devmode][buttons]")
{
    LinkSendRig rig;
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");

    REQUIRE (rig.manager.setParameterUiMode (rig.uuid(), "density", true));
    REQUIRE (density.getProperty (conduit::id::paramUiMode).toString() == "buttons");
    REQUIRE (conduit::ChassisSchema::isButtonMode (density));

    // Zurück auf Fader: Property entfernt
    REQUIRE (rig.manager.setParameterUiMode (rig.uuid(), "density", false));
    REQUIRE_FALSE (density.hasProperty (conduit::id::paramUiMode));

    // Je ein Undo pro Schritt
    REQUIRE (rig.undoManager.undo());
    REQUIRE (conduit::ChassisSchema::isButtonMode (density));
    REQUIRE (rig.undoManager.undo());
    REQUIRE_FALSE (density.hasProperty (conduit::id::paramUiMode));

    // No-op erzeugt KEINE neue Transaktion (Fader → Fader)
    const auto undosBefore = rig.undoManager.getUndoDescriptions().size();
    REQUIRE (rig.manager.setParameterUiMode (rig.uuid(), "density", false));
    REQUIRE (rig.undoManager.getUndoDescriptions().size() == undosBefore);

    // Nur dsp-Parameter
    REQUIRE_FALSE (rig.manager.setParameterUiMode (rig.uuid(), "input_gain", true));
    REQUIRE_FALSE (rig.manager.setParameterUiMode (rig.uuid(), "density_cv_amt", true));
    REQUIRE_FALSE (rig.manager.setParameterUiMode (rig.uuid(), "nicht_da", true));
}

TEST_CASE ("Dev-Modus: setParameterButtonCount legt Buttons mit aktuellem Wert an, EIN Undo", "[chassis][devmode][buttons]")
{
    using S = conduit::ChassisSchema;
    LinkSendRig rig;
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");

    density.setProperty (conduit::id::paramValue, 0.42, nullptr);

    // Wachsen: P1..P3, alle mit dem aktuellen Wert
    REQUIRE (rig.manager.setParameterButtonCount (rig.uuid(), "density", 3));
    auto buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE (buttons.has_value());
    REQUIRE (buttons->size() == 3);
    REQUIRE ((*buttons)[0].name == "P1");
    REQUIRE ((*buttons)[2].name == "P3");
    REQUIRE ((*buttons)[0].value == Approx (0.42));
    REQUIRE ((*buttons)[2].value == Approx (0.42));

    // Schrumpfen entfernt von hinten
    REQUIRE (rig.manager.setParameterButtonCount (rig.uuid(), "density", 2));
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE (buttons->size() == 2);

    // EIN Undo restauriert die komplette vorige Liste
    REQUIRE (rig.undoManager.undo());
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE (buttons->size() == 3);

    // 0 entfernt das Property
    REQUIRE (rig.manager.setParameterButtonCount (rig.uuid(), "density", 0));
    REQUIRE_FALSE (density.hasProperty (conduit::id::paramUiButtons));

    // Limits + Rollen
    REQUIRE_FALSE (rig.manager.setParameterButtonCount (rig.uuid(), "density", S::maxUiButtons + 1));
    REQUIRE_FALSE (rig.manager.setParameterButtonCount (rig.uuid(), "density", -1));
    REQUIRE_FALSE (rig.manager.setParameterButtonCount (rig.uuid(), "input_gain", 2));
    REQUIRE_FALSE (rig.manager.setParameterButtonCount (rig.uuid(), "nicht_da", 2));

    // Maximal 10
    REQUIRE (rig.manager.setParameterButtonCount (rig.uuid(), "density", S::maxUiButtons));
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE (static_cast<int> (buttons->size()) == S::maxUiButtons);
    REQUIRE ((*buttons)[9].name == "P10");
}

TEST_CASE ("Dev-Modus: storeParameterButtonValue schreibt paramValue geclamped in den Button", "[chassis][devmode][buttons]")
{
    using S = conduit::ChassisSchema;
    LinkSendRig rig;
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");

    density.setProperty (conduit::id::paramValue, 0.3, nullptr);
    REQUIRE (rig.manager.setParameterButtonCount (rig.uuid(), "density", 3));

    // Kern-Workflow: Fader-Wert ändern, in Button 1 speichern
    density.setProperty (conduit::id::paramValue, 0.7, nullptr);
    REQUIRE (rig.manager.storeParameterButtonValue (rig.uuid(), "density", 1));
    auto buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[1].value == Approx (0.7));
    REQUIRE ((*buttons)[0].value == Approx (0.3));   // Nachbarn unberührt

    // Undo restauriert den alten Button-Wert
    REQUIRE (rig.undoManager.undo());
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[1].value == Approx (0.3));

    // Wert außerhalb der Hard-Range wird beim Speichern geclamped
    density.setProperty (conduit::id::paramValue, 7.5, nullptr);   // Hard-Max = 1.0
    REQUIRE (rig.manager.storeParameterButtonValue (rig.uuid(), "density", 0));
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[0].value == Approx (1.0));

    // Ungültige Indizes / Parameter
    REQUIRE_FALSE (rig.manager.storeParameterButtonValue (rig.uuid(), "density", -1));
    REQUIRE_FALSE (rig.manager.storeParameterButtonValue (rig.uuid(), "density", 3));
    REQUIRE_FALSE (rig.manager.storeParameterButtonValue (rig.uuid(), "input_gain", 0));
}

TEST_CASE ("Dev-Modus: renameParameterButton validiert und ist undo-faehig", "[chassis][devmode][buttons]")
{
    using S = conduit::ChassisSchema;
    LinkSendRig rig;
    auto density = rig.node.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");

    REQUIRE (rig.manager.setParameterButtonCount (rig.uuid(), "density", 2));

    REQUIRE (rig.manager.renameParameterButton (rig.uuid(), "density", 0, "  Dry  "));
    auto buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[0].name == "Dry");   // getrimmt
    REQUIRE ((*buttons)[1].name == "P2");

    REQUIRE (rig.undoManager.undo());
    buttons = S::parseButtons (density.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE ((*buttons)[0].name == "P1");

    // Leer / nur Whitespace / zu lang / Index daneben
    REQUIRE_FALSE (rig.manager.renameParameterButton (rig.uuid(), "density", 0, ""));
    REQUIRE_FALSE (rig.manager.renameParameterButton (rig.uuid(), "density", 0, "   "));
    REQUIRE_FALSE (rig.manager.renameParameterButton (rig.uuid(), "density", 0,
                                                      juce::String::repeatedString ("x", S::maxUiButtonNameLength + 1)));
    REQUIRE_FALSE (rig.manager.renameParameterButton (rig.uuid(), "density", 2, "Dry"));
}

//==============================================================================
TEST_CASE ("ModuleUiDefaults: Capture → Overlay bei Neu-Anlage, Presets unberuehrt", "[chassis][defaults]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    // Temp-Verzeichnis, damit der Test die echten App-Settings nicht anfasst
    juce::TemporaryFile tempMarker;
    auto options = conduit::ModuleUiDefaults::defaultOptions();
    options.applicationName = "ModuleUiDefaultsTest_"
                            + tempMarker.getFile().getFileNameWithoutExtension();
    conduit::ModuleUiDefaults defaults { options };

    // Quelle: Node mit Overrides auf zwei Parametern
    conduit::AirwindowsDensityModule source;
    auto sourceNode = source.createState();
    auto params = sourceNode.getChildWithName (conduit::id::parameters);
    params.getChildWithProperty (conduit::id::paramId, "density")
          .setProperty (conduit::id::paramUserMin, 0.2, nullptr);
    params.getChildWithProperty (conduit::id::paramId, "density")
          .setProperty (conduit::id::paramUserMax, 0.6, nullptr);
    params.getChildWithProperty (conduit::id::paramId, "highpass")
          .setProperty (conduit::id::paramUiHidden, true, nullptr);
    params.getChildWithProperty (conduit::id::paramId, "dry_wet")
          .setProperty (conduit::id::paramCurve, "0.9 0.1 0.9 0.4", nullptr);
    params.getChildWithProperty (conduit::id::paramId, "out_level")
          .setProperty (conduit::id::paramLinkSource, "density", nullptr);
    params.getChildWithProperty (conduit::id::paramId, "out_level")
          .setProperty (conduit::id::paramLinkAmount, -0.5, nullptr);

    defaults.captureFromNode (sourceNode);
    REQUIRE (defaults.hasDefaultsFor ("airwindows_density"));

    // Overlay auf einen frischen Node desselben Typs
    conduit::AirwindowsDensityModule freshModule;
    auto fresh = freshModule.createState();
    defaults.applyTo (fresh);

    auto freshParams = fresh.getChildWithName (conduit::id::parameters);
    REQUIRE ((double) freshParams.getChildWithProperty (conduit::id::paramId, "density")
                 .getProperty (conduit::id::paramUserMin) == Approx (0.2));
    REQUIRE ((bool) freshParams.getChildWithProperty (conduit::id::paramId, "highpass")
                 .getProperty (conduit::id::paramUiHidden));
    REQUIRE (freshParams.getChildWithProperty (conduit::id::paramId, "dry_wet")
                 .getProperty (conduit::id::paramCurve).toString() == "0.9 0.1 0.9 0.4");
    REQUIRE (freshParams.getChildWithProperty (conduit::id::paramId, "out_level")
                 .getProperty (conduit::id::paramLinkSource).toString() == "density");
    REQUIRE ((double) freshParams.getChildWithProperty (conduit::id::paramId, "out_level")
                 .getProperty (conduit::id::paramLinkAmount) == Approx (-0.5));

    // Anderer Modul-Typ: kein Overlay
    REQUIRE_FALSE (defaults.hasDefaultsFor ("airwindows_spiral"));

    // Capture OHNE Overrides = Reset (Eintrag gelöscht)
    conduit::AirwindowsDensityModule plainModule;
    auto plain = plainModule.createState();
    defaults.captureFromNode (plain);
    REQUIRE_FALSE (defaults.hasDefaultsFor ("airwindows_density"));

    // Aufräumen: Test-Settings-Datei löschen
    juce::PropertiesFile (options).getFile().deleteFile();
}

TEST_CASE ("ModuleUiDefaults: uiMode + uiButtons wandern durch Capture → Overlay", "[chassis][defaults][buttons]")
{
    using S = conduit::ChassisSchema;
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    juce::TemporaryFile tempMarker;
    auto options = conduit::ModuleUiDefaults::defaultOptions();
    options.applicationName = "ModuleUiDefaultsTest_"
                            + tempMarker.getFile().getFileNameWithoutExtension();
    conduit::ModuleUiDefaults defaults { options };

    // Quelle: Button-Modus mit benannter Liste (Sonderzeichen → XML-Escaping)
    const std::vector<S::ButtonPreset> buttons {
        { "Dry", 0.0 }, { juce::String::fromUTF8 ("Hälfte"), 0.5 }, { "Max", 1.0 },
    };

    conduit::AirwindowsDensityModule source;
    auto sourceNode = source.createState();
    auto density = sourceNode.getChildWithName (conduit::id::parameters)
                       .getChildWithProperty (conduit::id::paramId, "density");
    density.setProperty (conduit::id::paramUiMode, S::uiModeButtons, nullptr);
    density.setProperty (conduit::id::paramUiButtons, S::buttonsToString (buttons), nullptr);

    defaults.captureFromNode (sourceNode);
    REQUIRE (defaults.hasDefaultsFor ("airwindows_density"));

    // Overlay: beide Properties kommen an, Liste identisch
    conduit::AirwindowsDensityModule freshModule;
    auto fresh = freshModule.createState();
    defaults.applyTo (fresh);

    auto freshDensity = fresh.getChildWithName (conduit::id::parameters)
                            .getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE (S::isButtonMode (freshDensity));

    const auto applied = S::parseButtons (
        freshDensity.getProperty (conduit::id::paramUiButtons).toString());
    REQUIRE (applied.has_value());
    REQUIRE (applied->size() == 3);
    REQUIRE ((*applied)[1].name == juce::String::fromUTF8 ("Hälfte"));
    REQUIRE ((*applied)[1].value == Approx (0.5));

    juce::PropertiesFile (options).getFile().deleteFile();
}

TEST_CASE ("GraphManager: addModuleNode wendet Modul-Typ-Defaults als Overlay an", "[chassis][defaults]")
{
    LinkSendRig rig;

    juce::TemporaryFile tempMarker;
    auto options = conduit::ModuleUiDefaults::defaultOptions();
    options.applicationName = "ModuleUiDefaultsTest_"
                            + tempMarker.getFile().getFileNameWithoutExtension();
    conduit::ModuleUiDefaults defaults { options };
    rig.manager.setModuleUiDefaults (&defaults);

    // Ist-Zustand der ersten Kachel als Standard sichern
    REQUIRE (rig.manager.setParameterUserRange (rig.uuid(), "density", 0.2, 0.6));
    REQUIRE (rig.manager.captureModuleUiDefaults (rig.uuid()));

    // Neu-Anlage erbt den Standard
    const auto second = rig.manager.addModuleNode (conduit::AirwindowsDensityModule::staticModuleId, {});
    const auto density = second.getChildWithName (conduit::id::parameters)
                             .getChildWithProperty (conduit::id::paramId, "density");
    REQUIRE ((double) density.getProperty (conduit::id::paramUserMin) == Approx (0.2));
    REQUIRE ((double) density.getProperty (conduit::id::paramUserMax) == Approx (0.6));

    juce::PropertiesFile (options).getFile().deleteFile();
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
