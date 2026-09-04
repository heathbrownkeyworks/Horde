#pragma once

#include "plugin.h"

#include <SKSE/SKSE.h>

namespace Plugin::RuntimeCompatibility
{
    constexpr SKSE::PluginVersionData MakePluginVersionData()
    {
        SKSE::PluginVersionData version{};
        version.PluginVersion(Plugin::VERSION);
        version.PluginName(Plugin::NAME);
        version.AuthorName("ColdSun");
        version.UsesAddressLibrary();
        version.UsesNoStructs();
        return version;
    }
}
