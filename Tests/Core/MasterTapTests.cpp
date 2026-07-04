#include <array>

#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Modules/AttenuatorModule.h"
#include "TestSettingsFolder.h"

namespace
{

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

juce::String uuidOf (const juce::ValueTree& node)
{
    return node.getProperty (conduit::id::nodeId).toString();
}

} // namespace

//==============================================================================
TEST_CASE ("Master-Output-Tap: master_l/_r sind ab Konstruktion registriert", "[looper][capture]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    const auto& capture = engine.getCaptureService();

    const auto left  = capture.getVirtualChannelUiInfo (0);
    const auto right = capture.getVirtualChannelUiInfo (1);

    REQUIRE (left.inUse);
    REQUIRE (left.name == "master_l");
    REQUIRE (right.inUse);
    REQUIRE (right.name == "master_r");

    // Vor prepare() trägt noch kein Puffersatz die Slots
    REQUIRE (left.captureIndex == -1);

    // Nach prepare() liegen die Master-Kanäle hinter den Hardware-Kanälen
    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);
    REQUIRE (capture.getVirtualChannelUiInfo (0).captureIndex == 2);
    REQUIRE (capture.getVirtualChannelUiInfo (1).captureIndex == 3);
}

//==============================================================================
TEST_CASE ("Master-Output-Tap: Session-Summe landet im Ring, der Metronom-Click nicht",
           "[looper][capture]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& manager = engine.getGraphManager();
    auto& capture = engine.getCaptureService();

    // Passthrough-Patch: Audio In → Attenuator (gain 1) → Audio Out —
    // damit trägt der Buffer nach dem Graph die Session-Summe (Muster
    // PatchingTests "erster hörbarer Patch")
    auto nodes = engine.getRootState().getChildWithName (conduit::id::nodes);
    const auto ioIn  = nodes.getChildWithProperty (conduit::id::factoryId,
                                                   juce::String (conduit::audioInputModuleId));
    const auto ioOut = nodes.getChildWithProperty (conduit::id::factoryId,
                                                   juce::String (conduit::audioOutputModuleId));

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    const auto att = manager.addModuleNode (attenuatorId, { 300, 200 });
    REQUIRE (att.isValid());

    for (int channel = 0; channel < 2; ++channel)
    {
        REQUIRE (manager.addConnection (uuidOf (ioIn), channel, uuidOf (att), channel));
        REQUIRE (manager.addConnection (uuidOf (att), channel, uuidOf (ioOut), channel));
    }

    juce::AudioBuffer<float> buffer (2, 480);
    juce::MidiBuffer midi;

    float maxBufferSample = 0.0f;
    const auto runBlocks = [&] (int count)
    {
        for (int i = 0; i < count; ++i)
        {
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), 1.0f, 480);

            engine.processBlock (buffer, midi);
            maxBufferSample = juce::jmax (maxBufferSample, buffer.getMagnitude (0, 480));
        }
    };

    // Fade-Swap durchpumpen (5.2), dann Fade-In ausklingen lassen
    manager.flushPendingTopologyUpdate();
    for (int i = 0; i < 100 && manager.isWaitingForSilence(); ++i)
    {
        runBlocks (1);
        manager.flushPendingTopologyUpdate();
    }
    runBlocks (12);
    REQUIRE (buffer.getMagnitude (0, 480) > 0.9f);  // Summe kommt am Ausgang an

    // Volle Summe (RMS 0 dB) reißt die Gate-Schwelle der Master-Kanäle;
    // der RAM-Wächter publiziert die Pool-Segmente
    capture.runRamGuard();
    runBlocks (20);

    const auto leftIndex = capture.getVirtualChannelUiInfo (0).captureIndex;
    REQUIRE (leftIndex == 2);
    const auto* master = capture.getChannel (leftIndex);
    REQUIRE (master != nullptr);
    REQUIRE (master->getState() == conduit::CaptureChannel::State::recording);

    // Metronom an, Anker-Paar 0 = dieselben Kanäle wie der Tap. Kein
    // Restaurieren nötig: die TransportSettings leben im Temp-Ordner des
    // Tests, nicht in der echten Transport.settings. Pumpen, bis ein Click
    // im Ausgangs-Buffer nachweisbar ist: Onset = 1.0 + 0.4
    auto& transport = engine.getTransportSettings();
    transport.setMetronomeEnabled (true);
    transport.setMetronomeAnchor (0);
    transport.dispatchPendingMessages();

    maxBufferSample = 0.0f;
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (maxBufferSample < 1.2f && juce::Time::getMillisecondCounter() < deadline)
        runBlocks (1);
    REQUIRE (maxBufferSample > 1.2f);  // Click ist im Master-Out hörbar …

    // … aber der Ring trägt weiter die reine Graph-Summe: die jüngsten
    // Samples sind exakt 1.0 — der Tap liegt VOR dem Metronom
    const auto range = master->getReadableRange();
    REQUIRE (range.to > range.from + 480);

    std::array<float, 480> ring {};
    REQUIRE (master->read (range.to - 480, ring.data(), 480));

    float ringMin = ring[0], ringMax = ring[0];
    for (const auto sample : ring)
    {
        ringMin = juce::jmin (ringMin, sample);
        ringMax = juce::jmax (ringMax, sample);
    }

    REQUIRE (ringMax < 1.0001f);
    REQUIRE (ringMin > 0.999f);
}
