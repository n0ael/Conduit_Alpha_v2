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
    inline const juce::Identifier scaleRoot { "scaleRoot" };  // globale Session-Skala (0–11, C=0)
    inline const juce::Identifier scaleType { "scaleType" };  // "chromatic"/"major"/"minor"/"pentatonic"
    inline const juce::Identifier globalSwing { "globalSwing" };  // globaler Session-Swing 0..0.75 (4.5)

    // Pages (ADR 008 M1): Seiten als reine View-Schicht über EINEM Graph —
    // Message-Thread-only, der Audio-Thread kennt keine Seiten.
    // rootStateVersion gated die Root-Migration beim Laden (Bestand ohne
    // Feld gilt als Version 1; Pages-Migration hebt auf PageManager-Wert).
    inline const juce::Identifier pages            { "Pages" };
    inline const juce::Identifier page             { "Page" };
    inline const juce::Identifier pageUuid         { "pageUuid" };  // Page-Key UND Node-Property
    inline const juce::Identifier pageGridX        { "gridX" };
    inline const juce::Identifier pageGridY        { "gridY" };
    inline const juce::Identifier pageName         { "name" };
    inline const juce::Identifier viewOffsetX      { "viewOffsetX" };
    inline const juce::Identifier viewOffsetY      { "viewOffsetY" };
    inline const juce::Identifier viewZoom         { "viewZoom" };
    inline const juce::Identifier rootStateVersion { "rootStateVersion" };
    inline const juce::Identifier activePage       { "activePage" };  // View-State (kein Undo), M3b

    // Node
    inline const juce::Identifier node          { "Node" };
    inline const juce::Identifier nodeId        { "nodeId" };
    inline const juce::Identifier type          { "type" };
    inline const juce::Identifier factoryId     { "factoryId" };  // unveränderlicher Factory-Schlüssel
    inline const juce::Identifier moduleId      { "moduleId" };   // user-editierbare named_id (OSC, 7)
    inline const juce::Identifier stateVersion  { "stateVersion" };
    inline const juce::Identifier nodeState     { "nodeState" };
    inline const juce::Identifier nodeError     { "nodeError" };
    inline const juce::Identifier positionX     { "x" };
    inline const juce::Identifier positionY     { "y" };
    inline const juce::Identifier numInputChannels  { "numInputChannels" };   // für die Port-UI
    inline const juce::Identifier numOutputChannels { "numOutputChannels" };

    // Announce-Bindung (7.4) — dokumentierte AUSNAHME zur Regel „keine
    // Laufzeit-IDs serialisieren" (6): remoteId ist in BEIDEN Welten
    // persistent (das Live-Set speichert sie als Device-Parameter, der
    // Conduit-Patch als Node-Property) — nur so finden sich Device und
    // Node nach Neustarts beider Seiten wieder.
    inline const juce::Identifier remoteId      { "remoteId" };
    inline const juce::Identifier tintColour    { "tintColour" };  // Track-Farbe (0x00RRGGBB, M4L-Announce)
    inline const juce::Identifier nodeColour    { "nodeColour" };  // user-Node-Farbe (0x00RRGGBB, M-B) — Kabel-Quellfarbe

    // Inputs (Multi-Input Link Audio Send, 7.2) — pro Eingang ein Kanal-Slot
    inline const juce::Identifier inputs            { "Inputs" };
    inline const juce::Identifier input             { "Input" };
    inline const juce::Identifier inputId           { "inputId" };       // stabile UUID (serialisiert)
    inline const juce::Identifier inputMode         { "mode" };          // "mono" / "stereo"
    inline const juce::Identifier inputUserName     { "userName" };      // vom User gesetzt (überschreibt auto)
    inline const juce::Identifier inputAutoName     { "autoName" };      // Snapshot aus der Quelle
    inline const juce::Identifier inputGainParamId  { "gainParamId" };   // "in{n}_gain"

    // Parameters
    inline const juce::Identifier parameters    { "Parameters" };
    inline const juce::Identifier parameter     { "Parameter" };
    inline const juce::Identifier paramId       { "id" };
    inline const juce::Identifier paramValue    { "value" };
    inline const juce::Identifier paramMin      { "min" };
    inline const juce::Identifier paramMax      { "max" };
    inline const juce::Identifier paramDefault  { "default" };

    // FX-Chassis (4.6): Rollen-Layout + Dev-Modus-Properties pro Parameter.
    // userMin/userMax/uiHidden/curve sind PATCH-Zustand (serialisiert,
    // undo-fähig) — die UI nutzt sie, der DSP clamped IMMER auf min/max.
    inline const juce::Identifier paramRole     { "role" };      // "dsp" | "chassis" | "cvAmount"
    inline const juce::Identifier paramUserMin  { "userMin" };   // optional: Fader-Range
    inline const juce::Identifier paramUserMax  { "userMax" };
    inline const juce::Identifier paramUiHidden { "uiHidden" };  // optional: Spalte ausgeblendet
    inline const juce::Identifier paramCurve    { "curve" };     // optional: "x1 y1 x2 y2" (Bezier)

    // Fader↔Button-Modus (4.6, Dev-Modus): Spalte zeigt statt des Faders
    // benannte Wert-Buttons. uiButtons bleibt beim Zurückschalten auf Fader
    // erhalten (verlustfreies Hin- und Herschalten) — die UI baut Buttons
    // nur bei uiMode == "buttons".
    inline const juce::Identifier paramUiMode    { "uiMode" };    // optional: "buttons" (fehlend = Fader)
    inline const juce::Identifier paramUiButtons { "uiButtons" }; // optional: JSON [{"n":"Dry","v":0.25},…]

    // Control-Linking (4.6, modulintern): dieser Parameter folgt einem
    // anderen dsp-Parameter DESSELBEN Moduls als interne Modulation
    inline const juce::Identifier paramLinkSource { "linkSource" };  // optional: paramId der Quelle
    inline const juce::Identifier paramLinkAmount { "linkAmount" };  // optional: bipolar −1..+1
    inline const juce::Identifier paramLinkCurve  { "linkCurve" };   // optional: Response-Kurve (Bezier)

    // FX-Chassis: Link-Audio-Send-Tap am Modul-Ausgang (Patch-Zustand)
    inline const juce::Identifier linkSendEnabled { "linkSendEnabled" };

    // Link Audio Receive (7.2): Kanal-WUNSCH als Namen — bewusst KEINE
    // ChannelKeys (session-transient, 6); Re-Bind matcht die Namen gegen
    // availableChannels() beim ChannelsChanged-Broadcast.
    inline const juce::Identifier targetPeer    { "targetPeer" };
    inline const juce::Identifier targetChannel { "targetChannel" };

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

// Reservierte moduleIds: Tree-Nodes, die NICHT über die ModuleFactory
// materialisiert werden, sondern auf externe Graph-Nodes gemappt sind
// (die Audio-I/O-Prozessoren des EngineProcessor).
inline constexpr const char* audioInputModuleId  = "audio_input";
inline constexpr const char* audioOutputModuleId = "audio_output";

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
