#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Horde
{
// Names, items, perks and places come from plugins saved in any code page
// (older ones in Windows-1252 or 1251). The strict serializer throws
// type_error.316 on bytes that are not UTF-8; this one writes U+FFFD instead.
inline std::string DumpGameJSON(const nlohmann::json &j)
{
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// BCP 47 tag for Skyrim's sLanguage:General value (ENGLISH, RUSSIAN, ...).
// Unknown or custom values fall back to English.
inline std::string_view LanguageTag(std::string_view skyrimLanguage)
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 9> kLanguages{{
        {"ENGLISH", "en"},
        {"FRENCH", "fr"},
        {"GERMAN", "de"},
        {"ITALIAN", "it"},
        {"SPANISH", "es"},
        {"POLISH", "pl"},
        {"RUSSIAN", "ru"},
        {"JAPANESE", "ja"},
        {"CHINESE", "zh"},
    }};
    const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; };
    for (const auto &[name, tag] : kLanguages)
        if (std::ranges::equal(skyrimLanguage, name, {}, upper))
            return tag;
    return "en";
}
} // namespace Horde
