#pragma once

#include <vector>

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
/**
    Benutzerdefinierte Namen für Hardware-Kanäle — App-Zustand, KEIN
    Patch-Zustand (gleiche Trennung wie CaptureSettings: loadPreset()
    ersetzt den Root-Tree, die Kanal-Namen bleiben unberührt, kein Undo).

    Mapping (deviceKey, direction, channelIndex) → { userLabel, imagePath,
    pairedWithNext }. imagePath ist ein leerer Platzhalter für ein späteres
    Kanal-Bild — persistiert, aber noch ohne UI.

    Stereo-Pairing (pairedWithNext): Kanal k ist mit k+1 zu einem Stereo-Paar
    gekoppelt — eine Hardware-Verkabelungs-Eigenschaft wie userLabel, deshalb
    App-Zustand am PHYSISCHEN Geräte-Kanal verankert (kein Undo, überlebt
    Preset-Load, folgt dem Device-Matching). Das Connection-Datenmodell bleibt
    unberührt: ein Stereo-Kabel sind zwei Mono-Connections, das Pairing steuert
    nur Port-UI, Kabel-Erzeugung und (später) den Link-Send. Ein Kanal gehört
    zu höchstens einem Paar — der Setter räumt Konflikte (k−1, k+1).

    Device-Matching wie CalibrationProfile (CLAUDE.md 8.1):
      1. exakter Name-Match (deviceKey == aktiver Device-Name)
      2. Prefix-Match (Suffix wie " (2)" ignoriert — beidseitig: gespeicherte
         und aktive Namen werden auf den Prefix reduziert)
      3. kein Match → nur Defaults (gemeldeter Kanalname, sonst "In N"/"Out N")

    Persistenz via juce::ApplicationProperties in einer EIGENEN Datei
    (Conduit/ChannelNames.settings) — CaptureSettings hält eine separate
    PropertiesFile-Instanz auf Conduit.settings; eine geteilte Datei würde
    sich beim Speichern gegenseitig mit veralteten Werten überschreiben.

    Threading: ALLE Methoden laufen auf dem Message Thread (Setter mit
    File-Zugriff, Getter werden nur von UI und Export-Enqueue [MT] gelesen).
    UI-Benachrichtigung über juce::ChangeBroadcaster (async).
*/
class ChannelNames : public juce::ChangeBroadcaster
{
public:
    enum class Direction { input, output };

    /** Labels werden beim Setzen hart gekürzt (Inline-Editor-Eingaben). */
    static constexpr int maxLabelLength = 48;

    /** Eigene Datei neben Conduit.settings (Klassendoku). */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit ChannelNames (const juce::PropertiesFile::Options& options = defaultOptions());
    ~ChannelNames() override;

    //==========================================================================
    /** Aktiven Geräte-Kontext setzen (initAudio bzw. Device-Wechsel).
        reportedNames = vom Device gemeldete Kanalnamen (Default-Quelle,
        indiziert über den vollen Geräte-Kanal-Index).

        activeInputChannels/activeOutputChannels = Auswahl aus dem Audio-Setup
        (welche Geräte-Kanäle aktiv sind). Der AudioProcessorPlayer komprimiert
        die aktiven Kanäle: Port/Prozessor-Kanal i trägt den i-ten *aktiven*
        Geräte-Kanal. getLabel & Co. bekommen diesen Port-Index und mappen ihn
        intern auf den echten Geräte-Kanal — so stimmen Namen auch bei
        Teil-Auswahl, und User-Labels bleiben am Geräte-Kanal verankert (stabil
        beim Ein-/Ausschalten früherer Kanäle). Leere Maske (Tests / vor
        Device-Init) → identisches Mapping. */
    void setActiveDevice (const juce::String& deviceName,
                          const juce::StringArray& reportedInputNames,
                          const juce::StringArray& reportedOutputNames,
                          const juce::BigInteger& activeInputChannels  = {},
                          const juce::BigInteger& activeOutputChannels = {});

    [[nodiscard]] juce::String getActiveDeviceName() const { return activeDeviceName; }

    //==========================================================================
    /** Effektives Label: userLabel → gemeldeter Kanalname → "In N"/"Out N". */
    [[nodiscard]] juce::String getLabel (Direction direction, int channelIndex) const;

    /** User-Override für das aktive Device. Leer (nach Trim) löscht den
        Eintrag — zurück zum Default. No-op ohne aktives Device. */
    void setUserLabel (Direction direction, int channelIndex, const juce::String& label);

    /** Leer, wenn kein Override existiert. */
    [[nodiscard]] juce::String getUserLabel (Direction direction, int channelIndex) const;

    /** Platzhalter fürs spätere Kanal-Bild — nur Persistenz, keine UI. */
    void setImagePath (Direction direction, int channelIndex, const juce::String& path);
    [[nodiscard]] juce::String getImagePath (Direction direction, int channelIndex) const;

    //==========================================================================
    // Kanal-Farbe (Quellfarbe) — App-Zustand am PHYSISCHEN Geräte-Kanal wie das
    // Pairing (kein Undo, überlebt Preset-Load, folgt Device-Matching). Format
    // 0x00RRGGBB, 0 = keine Farbe gesetzt (Sentinel wie tintColour::isVoid).
    // Speist das Kabel-Rendering (M-B); M-A schreibt und persistiert nur.

    /** 0, wenn keine Farbe gesetzt ist (→ Kabel nutzt die Default-Farbe). */
    [[nodiscard]] juce::uint32 getColour (Direction direction, int channelIndex) const;

    /** Farbe für den physischen Kanal des Ports setzen; 0 löscht sie.
        No-op ohne aktives Device. */
    void setColour (Direction direction, int channelIndex, juce::uint32 colour);

    //==========================================================================
    // Stereo-Pairing (Port-Sicht — Indizes sind komprimierte Port-Indizes,
    // das Mapping auf physische Geräte-Kanäle passiert am Rand wie getLabel)

    /** true, wenn der Port ein Stereo-Paar ANKERT: sein physischer Kanal k
        trägt pairedWithNext UND der nächste Port liegt physisch auf k+1
        (bei Teil-Auswahl können Port-Nachbarn physisch auseinanderliegen —
        dann wird das Paar nicht angezeigt, bleibt aber gespeichert).
        Ob Port portIndex+1 überhaupt existiert, prüft der Aufrufer. */
    [[nodiscard]] bool isPortPairStart (Direction direction, int portIndex) const;

    /** Koppelt den physischen Kanal des Ports mit seinem physischen Nachbarn
        (bzw. löst das Paar). Räumt Konflikte: ein Kanal gehört zu höchstens
        einem Paar (Anker auf k−1 und k+1 werden gelöst). No-op ohne aktives
        Device. */
    void setPortPairedWithNext (Direction direction, int portIndex, bool paired);

    //==========================================================================
    // Eingebetteter Link-Send (Port-Sicht, Mapping am Rand wie das Pairing)

    /** true, wenn der Kanal des Ports als Link-Audio-Kanal gesendet wird.
        Bei Stereo-Paaren zählt das Flag am ANKER-Kanal. App-Zustand wie das
        Pairing: läge der Send im Patch, würde jeder Preset-Load den
        Ableton-Stream abreißen — genau das Gegenteil der Anforderung 7.2. */
    [[nodiscard]] bool isPortLinkSendEnabled (Direction direction, int portIndex) const;

    /** Send für den physischen Kanal des Ports (de)aktivieren.
        No-op ohne aktives Device. */
    void setPortLinkSendEnabled (Direction direction, int portIndex, bool enabled);

    //==========================================================================
    // Pure Helfer — testbar ohne Datei und ohne Device

    /** "ES-3 (2)" → "ES-3"; Namen ohne " (N)"-Suffix bleiben unverändert. */
    [[nodiscard]] static juce::String stripDeviceSuffix (const juce::String& deviceName);

    /** Fallback ohne Eintrag und ohne gemeldeten Namen: "In N" / "Out N". */
    [[nodiscard]] static juce::String defaultLabel (Direction direction, int channelIndex);

    /** Dateinamen-tauglich: verbotene Zeichen (\\/:*?"<>| und Steuerzeichen)
        durch '_' ersetzen, trimmen, auf maxLabelLength kürzen; leeres
        Ergebnis → fallback. */
    [[nodiscard]] static juce::String sanitizeFileLabel (const juce::String& label,
                                                         const juce::String& fallback);

    //==========================================================================
    /** [Message Thread] Ausstehende Änderungen sofort auf Platte schreiben. */
    void flush();

private:
    struct Entry
    {
        Direction direction = Direction::input;
        int channelIndex = 0;
        juce::String userLabel;
        juce::String imagePath;
        bool pairedWithNext = false;   // Stereo-Paar (channelIndex, channelIndex+1)
        bool linkSendEnabled = false;  // Kanal wird als Link-Audio-Kanal gesendet
        juce::uint32 colour = 0;       // Quellfarbe 0x00RRGGBB, 0 = keine

        /** Trägt der Eintrag noch etwas Persistierenswertes? (Prune-Regel) */
        [[nodiscard]] bool isEmpty() const noexcept
        {
            return userLabel.isEmpty() && imagePath.isEmpty()
                && ! pairedWithNext && ! linkSendEnabled && colour == 0;
        }
    };

    struct DeviceEntry
    {
        juce::String deviceKey;  // exakter Device-Name bei Anlage (primärer Key)
        juce::String prefix;     // deviceKey ohne " (N)"-Suffix (Fallback-Key)
        std::vector<Entry> entries;
    };

    [[nodiscard]] const DeviceEntry* findMatch (const juce::String& deviceName) const;
    [[nodiscard]] DeviceEntry* findMatch (const juce::String& deviceName);
    [[nodiscard]] DeviceEntry& findOrCreateActiveDevice();
    [[nodiscard]] const Entry* findEntry (Direction direction, int channelIndex) const;
    [[nodiscard]] Entry& findOrCreateEntry (DeviceEntry& device, Direction direction,
                                            int deviceChannel);
    void pruneAndStore (DeviceEntry& device);

    /** Port/Prozessor-Kanal-Index → echter Geräte-Kanal-Index über die aktive
        Auswahl (i-ter gesetzter Bit). Ohne Auswahl-Info: identisch. */
    [[nodiscard]] int toDeviceChannel (Direction direction, int portIndex) const;

    /** Gesetzte Bits einer Maske aufsteigend als Kanal-Indizes. */
    [[nodiscard]] static std::vector<int> toChannelList (const juce::BigInteger& mask);

    void loadFromFile();
    void writeToFile();

    juce::ApplicationProperties applicationProperties;
    std::vector<DeviceEntry> devices;

    juce::String activeDeviceName;
    juce::StringArray activeInputNames;
    juce::StringArray activeOutputNames;

    // Port-Index → Geräte-Kanal-Index (i-ter aktiver Kanal); leer = identisch
    std::vector<int> activeInputChannelMap;
    std::vector<int> activeOutputChannelMap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelNames)
};

} // namespace conduit
