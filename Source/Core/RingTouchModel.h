#pragma once
#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h> // juce::Point lebt in juce_graphics, nicht juce_core

namespace conduit::grid
{

/** Ordnet Touch-Finger primären Fingern ("Sonne", Note) und Ring-Fingern
    ("Mond", zweite Achse) zu — der Kreis dazwischen ist der "Orbit".
    Pixel-Koordinaten. Message-Thread, kein RT. Testbar. */
class RingTouchModel
{
public:
    struct Config
    {
        float grabRadiusPx = 90.0f;   // Down näher als dies an einem primären Zentrum → Ring
        float minRadiusPx  = 40.0f;   // Slide = 0
        float maxRadiusPx  = 220.0f;  // Slide = 1
    };

    enum class TouchKind { Primary, Ring };
    /** ringOwner: die Sonne (primärer Finger), an der ein neu gegriffener
        Mond hängt — nur bei kind == Ring gültig. */
    struct DownResult { TouchKind kind; uint32_t ringOwner = 0; };
    /** owner: die Sonne, deren Mond sich gerade bewegt hat. */
    struct MoveResult { bool hasSlide = false; uint32_t owner = 0; float slide01 = 0.0f; };
    /** primaryFinger: die abgehobene Sonne (wasPrimary). ringOwner: die
        Sonne, deren Mond gerade losgelassen wurde (wasRing). */
    struct UpResult   { bool wasPrimary = false; bool wasRing = false;
                        uint32_t primaryFinger = 0; uint32_t ringOwner = 0; };
    /** hasOrbit/orbitPos: Position des Mondes (Ring-Finger) relativ zur
        Sonne (center) — live oder eingefroren, nur gültig, wenn ein Mond
        je aktiv war (Orbit bestand). */
    struct Circle     { juce::Point<float> center; float radiusPx = 0.0f;
                        bool hasOrbit = false; juce::Point<float> orbitPos; };

    // Clang lehnt eine verschachtelte Config mit In-Class-Defaults als
    // Default-Argument ab ("needed within definition of enclosing class
    // ... outside of member functions") — Default-Ctor delegiert stattdessen
    // in der .cpp, wo RingTouchModel schon vollständig ist.
    RingTouchModel() noexcept;
    explicit RingTouchModel (const Config& cfg) noexcept;

    /** Neuer Touch: nächster Greifpunkt im grabRadius, der noch KEINEN Ring
        hat → Ring dieses Fingers; sonst neuer primärer Finger. Greifpunkt ist
        die aktuelle Mond-Position, falls dieser primäre Finger schon einmal
        einen Ring hatte (Wiederaufnahme der eingefrorenen Umlaufbahn dort,
        wo sie steht — kann weit vom Zentrum entfernt sein), sonst das
        Zentrum selbst (Erstgriff). */
    DownResult onDown (uint32_t fingerId, juce::Point<float> pos) noexcept;

    /** Bewegung: Ring-Finger liefern hasSlide (Betrag des Offsets zum
        AKTUELLEN Zentrum→[0,1] zwischen min/maxRadius) und erfassen den
        Offset relativ zum Zentrum neu. Primäre Bewegung liefert kein Slide;
        das Zentrum wandert mit dem Finger, der Ring-OFFSET bleibt dabei
        UNVERÄNDERT — der Mond "klebt" relativ zur Sonne und wandert mit ihr
        mit (User-Entscheidung 06.07.2026: nur ein aktiv bewegter Ring-Finger
        darf die Umlaufbahn selbst verändern). */
    MoveResult onMove (uint32_t fingerId, juce::Point<float> pos) noexcept;

    /** Abheben: Ring → wasRing/ringOwner, Ring wird inaktiv (ringFinger = 0,
        erneut greifbar) — Mond-Orbit-Verhalten: der Ring-Offset (Radius +
        Richtung relativ zum Zentrum) bleibt eingefroren, KEIN Reset. Primär →
        wasPrimary/primaryFinger, der Finger wird entfernt (löst automatisch
        auch eine evtl. Ring-Zuordnung). */
    UpResult onUp (uint32_t fingerId) noexcept;

    std::vector<Circle> activeCircles() const; // Zeichnung; ohne Ring-Historie = minRadius
    void reset() noexcept;

    /** Ruheradius (Sonne/Mond-Größe fürs UI, CLAUDE.md-fremd — reiner
        Zeichen-Parameter, keine Logik). */
    [[nodiscard]] float restRadiusPx() const noexcept { return config.minRadiusPx; }

    /** Radius, bei dem Slide = 1 erreicht ist — der Akkord-Abruf
        (GridKeyboardComponent::latchConstellation) rechnet gespeicherte
        Mond-Offsets mit derselben Formel wie onMove in Slide um. */
    [[nodiscard]] float maxRadiusPx() const noexcept { return config.maxRadiusPx; }

private:
    struct PrimaryFinger
    {
        uint32_t id;
        juce::Point<float> center;      // Sonnen-Zentrum, live, folgt dem primären Finger
        uint32_t ringFinger = 0;        // 0 = kein Mond angedockt
        juce::Point<float> ringOffset;  // Mond-Position RELATIV zu center (friert/wandert mit)
        bool hasOrbit = false;          // true, sobald je ein Mond angedockt hat (Orbit bestand)
        float curRadiusPx = 0.0f;       // = ringOffset.getDistanceFromOrigin(), gecached
    };

    Config config;
    std::vector<PrimaryFinger> primaries;
};

} // namespace conduit::grid
