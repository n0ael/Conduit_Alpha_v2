/* ========================================
 *  AirwindowsRegistry.cpp — Katalog der portierten Airwindows-Effekte
 *  DSP-Originale: Chris Johnson / Airwindows (MIT-Lizenz)
 * ======================================== */

#include "DSP/Airwindows/AirwindowsRegistry.h"

#include "DSP/Airwindows/Plugins/Density.h"
#include "DSP/Airwindows/Plugins/Slew.h"
#include "DSP/Airwindows/Plugins/Spiral.h"

namespace conduit::airwindows
{

namespace
{
    template <typename PluginType>
    std::unique_ptr<AirwindowsPlugin> make()
    {
        return std::make_unique<PluginType>();
    }

    constexpr RegistryEntry kRegistry[] = {
        { "density", "Density", &make<Density> },
        { "slew",    "Slew",    &make<Slew> },
        { "spiral",  "Spiral",  &make<Spiral> },
    };
}

std::span<const RegistryEntry> getRegisteredPlugins() noexcept
{
    return kRegistry;
}

std::unique_ptr<AirwindowsPlugin> createPlugin (std::string_view id)
{
    for (const auto& entry : kRegistry)
        if (id == entry.id)
            return entry.create();

    return nullptr;
}

} // namespace conduit::airwindows
