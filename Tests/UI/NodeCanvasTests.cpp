#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

#include "Core/Capture/LevelMeter.h"
#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Core/PageManager.h"
#include "Core/UiSettings.h"
#include "UI/PageOverviewComponent.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ModuleFactory.h"
#include "Modules/ScopeModule.h"
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
TEST_CASE ("NodeCanvas: Scope-Node bekommt die Waveform-Kachel", "[ui]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (conduit::ScopeModule::staticModuleId, { 10, 10 });
    REQUIRE (node.isValid());

    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);
    REQUIRE (component->getWidth() == 252);   // Kachel mit ScopeDisplay
    REQUIRE (component->getHeight() == 168);
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
TEST_CASE ("Looper-In-Kachel: Ports fluchten mit den Slot-Zeilen", "[ui][looper]")
{
    PairingRig rig;
    const auto node = rig.manager.addModuleNode ("looper_patch_in", { 200, 100 });
    REQUIRE (node.isValid());

    auto* component = rig.canvas.findNodeComponent (PairingRig::uuidOf (node));
    REQUIRE (component != nullptr);

    // Default 4× stereo + 4× mono = 8 Zeilen; ein Stereo-Paar teilt seinen
    // Port (∓5px-Kabel-Anker) auf der Zeilen-Mitte
    const auto slot0a = component->getPortCentre (true, 0);
    const auto slot0b = component->getPortCentre (true, 1);
    REQUIRE (slot0a.x == slot0b.x);
    REQUIRE (slot0b.y - slot0a.y == 10);
    const auto row0 = (slot0a.y + slot0b.y) / 2;

    // Zeilenraster 30px (LooperPatchInPanel::rowHeight) statt Gleichverteilung
    // über die Kachel-Höhe — Ports und Panel-Zeilen fluchten horizontal
    const auto row1 = (component->getPortCentre (true, 2).y
                       + component->getPortCentre (true, 3).y) / 2;
    REQUIRE (row1 - row0 == 30);
    REQUIRE (component->getPortCentre (true, 8).y  - row0 == 4 * 30);   // "In 5" (mono)
    REQUIRE (component->getPortCentre (true, 11).y - row0 == 7 * 30);   // "In 8" (mono)

    // Pass-Through: Ein- und Ausgangs-Bank fluchten identisch
    REQUIRE (component->getPortCentre (false, 8).y
             == component->getPortCentre (true, 8).y);
}

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

//==============================================================================
// Viewport (ADR 008 M3a): Transform, Persistenz, Interaktions-Sperre

TEST_CASE ("NodeCanvas: Viewport-Transform — Interaktionen bleiben unter Zoom korrekt", "[ui][canvas]")
{
    UiTestRig rig;
    rig.canvas.setSize (800, 600);

    const auto node = rig.manager.addModuleNode (attenuatorId, { 100, 80 });
    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);

    // Default: Identität — Content-Position == Canvas-Position (Alt-Verhalten)
    REQUIRE (juce::exactlyEqual (rig.canvas.getViewState().zoom, 1.0));
    REQUIRE (rig.canvas.findConnectionAt ({ 5, 5 }).isValid() == false);

    // Zoom 0.5 + Offset: JUCE rechnet die Component-Koordinaten durch den
    // Content-Transform — die Kachel erscheint am erwarteten Screen-Punkt
    rig.canvas.setViewState ({ 40.0, 20.0, 0.5 });

    const auto screenTopLeft = rig.canvas.getLocalPoint (component, juce::Point<int> (0, 0));
    REQUIRE (screenTopLeft == juce::Point<int> (40 + 50, 20 + 40));

    // Sub-Pixel-Offsets werden beim ANWENDEN auf ganze Pixel gerundet
    // (Anti-Zitter, 18.07.2026) — der View-State selbst bleibt genau
    rig.canvas.setViewState ({ 40.7, 20.3, 0.5 });
    REQUIRE (juce::exactlyEqual (rig.canvas.getViewState().offsetX, 40.7));
    REQUIRE (rig.canvas.getLocalPoint (component, juce::Point<int> (0, 0))
             == juce::Point<int> (41 + 50, 20 + 40));

    rig.canvas.setViewState ({ 40.0, 20.0, 0.5 });   // zurück für den Drop-Check

    // Drop unter Zoom: Canvas-Position (300, 200) → Tree-Position im
    // Content-Raum ((300-40)/0.5, (200-20)/0.5)
    juce::DragAndDropTarget::SourceDetails details (
        conduit::browser_drag::makeModulePayload (attenuatorId), &rig.canvas, { 300, 200 });
    rig.canvas.itemDropped (details);

    const auto dropped = rig.nodes().getChild (rig.nodes().getNumChildren() - 1);
    REQUIRE ((int) dropped.getProperty (conduit::id::positionX) == 520);
    REQUIRE ((int) dropped.getProperty (conduit::id::positionY) == 360);
}

TEST_CASE ("NodeCanvas: Viewport persistiert in den Page-Properties und restauriert", "[ui][canvas]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    // Root MIT Pages-Zweig (M1-Schema) — der Canvas bindet an Pages[0]
    auto root = makeRootTree();
    juce::ValueTree pages (conduit::id::pages);
    juce::ValueTree page (conduit::id::page);
    page.setProperty (conduit::id::pageUuid, juce::Uuid().toString(), nullptr);
    page.setProperty (conduit::id::pageGridX, 0, nullptr);
    page.setProperty (conduit::id::pageGridY, 0, nullptr);
    page.setProperty (conduit::id::viewOffsetX, 12.5, nullptr);
    page.setProperty (conduit::id::viewOffsetY, -30.0, nullptr);
    page.setProperty (conduit::id::viewZoom, 1.5, nullptr);
    pages.appendChild (page, nullptr);
    root.appendChild (pages, nullptr);

    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };

    conduit::NodeCanvas canvas { root, manager, uiRegistry };

    // Ctor restauriert den gespeicherten Viewport der aktiven Seite
    REQUIRE (juce::exactlyEqual (canvas.getViewState().zoom, 1.5));
    REQUIRE (juce::exactlyEqual (canvas.getViewState().offsetX, 12.5));
    REQUIRE (juce::exactlyEqual (canvas.getViewState().offsetY, -30.0));

    // setViewState schreibt zurück (ohne Undo — View-State)
    canvas.setViewState ({ 100.0, 50.0, 0.8 });
    REQUIRE (juce::exactlyEqual ((double) page.getProperty (conduit::id::viewZoom), 0.8));
    REQUIRE (juce::exactlyEqual ((double) page.getProperty (conduit::id::viewOffsetX), 100.0));
    REQUIRE_FALSE (undoManager.canUndo());

    // Clamp: überhöhter Zoom wird auf die Range geklemmt
    canvas.setViewState ({ 0.0, 0.0, 99.0 });
    REQUIRE (juce::exactlyEqual (canvas.getViewState().zoom, 2.0));
}

TEST_CASE ("NodeCanvas: Interaktions-Sperre unter der Zoom-Schwelle", "[ui][canvas]")
{
    UiTestRig rig;

    // Ohne UiSettings gilt die Default-Schwelle (0.5)
    rig.canvas.setViewState ({ 0.0, 0.0, 1.0 });
    REQUIRE_FALSE (rig.canvas.isInteractionLocked());

    rig.canvas.setViewState ({ 0.0, 0.0, 0.5 });
    REQUIRE_FALSE (rig.canvas.isInteractionLocked());   // Schwelle inklusiv

    rig.canvas.setViewState ({ 0.0, 0.0, 0.49 });
    REQUIRE (rig.canvas.isInteractionLocked());

    rig.canvas.setViewState ({ 0.0, 0.0, 0.51 });
    REQUIRE_FALSE (rig.canvas.isInteractionLocked());
}

//==============================================================================
// Seiten-Navigation (ADR 008 M3b)

namespace
{

/** UiTestRig-Variante MIT PageManager (Seiten-Filter aktiv). */
struct PagesRig
{
    PagesRig()
    {
        conduit::registerDefaultModules (factory);
        pageManager.migrateAndRepair();
    }

    [[nodiscard]] juce::ValueTree nodes() { return root.getChildWithName (conduit::id::nodes); }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    juce::ValueTree root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::PageManager pageManager { root, undoManager };
    conduit::NodeCanvas canvas { root, manager, uiRegistry, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, &pageManager };
};

} // namespace

TEST_CASE ("NodeCanvas: Seiten-Filter zeigt nur die aktive Seite (M3b)", "[ui][canvas][pages]")
{
    PagesRig rig;
    rig.manager.setPageManager (&rig.pageManager);
    rig.canvas.setSize (800, 600);

    const auto nodeA = rig.manager.addModuleNode (attenuatorId, { 100, 100 });
    const auto nodeB = rig.manager.addModuleNode (attenuatorId, { 300, 100 });
    REQUIRE (rig.canvas.getNumNodeComponents() == 2);

    // Node B auf eine zweite Seite verschieben → Component verschwindet
    const auto east = rig.pageManager.createPage (1, 0);
    REQUIRE (rig.pageManager.setNodePage (UiTestRig::uuidOf (nodeB), east));
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);
    REQUIRE (rig.canvas.findNodeComponent (UiTestRig::uuidOf (nodeA)) != nullptr);

    // Navigation zur Ost-Seite → nur Node B sichtbar
    rig.canvas.navigatePages (1, 0);
    REQUIRE (rig.canvas.getNumNodeComponents() == 1);
    REQUIRE (rig.canvas.findNodeComponent (UiTestRig::uuidOf (nodeB)) != nullptr);

    // Navigation ins Leere legt eine neue Seite an (undo-fähig), Canvas leer
    const auto pagesBefore = rig.root.getChildWithName (conduit::id::pages).getNumChildren();
    rig.canvas.navigatePages (0, 1);
    CHECK (rig.root.getChildWithName (conduit::id::pages).getNumChildren() == pagesBefore + 1);
    CHECK (rig.canvas.getNumNodeComponents() == 0);
}

TEST_CASE ("NodeCanvas: Viewport reist pro Seite (M3b)", "[ui][canvas][pages]")
{
    PagesRig rig;
    rig.canvas.setSize (800, 600);

    rig.canvas.setViewState ({ 50.0, 25.0, 1.5 });
    rig.canvas.navigatePages (1, 0);   // neue Seite → Default-Viewport

    REQUIRE (juce::exactlyEqual (rig.canvas.getViewState().zoom, 1.0));

    rig.canvas.navigatePages (-1, 0);  // zurück → gespeicherter Viewport
    REQUIRE (juce::exactlyEqual (rig.canvas.getViewState().zoom, 1.5));
    REQUIRE (juce::exactlyEqual (rig.canvas.getViewState().offsetX, 50.0));
}

TEST_CASE ("NodeComponent: Doppel-Tap armiert, zweiter Doppel-Tap löscht (M3b)", "[ui][canvas]")
{
    UiTestRig rig;
    const auto node = rig.manager.addModuleNode (attenuatorId, { 60, 60 });
    auto* component = rig.canvas.findNodeComponent (UiTestRig::uuidOf (node));
    REQUIRE (component != nullptr);

    REQUIRE_FALSE (component->isDeleteArmed());

    component->mouseDoubleClick (makeDragEvent (*component, { 20.0f, 20.0f },
                                                { 20.0f, 20.0f }, false));
    REQUIRE (component->isDeleteArmed());
    REQUIRE_FALSE (component->isTearingDown());   // armiert ≠ gelöscht

    component->mouseDoubleClick (makeDragEvent (*component, { 20.0f, 20.0f },
                                                { 20.0f, 20.0f }, false));
    REQUIRE (component->isTearingDown());         // Phase 1 läuft
}

//==============================================================================
// Birdeye + Seiten-Übersicht (ADR 008 M4)

TEST_CASE ("NodeCanvas: Birdeye-Toggle — Pegel, Sperre, Rückkehr auf Arbeits-Zoom", "[ui][canvas][birdeye]")
{
    UiTestRig rig;
    rig.canvas.setSize (800, 600);
    rig.canvas.setViewState ({ 0.0, 0.0, 1.0 });

    rig.canvas.toggleBirdeye();
    REQUIRE (rig.canvas.isBirdeyeActive());
    CHECK (rig.canvas.getViewState().zoom
           == Approx ((double) conduit::UiSettings::defaultBirdeyeZoom));
    CHECK (rig.canvas.isInteractionLocked());   // Übersicht = nur Navigation

    rig.canvas.toggleBirdeye();
    REQUIRE_FALSE (rig.canvas.isBirdeyeActive());
    CHECK (rig.canvas.getViewState().zoom
           == Approx ((double) conduit::UiSettings::defaultWorkZoom));
    CHECK_FALSE (rig.canvas.isInteractionLocked());
}

TEST_CASE ("PageOverview: Kacheln, Sprung per Tap, Regel-a-Löschen", "[ui][canvas][pages]")
{
    PagesRig rig;

    const auto defaultUuid = rig.pageManager.getActivePageUuid();
    const auto emptyUuid = rig.pageManager.createPage (1, 0);

    conduit::PageOverviewComponent overview { rig.root, rig.pageManager };
    overview.setSize (900, 600);

    // Beide Seiten haben Kacheln im Grid
    const auto defaultTile = overview.tileBoundsFor (defaultUuid);
    const auto emptyTile = overview.tileBoundsFor (emptyUuid);
    REQUIRE_FALSE (defaultTile.isEmpty());
    REQUIRE_FALSE (emptyTile.isEmpty());
    CHECK (emptyTile.getX() > defaultTile.getX());   // gridX+1 → rechts

    // Tap auf eine Kachel meldet die Seite (Sprung übernimmt der Canvas)
    juce::String chosen;
    overview.onPageChosen = [&chosen] (const juce::String& uuid) { chosen = uuid; };

    overview.mouseUp (makeDragEvent (overview, defaultTile.getCentre().toFloat(),
                                     defaultTile.getCentre().toFloat(), false));
    CHECK (chosen == defaultUuid);

    // Regel a: × auf der LEEREN, nicht-aktiven Kachel löscht die Seite
    const juce::Point<float> closePoint ((float) emptyTile.getRight() - 14.0f,
                                         (float) emptyTile.getY() + 14.0f);
    overview.mouseUp (makeDragEvent (overview, closePoint, closePoint, false));
    CHECK_FALSE (rig.pageManager.findPageByUuid (emptyUuid).isValid());

    // Die AKTIVE Kachel hat nie eine ×-Zone (auch wenn leer) — Klick wählt
    chosen.clear();
    const juce::Point<float> activeClose ((float) defaultTile.getRight() - 14.0f,
                                          (float) defaultTile.getY() + 14.0f);
    overview.mouseUp (makeDragEvent (overview, activeClose, activeClose, false));
    CHECK (chosen == defaultUuid);
    CHECK (rig.pageManager.findPageByUuid (defaultUuid).isValid());
}

TEST_CASE ("NodeCanvas: togglePageOverview zeigt/versteckt das Overlay", "[ui][canvas][pages]")
{
    PagesRig rig;
    rig.canvas.setSize (800, 600);

    REQUIRE_FALSE (rig.canvas.isPageOverviewVisible());
    rig.canvas.togglePageOverview();
    REQUIRE (rig.canvas.isPageOverviewVisible());
    rig.canvas.togglePageOverview();
    REQUIRE_FALSE (rig.canvas.isPageOverviewVisible());
}

TEST_CASE ("NodeCanvas: Sperre blockiert auch Kabel-Trennen und Doppel-Tap", "[ui][canvas]")
{
    UiTestRig rig;
    rig.canvas.setSize (800, 600);

    const auto a = rig.manager.addModuleNode (attenuatorId, { 50, 50 });
    const auto b = rig.manager.addModuleNode (attenuatorId, { 400, 50 });
    REQUIRE (rig.manager.addConnection (UiTestRig::uuidOf (a), 0, UiTestRig::uuidOf (b), 0));

    const auto connections = rig.root.getChildWithName (conduit::id::connections);
    REQUIRE (connections.getNumChildren() == 1);

    // Kabel-Startpunkt (Output-Port von a) — liegt exakt auf dem Kabelpfad
    auto* compA = rig.canvas.findNodeComponent (UiTestRig::uuidOf (a));
    REQUIRE (compA != nullptr);
    const auto cableStart = compA->getPosition() + compA->getPortCentre (false, 0);

    // Gesperrt (Zoom 0.3): Klick auf das Kabel trennt NICHT (18.07.2026)
    rig.canvas.setViewState ({ 0.0, 0.0, 0.3 });
    REQUIRE (rig.canvas.isInteractionLocked());

    const auto lockedClick = cableStart.toFloat() * 0.3f;   // Screen-Position
    rig.canvas.mouseDown (makeDragEvent (rig.canvas, lockedClick, lockedClick, false));
    REQUIRE (connections.getNumChildren() == 1);

    // ... und Doppel-Tap legt KEIN Modul an
    const auto nodeCount = rig.canvas.getNumNodeComponents();
    rig.canvas.mouseDoubleClick (makeDragEvent (rig.canvas, { 300.0f, 300.0f },
                                                { 300.0f, 300.0f }, false));
    REQUIRE (rig.canvas.getNumNodeComponents() == nodeCount);

    // Entsperrt (Identität): derselbe Klick trennt das Kabel
    rig.canvas.setViewState ({ 0.0, 0.0, 1.0 });
    rig.canvas.mouseDown (makeDragEvent (rig.canvas, cableStart.toFloat(),
                                         cableStart.toFloat(), false));
    REQUIRE (connections.getNumChildren() == 0);
}
