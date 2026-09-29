#include "Theme.h"
#include <filesystem>

namespace Horde::ImGuiUI
{
bool LoadFonts(ImGuiIO &io, const std::string &directory, Fonts &f)
{
    auto load = [&](const char *file) -> ImFont * {
        const auto path = std::filesystem::path(directory) / file;
        return std::filesystem::is_regular_file(path) ? io.Fonts->AddFontFromFileTTF(path.string().c_str(), 18)
                                                      : nullptr;
    };
    f.body = load("Poppins-Regular.ttf");
    f.medium = load("Poppins-Medium.ttf");
    f.bold = load("Poppins-SemiBold.ttf");
    f.heading = load("Montserrat-Black.ttf");
    f.icons = load("HordeIcons.ttf");
    const bool complete = f.body && f.medium && f.bold && f.heading && f.icons;
    if (!f.body)
        f.body = io.Fonts->AddFontDefault();
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
