/* ========================================
 *  AirwindowsRegistry.h — Katalog der portierten Airwindows-Effekte
 *  DSP-Originale: Chris Johnson / Airwindows (MIT-Lizenz)
 * ======================================== */

#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "DSP/Airwindows/AirwindowsPlugin.h"

namespace conduit::airwindows
{

/** Ein Katalog-Eintrag: stabiler Schlüssel (spätere factoryId-Basis),
    Anzeigename und Factory-Funktion. */
struct RegistryEntry
{
    const char* id;
    const char* name;
    std::unique_ptr<AirwindowsPlugin> (*create)();
};

/** Alle portierten Effekte — statische Daten, von jedem Thread lesbar. */
std::span<const RegistryEntry> getRegisteredPlugins() noexcept;

/** [Message Thread] Instanz erzeugen; nullptr bei unbekannter id. */
std::unique_ptr<AirwindowsPlugin> createPlugin (std::string_view id);

} // namespace conduit::airwindows
