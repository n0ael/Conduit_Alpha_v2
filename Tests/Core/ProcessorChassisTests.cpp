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
