#pragma once

#include <string_view>
#include "REL/Version.h"

namespace Plugin
{
    inline constexpr std::string_view NAME = "Horde";
    inline constexpr REL::Version VERSION{ 2, 1, 0, 0 };
}
