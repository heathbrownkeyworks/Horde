// Game text reaches Horde's Magelight page as JSON. Names, items, perks and
// places come from plugins saved in any code page, so the serializer must
// never throw on bytes that are not UTF-8.
#include "GameText.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int g_failures = 0;

void Check(bool value, const std::string &message)
{
    if (!value)
    {
        ++g_failures;
        std::cerr << "FAILED: " << message << '\n';
    }
}
} // namespace

int main()
{
    nlohmann::json model;
    model["followers"] = nlohmann::json::array({{{"formID", 1001}, {"name", "\xC9owyn"}, // Windows-1252 "Éowyn"
                                                 {"race", "\xCB\xE8\xE4\xE8\xFF"},    // Windows-1251 "Лидия"
                                                 {"perks", nlohmann::json::array({"\xD0\xEE\xE4"})}}});
    model["dismissed"] = nlohmann::json::array({{{"formID", 2001},
                                                 {"name", "\xD0\x9B\xD0\xB8\xD0\xB4\xD0"}, // UTF-8 "Лид", cut mid-letter
                                                 {"homeName", "\x81\x8D"}}});

    bool strictThrows = false;
    try
    {
        (void)model.dump();
    }
    catch (const nlohmann::json::type_error &e)
    {
        strictThrows = e.id == 316;
    }
    Check(strictThrows, "the strict serializer throws on text that is not UTF-8");

    const auto text = Horde::DumpGameJSON(model);
    const auto repaired = nlohmann::json::parse(text);
    Check(repaired["followers"][0]["name"] == "\xEF\xBF\xBDowyn", "a Windows-1252 byte becomes U+FFFD");
    Check(repaired["dismissed"][0]["name"] == "\xD0\x9B\xD0\xB8\xD0\xB4\xEF\xBF\xBD",
          "valid UTF-8 survives around a truncated letter");
    Check(Horde::DumpGameJSON(repaired) == text && repaired.dump() == text, "repaired state is valid UTF-8");

    Check(Horde::LanguageTag("RUSSIAN") == "ru" && Horde::LanguageTag("CHINESE") == "zh" &&
              Horde::LanguageTag("JAPANESE") == "ja" && Horde::LanguageTag("POLISH") == "pl",
          "Skyrim languages map to BCP 47 tags");
    Check(Horde::LanguageTag("english") == "en" && Horde::LanguageTag("") == "en" &&
              Horde::LanguageTag("KLINGON") == "en",
          "unknown or missing languages fall back to English");

    if (g_failures != 0)
    {
        std::cerr << g_failures << " game text check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Horde game text: all checks passed\n";
    return EXIT_SUCCESS;
}
