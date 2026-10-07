#include "Theme.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

namespace Horde::ImGuiUI
{
namespace
{
std::filesystem::path WindowsFonts()
{
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    return std::filesystem::path(windows) / L"Fonts";
}

// Share font bytes across weights and contexts. ImGui reads them on demand for the atlas's entire lifetime.
std::vector<unsigned char> &SystemFont(const std::filesystem::path &folder, const std::vector<const wchar_t *> &files)
{
    static std::map<std::wstring, std::vector<unsigned char>> cache;
    static std::vector<unsigned char> none;
    for (const wchar_t *file : files)
    {
        const auto path = folder / file;
        if (const auto it = cache.find(path.native()); it != cache.end())
            return it->second;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            continue;
        std::vector<unsigned char> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (!data.empty())
            return cache.emplace(path.native(), std::move(data)).first->second;
    }
    return none;
}

std::vector<std::vector<const wchar_t *>> EastAsianFonts()
{
    const std::vector<const wchar_t *> chinese{L"msyh.ttc", L"simsun.ttc"};
    const std::vector<const wchar_t *> japanese{L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc"};
    const std::vector<const wchar_t *> korean{L"malgun.ttf"};
    // Prefer the display language's forms of shared CJK characters.
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_JAPANESE:
        return {japanese, chinese, korean};
    case LANG_KOREAN:
        return {korean, chinese, japanese};
    default:
        return {chinese, japanese, korean};
    }
}
} // namespace

bool LoadFonts(ImGuiIO &io, const std::string &directory, Fonts &f)
{
    const auto system = WindowsFonts();
    const auto eastAsian = EastAsianFonts();
    auto merge = [&](std::vector<unsigned char> &data) {
        if (data.empty())
            return;
        ImFontConfig config;
        config.MergeMode = true;
        config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), 18, &config);
    };
    auto fallback = [&](const wchar_t *weight) {
        if (system.empty())
            return;
        // Bundled glyphs keep priority. Only missing characters use the system fonts.
        merge(SystemFont(system, {weight, L"segoeui.ttf"}));
        for (const auto &files : eastAsian)
            merge(SystemFont(system, files));
    };
    auto load = [&](const char *file) -> ImFont * {
        const auto path = std::filesystem::path(directory) / file;
        return std::filesystem::is_regular_file(path) ? io.Fonts->AddFontFromFileTTF(path.string().c_str(), 18)
                                                      : nullptr;
    };
    auto text = [&](const char *file, const wchar_t *weight) -> ImFont * {
        ImFont *font = load(file);
        if (font)
            fallback(weight);
        return font;
    };
    f.body = text("Poppins-Regular.ttf", L"segoeui.ttf");
    f.medium = text("Poppins-Medium.ttf", L"seguisb.ttf");
    f.bold = text("Poppins-SemiBold.ttf", L"seguisb.ttf");
    f.heading = text("Montserrat-Black.ttf", L"seguibl.ttf");
    f.icons = load("HordeIcons.ttf");
    const bool complete = f.body && f.medium && f.bold && f.heading && f.icons;
    if (!f.body)
    {
        f.body = io.Fonts->AddFontDefault();
        fallback(L"segoeui.ttf");
    }
    if (!f.medium)
        f.medium = f.body;
    if (!f.bold)
        f.bold = f.body;
    if (!f.heading)
        f.heading = f.bold;
    io.FontDefault = f.body;
    return complete;
}
void ApplyTheme(float scale)
{
    ImGuiStyle style;
    style.WindowPadding = {0, 0};
    style.FramePadding = {12, 7};
    style.ItemSpacing = {8, 8};
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.FrameBorderSize = 0;
    style.WindowRounding = 0;
    style.FrameRounding = 7;
    style.PopupRounding = 14;
    style.ScrollbarSize = 7;
    auto set = [&](ImGuiCol id, ImU32 c) { style.Colors[id] = ImGui::ColorConvertU32ToFloat4(c); };
    set(ImGuiCol_Text, Palette::Text);
    set(ImGuiCol_TextDisabled, Palette::Muted);
    set(ImGuiCol_WindowBg, Palette::Background);
    set(ImGuiCol_ChildBg, 0);
    set(ImGuiCol_PopupBg, IM_COL32(16, 11, 7, 248));
    set(ImGuiCol_Border, Palette::Border);
    set(ImGuiCol_Button, Palette::Raised);
    set(ImGuiCol_ButtonHovered, IM_COL32(74, 51, 25, 255));
    set(ImGuiCol_ButtonActive, IM_COL32(90, 60, 22, 255));
    set(ImGuiCol_NavCursor, Palette::Amber);
    set(ImGuiCol_CheckMark, Palette::Amber);
    set(ImGuiCol_ScrollbarBg, 0);
    set(ImGuiCol_ScrollbarGrab, IM_COL32(184, 115, 51, 100));
    set(ImGuiCol_ScrollbarGrabHovered, Palette::Copper);
    set(ImGuiCol_ScrollbarGrabActive, Palette::Amber);
    set(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 170));
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}
} // namespace Horde::ImGuiUI
