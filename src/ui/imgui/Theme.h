#pragma once
#include <imgui.h>
#include <string>

namespace Horde::ImGuiUI
{
// Scale the bundled fonts' em metrics to the screen's line height.
inline constexpr float FontMetricScale = 4.0f / 3.0f;
struct Fonts
{
    ImFont *body{};
    ImFont *medium{};
    ImFont *bold{};
    ImFont *heading{};
    ImFont *icons{};
};
bool LoadFonts(ImGuiIO &, const std::string &directory, Fonts &);
void ApplyTheme(float scale);
namespace Palette
{
inline constexpr ImU32 Background = IM_COL32(16, 11, 7, 240);
inline constexpr ImU32 Surface = IM_COL32(42, 33, 24, 255);
inline constexpr ImU32 Raised = IM_COL32(56, 44, 31, 255);
inline constexpr ImU32 Amber = IM_COL32(245, 158, 11, 255);
inline constexpr ImU32 AmberLight = IM_COL32(251, 191, 36, 255);
inline constexpr ImU32 Copper = IM_COL32(184, 115, 51, 255);
inline constexpr ImU32 CopperLight = IM_COL32(217, 149, 99, 255);
inline constexpr ImU32 Text = IM_COL32(244, 235, 216, 255);
inline constexpr ImU32 Muted = IM_COL32(184, 162, 133, 255);
inline constexpr ImU32 Dim = IM_COL32(244, 235, 216, 107);
inline constexpr ImU32 Border = IM_COL32(245, 158, 11, 36);
inline constexpr ImU32 Health = IM_COL32(244, 63, 94, 255);
inline constexpr ImU32 Magicka = IM_COL32(107, 155, 216, 255);
inline constexpr ImU32 Stamina = IM_COL32(52, 211, 153, 255);
} // namespace Palette
} // namespace Horde::ImGuiUI
