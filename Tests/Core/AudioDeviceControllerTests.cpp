#include <catch2/catch_test_macros.hpp>

#include "Core/AudioDeviceController.h"

using conduit::AudioDeviceController;

//==============================================================================
// computeWarning ist ein reiner, device-freier Helfer (CLAUDE.md 3.2/9.1):
// leer = alles im Ziel, sonst der Warntext für die Toolbar.

TEST_CASE ("AudioDeviceController: Zielwerte erzeugen keine Warnung", "[audiodevice]")
{
    CHECK (AudioDeviceController::computeWarning (48000.0, 32).isEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 64).isEmpty());
    CHECK (AudioDeviceController::computeWarning (44100.0, 32).isEmpty());  // Fallback-Rate
    CHECK (AudioDeviceController::computeWarning (44100.0, 64).isEmpty());
}

TEST_CASE ("AudioDeviceController: abweichende Rate warnt", "[audiodevice]")
{
    const auto warning = AudioDeviceController::computeWarning (96000.0, 32);
    REQUIRE (warning.isNotEmpty());
    CHECK (warning.contains ("96000"));
    CHECK (warning.contains ("Ziel: 48000 Hz / 32"));
}

TEST_CASE ("AudioDeviceController: zu großer Buffer warnt", "[audiodevice]")
{
    CHECK (AudioDeviceController::computeWarning (48000.0, 128).isNotEmpty());
    CHECK (AudioDeviceController::computeWarning (48000.0, 65).isNotEmpty());

    // Grenzwert: 64 ist noch ok, 65 nicht mehr
    CHECK (AudioDeviceController::computeWarning (48000.0, 64).isEmpty());
}

TEST_CASE ("AudioDeviceController: Warntext enthält Rate und Buffer", "[audiodevice]")
{
    const auto warning = AudioDeviceController::computeWarning (44100.0, 256);
    REQUIRE (warning.isNotEmpty());  // Buffer > 64
    CHECK (warning.contains ("44100"));
    CHECK (warning.contains ("256"));
    CHECK (warning.contains ("Samples"));
}
