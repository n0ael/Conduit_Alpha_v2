#include <catch2/catch_test_macros.hpp>

#include "Core/AudioDeviceController.h"

using conduit::AudioDeviceController;

//==============================================================================
// computeWarning ist ein reiner, device-freier Helfer (CLAUDE.md 3.2/9.1):
// leer = alles im Ziel, sonst der Warntext für die Toolbar.
// KEINE statische Buffer-Untergrenze — ob ein kleiner Buffer trägt, zeigt
// der XRun-Zähler live (04.07.2026: Debug riss bei 32, Release nicht).

TEST_CASE ("AudioDeviceController: Zielwerte erzeugen keine Warnung", "[audiodevice]")
{
    CHECK (AudioDeviceController::computeWarning (48000.0, 32).isEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 64).isEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 128).isEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 256).isEmpty());
    CHECK (AudioDeviceController::computeWarning (44100.0, 128).isEmpty());  // Fallback-Rate
}

TEST_CASE ("AudioDeviceController: abweichende Rate warnt", "[audiodevice]")
{
    const auto warning = AudioDeviceController::computeWarning (96000.0, 128);
    REQUIRE (warning.isNotEmpty());
    CHECK (warning.contains ("96000"));
    CHECK (warning.contains ("Ziel: 48000 Hz / max. 256"));
}

TEST_CASE ("AudioDeviceController: Buffer über 256 warnt (spürbare Latenz)", "[audiodevice]")
{
    CHECK (AudioDeviceController::computeWarning (48000.0, 257).isNotEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 512).isNotEmpty());

    // Grenzwert inklusiv: 256 ist ok; nach unten gibt es keine Schwelle
    CHECK (AudioDeviceController::computeWarning (48000.0, 256).isEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 16).isEmpty());
}

TEST_CASE ("AudioDeviceController: Warntext enthält Rate und Buffer", "[audiodevice]")
{
    const auto warning = AudioDeviceController::computeWarning (44100.0, 512);
    REQUIRE (warning.isNotEmpty());  // Buffer > 256
    CHECK (warning.contains ("44100"));
    CHECK (warning.contains ("512"));
    CHECK (warning.contains ("Samples"));
}
