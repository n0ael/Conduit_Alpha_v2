/* ========================================
 *  AirwindowsRegistryTests.cpp — Katalog + Parameter-Metadaten der Ports
 * ======================================== */

#include <catch2/catch_test_macros.hpp>

#include "DSP/Airwindows/AirwindowsRegistry.h"

TEST_CASE ("AirwindowsRegistry: Katalog liefert Instanzen, unbekannte id -> nullptr", "[airwindows]")
{
    using namespace conduit::airwindows;

    const auto entries = getRegisteredPlugins();
    REQUIRE (entries.size() == 3);

    for (const auto& entry : entries)
    {
        auto plugin = createPlugin (entry.id);
        REQUIRE (plugin != nullptr);
        CHECK (std::string_view (plugin->getEffectId()) == entry.id);
        CHECK (std::string_view (plugin->getEffectName()) == entry.name);

        // Defaults liegen in 0..1 und kommen über getParameter zurück
        for (int i = 0; i < plugin->getNumParameters(); ++i)
        {
            const auto& info = plugin->getParameterInfo (i);
            CHECK (info.defaultValue >= 0.0f);
            CHECK (info.defaultValue <= 1.0f);
            CHECK (plugin->getParameter (i) == info.defaultValue);
        }
    }

    CHECK (createPlugin ("does_not_exist") == nullptr);
}
