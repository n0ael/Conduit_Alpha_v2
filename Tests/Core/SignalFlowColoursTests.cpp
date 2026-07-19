#include <catch2/catch_test_macros.hpp>

#include "Core/ChannelNames.h"
#include "Core/SignalFlowColours.h"
#include "Modules/ConduitModule.h"

using namespace conduit;
using Direction = ChannelNames::Direction;

namespace
{

/** Persistenz in ein Temp-Verzeichnis statt in die echte Settings-Datei. */
struct TempSettingsFolder
{
    TempSettingsFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitFlowColoursTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettingsFolder() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitFlowColoursTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();
        return o;
    }

    juce::File folder;
};

/** Minimal-Root mit Nodes[]/Connections[] (Schema 6.2). */
struct TreeRig
{
    juce::ValueTree root { id::root };
    juce::ValueTree nodes { id::nodes };
    juce::ValueTree connections { id::connections };

    TreeRig()
    {
        root.appendChild (nodes, nullptr);
        root.appendChild (connections, nullptr);
    }

    juce::String addNode (const juce::String& factoryKey, const juce::String& moduleId,
                          juce::uint32 nodeColour = 0)
    {
        juce::ValueTree node (id::node);
        const auto uuid = juce::Uuid().toString();
        node.setProperty (id::nodeId, uuid, nullptr);
        node.setProperty (id::factoryId, factoryKey, nullptr);
        node.setProperty (id::moduleId, moduleId, nullptr);
        if (nodeColour != 0)
            node.setProperty (id::nodeColour, (int) nodeColour, nullptr);
        nodes.appendChild (node, nullptr);
        return uuid;
    }

    void connect (const juce::String& srcUuid, int srcCh,
                  const juce::String& dstUuid, int dstCh)
    {
        juce::ValueTree c (id::connection);
        c.setProperty (id::sourceNodeId,  srcUuid, nullptr);
        c.setProperty (id::sourceChannel, srcCh,   nullptr);
        c.setProperty (id::destNodeId,    dstUuid, nullptr);
        c.setProperty (id::destChannel,   dstCh,   nullptr);
        connections.appendChild (c, nullptr);
    }
};

} // namespace

//==============================================================================
TEST_CASE ("flow_colours: Vererbung Eingang → FX-Kette → Ziel", "[flowcolours]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettingsFolder temp;
    ChannelNames names (temp.options());
    names.setActiveDevice ("TestDev", { "mopho", "micro freak" }, {});
    names.setColour (Direction::input, 0, 0xcc4455u);

    TreeRig rig;
    const auto audioIn = rig.addNode (audioInputModuleId, audioInputModuleId);
    const auto fx      = rig.addNode ("airwindows_galactic", "galactic_1");
    const auto fx2     = rig.addNode ("airwindows_verbtiny", "verbtiny_1");
    rig.connect (audioIn, 0, fx, 0);
    rig.connect (fx, 0, fx2, 0);

    SECTION ("FX ohne eigene Farbe erben die Kanal-Farbe des Eingangs")
    {
        const auto colours = flow_colours::computeAll (rig.root, &names);
        REQUIRE (colours.at (fx)  == 0xcc4455u);
        REQUIRE (colours.at (fx2) == 0xcc4455u);
    }

    SECTION ("Explizite nodeColour gewinnt und vererbt sich weiter")
    {
        rig.nodes.getChildWithProperty (id::nodeId, fx)
                 .setProperty (id::nodeColour, (int) 0x2266ffu, nullptr);

        const auto colours = flow_colours::computeAll (rig.root, &names);
        REQUIRE (colours.at (fx)  == 0x2266ffu);
        REQUIRE (colours.at (fx2) == 0x2266ffu);
    }

    SECTION ("resolveDestSourceRgb: Ziel-Eingang → geerbte Quellfarbe durch die Kette")
    {
        const auto looperPatchIn = rig.addNode ("looper_patch_in", "looper_patch_in_1");
        rig.connect (fx2, 0, looperPatchIn, 0);

        REQUIRE (flow_colours::resolveDestSourceRgb (rig.root, &names, looperPatchIn, 0)
                 == 0xcc4455u);

        // Unverkabelter Kanal → 0 (Aufrufer fällt auf die Modul-Farbe zurück)
        REQUIRE (flow_colours::resolveDestSourceRgb (rig.root, &names, looperPatchIn, 1) == 0);
    }

    SECTION ("resolveDestSourceRgb: direkt am Interface-Kanal")
    {
        const auto looperPatchIn = rig.addNode ("looper_patch_in", "looper_patch_in_1");
        rig.connect (audioIn, 0, looperPatchIn, 0);

        REQUIRE (flow_colours::resolveDestSourceRgb (rig.root, &names, looperPatchIn, 0)
                 == 0xcc4455u);
    }

    SECTION ("Zwei farbige Quellen mischen sich (blendRgb)")
    {
        names.setColour (Direction::input, 1, 0x2266ffu);
        const auto mixer = rig.addNode ("utility_mixer", "mixer_1");
        rig.connect (audioIn, 0, mixer, 0);
        rig.connect (audioIn, 1, mixer, 1);

        const auto colours = flow_colours::computeAll (rig.root, &names);
        REQUIRE (colours.at (mixer)
                 == flow_colours::blendRgb ({ 0xcc4455u, 0x2266ffu }));
    }

    SECTION ("Feedback-Schleife terminiert (visiting-Set)")
    {
        rig.connect (fx2, 0, fx, 1);   // Zyklus fx → fx2 → fx
        const auto colours = flow_colours::computeAll (rig.root, &names);
        REQUIRE (colours.at (fx) == 0xcc4455u);   // Eingangs-Farbe bleibt maßgeblich
    }
}
