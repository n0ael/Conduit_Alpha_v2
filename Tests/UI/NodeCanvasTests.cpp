#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "Core/Capture/LevelMeter.h"
#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ModuleFactory.h"
#include "UI/Browser/BrowserDragPayload.h"
#include "UI/NodeCanvas.h"

namespace
{

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

constexpr auto attenuatorId = conduit::AttenuatorModule::staticModuleId;

// Headless: Components werden nie auf den Desktop gehängt — der VBlank-Pfad
// feuert daher nicht, Teardowns laufen über completeTeardownNow().
struct UiTestRig
{
    UiTestRig()
    {
        conduit::registerDefaultModules (factory);
        inputLevels.prepare (48000.0, 64);
        outputLevels.prepare (48000.0, 64);
    }

    [[nodiscard]] juce::ValueTree nodes() { return root.getChildWithName (conduit::id::nodes); }

    [[nodiscard]] juce::ValueTree gainParameterOf (const juce::ValueTree& node) const
    {
        return node.getChildWithName (conduit::id::parameters)
                   .getChildWithProperty (conduit::id::paramId, "gain");
    }

    static juce::String uuidOf (const juce::ValueTree& node)
    {
        return node.getProperty (conduit::id::nodeId).toString();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::LevelMeter inputLevels;
    conduit::LevelMeter outputLevels;
    conduit::NodeCanvas canvas { root, manager, uiRegistry, nullptr, &inputLevels, &outputLevels };
};

} // namespace

//==============================================================================
TEST_CASE ("NodeCanvas: Tree → Components-Sync mit Registry-Refcounts", "[ui]")
{
    UiTestRig rig;
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);

    auto first        = rig.manager.addModuleNode (attenuatorId, { 10, 20 });
    const auto second = rig.manager.addModuleNode (attenuatorId, { 200, 80 });
    REQUIRE (first.isValid());
    REQUIRE (second.isValid());

    REQUIRE (rig.canvas.getNumNodeComponents() == 2);
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (first)) == 1);
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (second)) == 1);

    // Position: Tree ist führend, Component folgt
    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (first));
    REQUIRE (component != nullptr);
    REQUIRE (component->getX() == 10);
    REQUIRE (component->getY() == 20);

    first.setProperty (conduit::id::positionX, 99, nullptr);
    REQUIRE (component->getX() == 99);

    // Unbekannte moduleId → keine Patch-Aktion, kein Component
    REQUIRE_FALSE (rig.manager.addModuleNode ("gibt_es_nicht", {}).isValid());
    REQUIRE (rig.canvas.getNumNodeComponents() == 2);
}

//==============================================================================
TEST_CASE ("NodeCanvas: zweiphasiges Delete — UI blockiert Phase 2 bis zur Freigabe (5.3)", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    const auto nodeUuid = UiTestRig::uuidOf (node);
    rig.manager.flushPendingTopologyUpdate();  // materialisieren

    // Phase 1: Component entkoppelt sich, hält die Registry-Referenz aber
    // bis zum Ende des Render-Zyklus
    REQUIRE (rig.manager.requestNodeDelete (nodeUuid));
    auto* component = rig.canvas.findNodeComponent (nodeUuid);
    REQUIRE (component != nullptr);
    REQUIRE (component->isTearingDown());
    REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 1);

    // Phase 2 ist blockiert, solange die UI referenziert
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 1);

    // Render-Zyklus abgeschlossen (headless: explizit) → Freigabe → Phase 2
    component->completeTeardownNow();
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);
    REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 0);

    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 0);
    REQUIRE (rig.graph.getNumNodes() == 0);
}

//==============================================================================
TEST_CASE ("NodeCanvas: Undo nach Delete restauriert Node samt Component", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    const auto nodeUuid = UiTestRig::uuidOf (node);
    rig.manager.flushPendingTopologyUpdate();

    // Vollständiger Delete-Durchlauf
    REQUIRE (rig.manager.requestNodeDelete (nodeUuid));
    rig.canvas.findNodeComponent (nodeUuid)->completeTeardownNow();
    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 0);

    // Undo: Subtree kommt mit nodeState == Deleting zurück — erst der Swap
    // setzt Active und zieht damit die Component nach
    REQUIRE (rig.undoManager.undo());
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);  // noch Deleting

    rig.manager.flushPendingTopologyUpdate();
    REQUIRE (rig.nodes().getNumChildren() == 1);
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);
    REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 1);
}

//==============================================================================
TEST_CASE ("NodeCanvas: Preset-Load (Container-Austausch) rebuildet ohne Zombies", "[ui]")
{
    UiTestRig rig;
    rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);

    // "Preset": eigener Root mit zwei Nodes (voller createState-Lifecycle)
    auto preset = makeRootTree();

    for (int i = 0; i < 2; ++i)
        preset.getChildWithName (conduit::id::nodes)
              .appendChild (rig.factory.create (attenuatorId)->createState(), nullptr);

    rig.root.copyPropertiesAndChildrenFrom (preset, nullptr);

    REQUIRE (rig.canvas.getNumNodeComponents() == 2);

    for (int i = 0; i < 2; ++i)
    {
        const auto nodeUuid = UiTestRig::uuidOf (rig.nodes().getChild (i));
        REQUIRE (rig.uiRegistry.getRefCount (nodeUuid) == 1);
        REQUIRE (rig.canvas.findNodeComponent (nodeUuid) != nullptr);
    }
}

//==============================================================================
TEST_CASE ("NodeCanvas: Sequencer-Node bekommt die Grid-Kachel", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode ("sequencer", { 10, 10 });
    REQUIRE (node.isValid());

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);
    REQUIRE (component->getWidth() == 492);   // große Kachel mit StepGridDisplay
    REQUIRE (component->getHeight() == 380);  // inkl. Urzwerg-Kontrollleiste
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (node)) == 1);

    // Teardown-Flow unverändert (Timer/Listener sauber gestoppt)
    REQUIRE (rig.manager.requestNodeDelete (UiTestRig::uuidOf (node)));
    component->completeTeardownNow();
    REQUIRE (rig.uiRegistry.getRefCount (UiTestRig::uuidOf (node)) == 0);
}

//==============================================================================
TEST_CASE ("NodeComponent: I/O-Ports folgen der Hardware-Kanalzahl (Schritt B)", "[ui][io]")
{
    UiTestRig rig;

    // Externen Endpunkt registrieren (in der App: der AudioGraphIOProcessor)
    const auto graphNode = rig.graph.addNode (std::make_unique<conduit::AttenuatorModule>())->nodeID;
    rig.manager.registerExternalEndpoint (conduit::audioInputModuleId, graphNode);

    // audio_input-Tree-Node von Hand anlegen (wie EngineProcessor::ensureIONodeStates):
    // liefert Kanäle → Ausgangs-Ports, startet stereo
    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId,            juce::Uuid().toString(),                    nullptr);
    node.setProperty (conduit::id::factoryId,         conduit::audioInputModuleId,                nullptr);
    node.setProperty (conduit::id::moduleId,          "audio_in",                                 nullptr);
    node.setProperty (conduit::id::nodeState,         conduit::toString (conduit::NodeState::active), nullptr);
    node.setProperty (conduit::id::numInputChannels,  0,                                          nullptr);
    node.setProperty (conduit::id::numOutputChannels, 2,                                          nullptr);
    node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);
    rig.nodes().appendChild (node, nullptr);

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);
    REQUIRE (component->getNumOutputPorts() == 2);
    const auto stereoHeight = component->getHeight();

    SECTION ("Multichannel-Gerätewechsel: 8 Ausgangs-Ports, höhere Kachel")
    {
        node.setProperty (conduit::id::numOutputChannels, 8, nullptr);
        REQUIRE (component->getNumOutputPorts() == 8);
        REQUIRE (component->getNumInputPorts()  == 0);  // andere Bank unberührt
        REQUIRE (component->getHeight() > stereoHeight);
    }

    SECTION ("Schrumpfen zurück auf stereo stellt die Ausgangsgröße wieder her")
    {
        node.setProperty (conduit::id::numOutputChannels, 8, nullptr);
        node.setProperty (conduit::id::numOutputChannels, 2, nullptr);
        REQUIRE (component->getNumOutputPorts() == 2);
        REQUIRE (component->getHeight() == stereoHeight);
    }
}

//==============================================================================
TEST_CASE ("NodeComponent: I/O-Endpunkt zeigt eine Pegelanzeige pro Kanal (Schritt 2)", "[ui][io]")
{
    UiTestRig rig;

    const auto graphNode = rig.graph.addNode (std::make_unique<conduit::AttenuatorModule>())->nodeID;
    rig.manager.registerExternalEndpoint (conduit::audioInputModuleId, graphNode);

    juce::ValueTree node (conduit::id::node);
    node.setProperty (conduit::id::nodeId,            juce::Uuid().toString(),                        nullptr);
    node.setProperty (conduit::id::factoryId,         conduit::audioInputModuleId,                    nullptr);
    node.setProperty (conduit::id::moduleId,          "audio_in",                                     nullptr);
    node.setProperty (conduit::id::nodeState,         conduit::toString (conduit::NodeState::active), nullptr);
    node.setProperty (conduit::id::numInputChannels,  0,                                              nullptr);
    node.setProperty (conduit::id::numOutputChannels, 4,                                              nullptr);
    node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);
    rig.nodes().appendChild (node, nullptr);

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);

    SECTION ("eine Bar pro Ausgangs-Port, Kachel verbreitert")
    {
        REQUIRE (component->getNumMeterBars() == 4);
        REQUIRE (component->getWidth() == 300);  // endpointWidth mit Metern
    }

    SECTION ("Meter folgen der Hardware-Kanalzahl")
    {
        node.setProperty (conduit::id::numOutputChannels, 8, nullptr);
        REQUIRE (component->getNumMeterBars() == 8);

        node.setProperty (conduit::id::numOutputChannels, 2, nullptr);
        REQUIRE (component->getNumMeterBars() == 2);
    }
}

TEST_CASE ("NodeComponent: normale Module haben keine Pegelanzeigen", "[ui][io]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 10, 10 });
    REQUIRE (node.isValid());

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);
    REQUIRE (component->getNumMeterBars() == 0);
}

//==============================================================================
namespace
{
    juce::MouseEvent makeDragEvent (juce::Component& eventComponent,
                                    juce::Point<float> position,
                                    juce::Point<float> mouseDownPosition,
                                    bool wasDragged)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position,
                 juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &eventComponent, &eventComponent, now, mouseDownPosition, now, 1, wasDragged };
    }
}

TEST_CASE ("NodeComponent: Drag ist pixelgenau und rastet an Geschwister-Kanten ein", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 48, 48 });
    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);

    SECTION ("Kachel-Drag bewegt beide Achsen pixelgenau, der Tree zieht nach")
    {
        component->mouseDown (makeDragEvent (*component, { 10.0f, 10.0f }, { 10.0f, 10.0f }, false));
        component->mouseDrag (makeDragEvent (*component, { 41.0f, 30.0f }, { 10.0f, 10.0f }, true));

        // +31/+20 px ohne Raster — der Y-Anteil ging vor dem Positions-Write-
        // Fix verloren (X-Write setzte die Component aufs alte Tree-Y zurück)
        REQUIRE (component->getPosition() == juce::Point<int> (79, 68));
        REQUIRE ((int) node.getProperty (conduit::id::positionX) == 79);
        REQUIRE ((int) node.getProperty (conduit::id::positionY) == 68);
    }

    SECTION ("Kopfzeilen-Drag: Label-relative Events bewegen die Kachel korrekt")
    {
        // Das Titel-Label leitet als MouseListener an die Kachel weiter — hier
        // wird der weitergeleitete Pfad direkt gefüttert (Label-Koordinaten),
        // getEventRelativeTo muss auf die Kachel umrechnen
        juce::Label* title = nullptr;
        for (auto* child : component->getChildren())
            if ((title = dynamic_cast<juce::Label*> (child)) != nullptr)
                break;
        REQUIRE (title != nullptr);

        component->mouseDown (makeDragEvent (*title, { 5.0f, 5.0f }, { 5.0f, 5.0f }, false));
        component->mouseDrag (makeDragEvent (*title, { 29.0f, 29.0f }, { 5.0f, 5.0f }, true));

        REQUIRE (component->getPosition() == juce::Point<int> (72, 72));
    }

    SECTION ("Nahe Geschwister-Kanten rasten X und Y unabhängig ein")
    {
        const auto other = rig.manager.addModuleNode (attenuatorId, { 300, 100 });
        REQUIRE (other.isValid());

        // Drag endet ungesnappt bei (79, 93): Y liegt 7px unter der Oberkante
        // des Nachbarn (100) → rastet auf gleiche Höhe; X (Abstand 221) nicht
        component->mouseDown (makeDragEvent (*component, { 10.0f, 10.0f }, { 10.0f, 10.0f }, false));
        component->mouseDrag (makeDragEvent (*component, { 41.0f, 55.0f }, { 10.0f, 10.0f }, true));

        REQUIRE (component->getPosition() == juce::Point<int> (79, 100));

        // Weiter zu (305, 145): X liegt 5px neben der linken Kante des
        // Nachbarn → bündig untereinander; Y (Abstand 45) bleibt frei
        component->mouseDrag (makeDragEvent (*component, { 236.0f, 55.0f }, { 10.0f, 10.0f }, true));

        REQUIRE (component->getPosition() == juce::Point<int> (300, 145));
    }
}

//==============================================================================
TEST_CASE ("GraphManager: Parameter-Sync Tree → Atomic (UI/Preset/Undo-Pfad)", "[ui]")
{
    UiTestRig rig;

    SECTION ("Live-Änderung nach Materialisierung")
    {
        const auto node = rig.manager.addModuleNode (attenuatorId, {});
        rig.manager.flushPendingTopologyUpdate();

        auto* module = rig.manager.getModuleFor (UiTestRig::uuidOf (node));
        REQUIRE (module != nullptr);
        auto* target = module->getParameterTarget ("gain");
        REQUIRE (target != nullptr);

        rig.gainParameterOf (node).setProperty (conduit::id::paramValue, 0.25, nullptr);
        REQUIRE (juce::exactlyEqual (target->load (std::memory_order_relaxed), 0.25f));

        // Clamping auch auf diesem Pfad
        rig.gainParameterOf (node).setProperty (conduit::id::paramValue, 7.0, nullptr);
        REQUIRE (juce::exactlyEqual (target->load (std::memory_order_relaxed), 1.0f));
    }

    SECTION ("Initial-Sync beim Materialisieren (Preset-Load-Fall)")
    {
        const auto node = rig.manager.addModuleNode (attenuatorId, {});
        rig.gainParameterOf (node).setProperty (conduit::id::paramValue, 0.3, nullptr);

        rig.manager.flushPendingTopologyUpdate();  // erst jetzt materialisiert

        auto* module = rig.manager.getModuleFor (UiTestRig::uuidOf (node));
        REQUIRE (module != nullptr);
        REQUIRE (juce::exactlyEqual (module->getParameterTarget ("gain")->load (std::memory_order_relaxed),
                                     0.3f));
    }
}

//==============================================================================
// Stereo-Pairing (Schritt 2): Modell → Port-Zeilen → Doppel-Kabel

namespace
{

/** ChannelNames mit Temp-Persistenz (Muster ChannelNamesTests). */
struct TempChannelNames
{
    TempChannelNames()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitNodeCanvasPairingTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitNodeCanvasPairingTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();
        names = std::make_unique<conduit::ChannelNames> (options);
    }

    ~TempChannelNames()
    {
        names.reset();
        folder.deleteRecursively();
    }

    juce::File folder;
    std::unique_ptr<conduit::ChannelNames> names;
};

/** UiTestRig-Variante mit ChannelNames am Canvas (Pairing-UI aktiv). */
struct PairingRig
{
    PairingRig()
    {
        conduit::registerDefaultModules (factory);
        inputLevels.prepare (48000.0, 64);
        outputLevels.prepare (48000.0, 64);
    }

    /** audio_in-Endpunkt mit numChannels Hardware-Eingängen anlegen. */
    juce::ValueTree addAudioInNode (int numChannels)
    {
        const auto graphNode = graph.addNode (std::make_unique<conduit::AttenuatorModule>())->nodeID;
        manager.registerExternalEndpoint (conduit::audioInputModuleId, graphNode);

        juce::ValueTree node (conduit::id::node);
        node.setProperty (conduit::id::nodeId,            juce::Uuid().toString(),                        nullptr);
        node.setProperty (conduit::id::factoryId,         conduit::audioInputModuleId,                    nullptr);
        node.setProperty (conduit::id::moduleId,          "audio_in",                                     nullptr);
        node.setProperty (conduit::id::nodeState,         conduit::toString (conduit::NodeState::active), nullptr);
        node.setProperty (conduit::id::numInputChannels,  0,                                              nullptr);
        node.setProperty (conduit::id::numOutputChannels, numChannels,                                    nullptr);
        node.appendChild (juce::ValueTree (conduit::id::parameters), nullptr);
        root.getChildWithName (conduit::id::nodes).appendChild (node, nullptr);
        return node;
    }

    [[nodiscard]] juce::ValueTree connections() { return root.getChildWithName (conduit::id::connections); }

    /** Existiert das Kabel (source ch → dest ch) im Connections-Tree? */
    [[nodiscard]] bool hasConnection (const juce::String& sourceUuid, int sourceChannel,
                                      const juce::String& destUuid, int destChannel)
    {
        const auto tree = connections();
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto c = tree.getChild (i);
            if (c.getProperty (conduit::id::sourceNodeId).toString() == sourceUuid
                && (int) c.getProperty (conduit::id::sourceChannel) == sourceChannel
                && c.getProperty (conduit::id::destNodeId).toString() == destUuid
                && (int) c.getProperty (conduit::id::destChannel) == destChannel)
                return true;
        }
        return false;
    }

    static juce::String uuidOf (const juce::ValueTree& node)
    {
        return node.getProperty (conduit::id::nodeId).toString();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempChannelNames temp;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::LevelMeter inputLevels;
    conduit::LevelMeter outputLevels;
    conduit::NodeCanvas canvas { root, manager, uiRegistry, temp.names.get(), &inputLevels, &outputLevels };
};

} // namespace

//==============================================================================
TEST_CASE ("NodeComponent::buildPortRows: Paare verschmelzen zu span-2-Zeilen", "[ui][pairing]")
{
    using Rows = std::vector<conduit::NodeComponent::PortRow>;

    const auto pairAt = [] (std::initializer_list<int> anchors)
    {
        return [anchorList = std::vector<int> (anchors)] (int channel)
        { return std::find (anchorList.begin(), anchorList.end(), channel) != anchorList.end(); };
    };

    const auto check = [] (const Rows& rows, std::initializer_list<std::pair<int, int>> expected)
    {
        REQUIRE (rows.size() == expected.size());
        auto it = expected.begin();
        for (const auto& row : rows)
        {
            REQUIRE (row.channel == it->first);
            REQUIRE (row.span == it->second);
            ++it;
        }
    };

    // Ohne Pairing: eine Zeile pro Kanal
    check (conduit::NodeComponent::buildPortRows (3, nullptr), { { 0, 1 }, { 1, 1 }, { 2, 1 } });

    // Paar (0,1) bei 4 Kanälen: 3 Zeilen
    check (conduit::NodeComponent::buildPortRows (4, pairAt ({ 0 })),
           { { 0, 2 }, { 2, 1 }, { 3, 1 } });

    // Paar mitten drin (1,2)
    check (conduit::NodeComponent::buildPortRows (4, pairAt ({ 1 })),
           { { 0, 1 }, { 1, 2 }, { 3, 1 } });

    // Anker am LETZTEN Kanal ohne Partner: bleibt mono (ungerade Kanalzahl)
    check (conduit::NodeComponent::buildPortRows (3, pairAt ({ 2 })),
           { { 0, 1 }, { 1, 1 }, { 2, 1 } });

    // Zwei Paare
    check (conduit::NodeComponent::buildPortRows (4, pairAt ({ 0, 2 })),
           { { 0, 2 }, { 2, 2 } });
}

//==============================================================================
TEST_CASE ("NodeComponent: Stereo-Paar verschmilzt Ports, Meter bleiben pro Kanal", "[ui][pairing]")
{
    PairingRig rig;
    rig.temp.names->setActiveDevice ("TestDev", { "A", "B", "C", "D" }, {});
    rig.temp.names->setPortPairedWithNext (conduit::ChannelNames::Direction::input, 0, true);
    rig.temp.names->dispatchPendingMessages();

    const auto node = rig.addAudioInNode (4);
    auto* component = rig.canvas.findNodeComponent (PairingRig::uuidOf (node));
    REQUIRE (component != nullptr);

    // Paar (0,1): 3 Ports, aber weiterhin 4 Meter (eine Zeile pro Kanal);
    // ein Send-Toggle pro Port-ZEILE (Paar = ein Send am Anker)
    REQUIRE (component->getNumOutputPorts() == 3);
    REQUIRE (component->getNumMeterBars() == 4);
    REQUIRE (component->getNumSendButtons() == 3);
    REQUIRE (component->getWidth() == 296);  // endpointWidth − Label-Sparung + Koppel/Send

    // Kabel-Anker des Paars: derselbe Port, ∓5px versetzt (zwei enge Striche)
    const auto anchor0 = component->getPortCentre (false, 0);
    const auto anchor1 = component->getPortCentre (false, 1);
    REQUIRE (anchor0.x == anchor1.x);
    REQUIRE (anchor1.y - anchor0.y == 10);

    // Paar-Zuordnung für den Kabel-Klick-Pfad
    REQUIRE (component->pairAnchorForPort (false, 0) == 0);
    REQUIRE (component->pairAnchorForPort (false, 1) == 0);
    REQUIRE_FALSE (component->pairAnchorForPort (false, 2).has_value());

    // Entkoppeln (ChangeBroadcast) baut die Ports zurück auf mono
    rig.temp.names->setPortPairedWithNext (conduit::ChannelNames::Direction::input, 0, false);
    rig.temp.names->dispatchPendingMessages();
    REQUIRE (component->getNumOutputPorts() == 4);
    REQUIRE (component->getNumMeterBars() == 4);
    REQUIRE (component->getNumSendButtons() == 4);  // eine Zeile pro Kanal
    REQUIRE_FALSE (component->pairAnchorForPort (false, 0).has_value());

    // Send-Flag setzen (wie ein Button-Klick): Broadcast baut die Buttons
    // neu, der Zustand bleibt konsistent — normale Module tragen keine
    rig.temp.names->setPortLinkSendEnabled (conduit::ChannelNames::Direction::input, 0, true);
    rig.temp.names->dispatchPendingMessages();
    REQUIRE (component->getNumSendButtons() == 4);
    REQUIRE (rig.temp.names->isPortLinkSendEnabled (conduit::ChannelNames::Direction::input, 0));
}

//==============================================================================
TEST_CASE ("NodeCanvas: Drag vom Stereo-Port erzeugt beide Kabel, EIN Undo entfernt beide", "[ui][pairing]")
{
    PairingRig rig;
    rig.temp.names->setActiveDevice ("TestDev", { "A", "B", "C", "D" }, {});
    rig.temp.names->setPortPairedWithNext (conduit::ChannelNames::Direction::input, 0, true);
    rig.temp.names->dispatchPendingMessages();

    const auto audioIn = rig.addAudioInNode (4);
    const auto atten   = rig.manager.addModuleNode (attenuatorId, { 400, 100 });  // stereo (2 in)
    REQUIRE (atten.isValid());

    auto* inComponent    = rig.canvas.findNodeComponent (PairingRig::uuidOf (audioIn));
    auto* attenComponent = rig.canvas.findNodeComponent (PairingRig::uuidOf (atten));
    REQUIRE (inComponent != nullptr);
    REQUIRE (attenComponent != nullptr);

    SECTION ("Stereo-Ziel: zwei Connections in einer Undo-Transaktion")
    {
        // Drag vom Paar-Port (span 2) auf den ersten Attenuator-Eingang
        const auto* pairPort = inComponent->findPortNear (
            inComponent->getPortCentre (false, 0), 10);
        REQUIRE (pairPort != nullptr);
        REQUIRE (pairPort->getInfo().span == 2);

        rig.canvas.beginCableDrag (pairPort->getInfo(), { 0, 0 });
        rig.canvas.endCableDrag (attenComponent->getPosition()
                                 + attenComponent->getPortCentre (true, 0));

        REQUIRE (rig.connections().getNumChildren() == 2);
        REQUIRE (rig.hasConnection (PairingRig::uuidOf (audioIn), 0,
                                    PairingRig::uuidOf (atten), 0));
        REQUIRE (rig.hasConnection (PairingRig::uuidOf (audioIn), 1,
                                    PairingRig::uuidOf (atten), 1));

        // EIN Undo entfernt beide Kabel (eine Transaktion, 5.5)
        REQUIRE (rig.undoManager.undo());
        REQUIRE (rig.connections().getNumChildren() == 0);
    }

    SECTION ("Mono-Ziel (m+1 existiert nicht): nur das erste Kabel")
    {
        const auto monoSend = rig.manager.addModuleNode (
            conduit::LinkAudioSendModule::staticModuleId, { 400, 300 },
            [] (juce::ValueTree& tree)
            {
                using Mode = conduit::LinkAudioSendModule::InputMode;
                conduit::LinkAudioSendModule::applyInputConfig (tree, { Mode::mono });
            });
        REQUIRE ((int) monoSend.getProperty (conduit::id::numInputChannels) == 1);

        REQUIRE (rig.manager.addConnectionPair (PairingRig::uuidOf (audioIn), 0,
                                                PairingRig::uuidOf (monoSend), 0));
        REQUIRE (rig.connections().getNumChildren() == 1);  // Mono-Fallback
    }

    SECTION ("removeConnectionPair trennt beide Kabel in einer Transaktion")
    {
        REQUIRE (rig.manager.addConnectionPair (PairingRig::uuidOf (audioIn), 0,
                                                PairingRig::uuidOf (atten), 0));
        REQUIRE (rig.connections().getNumChildren() == 2);

        REQUIRE (rig.manager.removeConnectionPair (PairingRig::uuidOf (audioIn), 0,
                                                   PairingRig::uuidOf (atten), 0, 1, 1));
        REQUIRE (rig.connections().getNumChildren() == 0);

        // EIN Undo stellt beide wieder her
        REQUIRE (rig.undoManager.undo());
        REQUIRE (rig.connections().getNumChildren() == 2);
    }
}

//==============================================================================
TEST_CASE ("NodeCanvas: Browser-Drop legt das Modul an der Drop-Position an",
           "[ui][browser]")
{
    UiTestRig rig;
    rig.canvas.setSize (800, 600);

    juce::DragAndDropTarget::SourceDetails details (
        conduit::browser_drag::makeModulePayload ("airwindows_density"),
        &rig.canvas, { 300, 200 });

    REQUIRE (rig.canvas.isInterestedInDragSource (details));

    rig.canvas.itemDragEnter (details);
    rig.canvas.itemDropped (details);

    REQUIRE (rig.canvas.getNumNodeComponents() == 1);
    const auto node = rig.nodes().getChild (0);
    REQUIRE (node.getProperty (conduit::id::factoryId).toString() == "airwindows_density");
    REQUIRE ((int) node.getProperty (conduit::id::positionX) == 300);
    REQUIRE ((int) node.getProperty (conduit::id::positionY) == 200);

    // Derselbe undo-faehige Pfad wie Tap-to-Load
    REQUIRE (rig.undoManager.undo());
    REQUIRE (rig.canvas.getNumNodeComponents() == 0);
    REQUIRE (rig.undoManager.redo());
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);
}

TEST_CASE ("NodeCanvas: fremde Drag-Descriptions werden ignoriert", "[ui][browser]")
{
    UiTestRig rig;

    juce::DragAndDropTarget::SourceDetails foreign ("irgendwas", &rig.canvas, { 10, 10 });
    REQUIRE_FALSE (rig.canvas.isInterestedInDragSource (foreign));

    // Payload-Roundtrip der gemeinsamen Definition
    const auto payload = conduit::browser_drag::makeModulePayload ("lfo");
    REQUIRE (conduit::browser_drag::extractFactoryKey (payload) == "lfo");
    REQUIRE (conduit::browser_drag::extractFactoryKey ("lfo").isEmpty());
}
