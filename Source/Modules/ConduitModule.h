#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

namespace conduit
{

//==============================================================================
// ValueTree-Identifier nach Schema (CLAUDE.md 6.2)
namespace id
{
    // Root
    inline const juce::Identifier root              { "ConduitRoot" };
    inline const juce::Identifier nodes             { "Nodes" };
    inline const juce::Identifier connections       { "Connections" };
    inline const juce::Identifier calibrationProfiles { "CalibrationProfiles" };
    inline const juce::Identifier audioSetupWarning { "audioSetupWarning" };

    // Node
    inline const juce::Identifier node          { "Node" };
    inline const juce::Identifier nodeId        { "nodeId" };
    inline const juce::Identifier type          { "type" };
    inline const juce::Identifier moduleId      { "moduleId" };
    inline const juce::Identifier stateVersion  { "stateVersion" };
    inline const juce::Identifier nodeState     { "nodeState" };
    inline const juce::Identifier nodeError     { "nodeError" };
    inline const juce::Identifier positionX     { "x" };
    inline const juce::Identifier positionY     { "y" };

    // Parameters
    inline const juce::Identifier parameters    { "Parameters" };
    inline const juce::Identifier parameter     { "Parameter" };
    inline const juce::Identifier paramId       { "id" };
    inline const juce::Identifier paramValue    { "value" };
    inline const juce::Identifier paramMin      { "min" };
    inline const juce::Identifier paramMax      { "max" };
    inline const juce::Identifier paramDefault  { "default" };

    // Connections
    inline const juce::Identifier connection    { "Connection" };
    inline const juce::Identifier sourceNodeId  { "sourceNodeId" };
    inline const juce::Identifier sourceChannel { "sourceChannel" };
    inline const juce::Identifier destNodeId    { "destNodeId" };
    inline const juce::Identifier destChannel   { "destChannel" };
} // namespace id

//==============================================================================
// Modul-Kategorien für den GraphManager (CLAUDE.md 4.1)
enum class ModuleType
{
    generator,   // LFO, Envelope, Sequencer, MIDI→CV
    processor,   // Gate, EQ, Compressor, PluginModule (v2.x)
    io,          // HardwareIO (ES-3, ESX-8CV, ...), NetworkIO (OSC)
    analysis,    // Scope, Tuner, FFT, CVTunerModule
    utility      // Mixer, Attenuator, DC Block, Math, Offset
};

// Lifecycle-Zustand eines Nodes (CLAUDE.md 5.3)
enum class NodeState
{
    active,
    fadingOut,
    fadingIn,
    deleting
};

[[nodiscard]] juce::String toString (ModuleType type);
[[nodiscard]] juce::String toString (NodeState state);

//==============================================================================
/**
    Abstrakte Basisklasse aller Conduit-Module (CLAUDE.md 4).

    Jedes Modul ist ein eigenständiger AudioProcessor im AudioProcessorGraph.
    Zustand lebt zentral im Root-ValueTree (Single Source of Truth) — Module
    serialisieren sich nicht selbst.

    Subklassen implementieren zusätzlich prepareToPlay(), releaseResources()
    und processBlock() (lock-free, allocation-free — CLAUDE.md 3.1).
*/
class ConduitModule : public juce::AudioProcessor
{
public:
    explicit ConduitModule (const BusesProperties& buses);
    ~ConduitModule() override = default;

    //==========================================================================
    // Pflicht-Methoden (CLAUDE.md 4.4)

    /** Lazy: wird vom GraphManager VOR addNode() aufgerufen — nie im Konstruktor.
        Die Basis-Implementierung erzeugt das Node-Skelett nach Schema 6.2 und
        ruft appendParametersTo() für die modulspezifischen Parameter auf.
        Läuft auf dem Message Thread. */
    [[nodiscard]] virtual juce::ValueTree createState();

    /** named_id für den OSC-Pfad /conduit/{type}/{named_id}/value,
        z.B. "neutron_filter". Persistent über Ableton-Neustarts. */
    [[nodiscard]] virtual juce::String getModuleId() const = 0;

    /** Lokalisierter UI-Name — bewusst getrennt von getModuleId(). */
    [[nodiscard]] virtual juce::String getModuleDisplayName() const = 0;

    /** ModuleType enum für den GraphManager. */
    [[nodiscard]] virtual ModuleType getType() const = 0;

    /** Versionsnummer des Subtrees für Serialisierungs-Migration
        (Rückwärtskompatibilität). */
    [[nodiscard]] virtual int getStateVersion() const = 0;

    //==========================================================================
    /** Schritt 1 des Graph-Swaps (CLAUDE.md 5.2, Async Prepare): wird vom
        GraphManager manuell VOR addNode() aufgerufen. Speicherintensive
        Allokationen (Delay-Buffer etc.) hier abschließen.

        JUCEs prepareToPlay() gibt void zurück — Fehler werden deshalb über
        dieses Result gemeldet: bei failed() schreibt der GraphManager
        nodeError ins ValueTree, das Modul kommt nicht in den Graph,
        kein Crash, kein Retry-Loop.

        Basis-Implementierung: setPlayConfigDetails() + prepareToPlay().
        Muss idempotent sein — der Graph ruft prepareToPlay() später erneut. */
    [[nodiscard]] virtual juce::Result prepareForGraph (double sampleRate, int maximumBlockSize);

    //==========================================================================
    /** Atomic-Schreibziel für Echtzeit-Parameter-Updates (Dual-State 6.1).

        Generische Parameter-API — das Modul weiß nichts von OSC (7.1, Single
        Responsibility). nullptr, wenn der Parameter kein Echtzeit-Ziel hat;
        Werte-Clamping auf [min, max] übernimmt der Schreiber (OscController)
        anhand des Parameters-Subtrees.

        Aufruf auf dem Message Thread (Registrierung). Der zurückgegebene
        Atomic wird von Fremd-Threads per store() beschrieben und vom Audio
        Thread in processBlock() gelesen. Der Pointer muss bis zur Zerstörung
        des Moduls gültig bleiben. */
    [[nodiscard]] virtual std::atomic<float>* getParameterTarget (const juce::String& parameterId) noexcept;

    //==========================================================================
    // AudioProcessor-Defaults (Subklassen überschreiben bei Bedarf)

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    // Modul-UI ist ValueTree-gebunden (CLAUDE.md 5.3 / 6) — kein eigener Editor.
    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    // Serialisierung läuft zentral über den Root-ValueTree (CLAUDE.md 5.4) —
    // bewusst leer.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

protected:
    /** Hook für Subklassen: modulspezifische Parameter-Einträge an den
        Parameters-Subtree anhängen. Default: keine Parameter. */
    virtual void appendParametersTo (juce::ValueTree& parameters);

    /** Helper: erzeugt einen Parameter-Eintrag nach Schema 6.2
        (id, value, min, max, default). */
    [[nodiscard]] static juce::ValueTree makeParameter (const juce::String& parameterId,
                                                        double value,
                                                        double minValue,
                                                        double maxValue,
                                                        double defaultValue);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConduitModule)
};

} // namespace conduit
