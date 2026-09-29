#include "HordeScreen.h"
#include "ControllerLegend.h"
#include "Icons.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>

namespace Horde::ImGuiUI
{
namespace
{
const Model emptyObject = Model::object(), emptyArray = Model::array();
const Model &Array(const Model &j, const char *key)
{
    auto it = j.find(key);
    return it != j.end() && it->is_array() ? *it : emptyArray;
}
const Model &Object(const Model &j, const char *key)
{
    auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : emptyObject;
}
std::string Str(const Model &j, const char *key, const char *fallback = "")
{
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}
float Num(const Model &j, const char *key, float fallback = 0)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_number())
        return fallback;
    float v = it->get<float>();
    return std::isfinite(v) ? v : fallback;
}
bool Bool(const Model &j, const char *key, bool fallback = false)
{
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}
std::uint32_t ID(const Model &j)
{
    auto it = j.find("formID");
    return it != j.end() && it->is_number_integer() ? it->get<std::uint32_t>() : 0;
}
std::string Number(float n)
{
    return std::to_string(static_cast<int>(std::round(n)));
}
std::string Upper(std::string value)
{
    for (auto &c : value)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
float Ratio(const Model &j, const char *value, const char *maximum)
{
    float m = Num(j, maximum);
    return m > 0 ? std::clamp(Num(j, value) / m, 0.0f, 1.0f) : 0;
}
ImU32 Alpha(ImU32 color, float alpha)
{
    return (color & 0x00ffffffu) |
           (static_cast<ImU32>(static_cast<float>(color >> 24) * std::clamp(alpha, 0.0f, 1.0f)) << 24);
}

struct Canvas
{
    const Fonts &f;
    ScreenState &state;
    FrameResult &result;
    float s;
    bool interactive;
    float offsetX = 0, offsetY = 0;
    ImDrawList *draw() const
    {
        return ImGui::GetWindowDrawList();
    }
    ImVec2 P(float x, float y) const
    {
        return {(x + offsetX) * s, (y + offsetY) * s};
    }
    void Fill(float x, float y, float w, float h, ImU32 c, float r = 0) const
    {
        draw()->AddRectFilled(P(x, y), P(x + w, y + h), c, r * s);
    }
    void Border(float x, float y, float w, float h, ImU32 c, float r = 0) const
    {
        draw()->AddRect(P(x, y), P(x + w, y + h), c, r * s, 0, 1);
    }
    void Line(float x, float y, float xx, float yy, ImU32 c, float width = 1) const
    {
        draw()->AddLine(P(x, y), P(xx, yy), c, width * s);
    }
    void Icon(float x, float y, const char *glyph, float size = 15, ImU32 color = Palette::Muted) const
    {
        if (f.icons)
            draw()->AddText(f.icons, size * s, P(x, y), color, glyph);
    }
    void Text(float x, float y, const std::string &text, float size = 12.5f, ImU32 color = Palette::Text,
              ImFont *font = nullptr, float width = 10000, bool right = false) const
    {
        if (width <= 0)
            return;
        font = font ? font : f.body;
        const float pixels = size * s * FontMetricScale;
        std::string shown = text;
        auto measure = [&] { return font->CalcTextSizeA(pixels, FLT_MAX, 0, shown.c_str()).x / s; };
        if (measure() > width)
        {
            while (!shown.empty() && measure() + size * 1.2f > width)
            {
                auto n = shown.size() - 1;
                while (n > 0 && (static_cast<unsigned char>(shown[n]) & 0xc0) == 0x80)
                    --n;
                shown.resize(n);
            }
            shown += "...";
        }
        if (right)
            x += width - measure();
        ImVec4 clip{(x + offsetX) * s, (y + offsetY - 10) * s, (x + offsetX + width) * s, (y + offsetY + size * 3) * s};
        draw()->AddText(font, pixels, P(x, y), color, shown.c_str(), nullptr, 0, &clip);
    }
    void Title(float x, float y, const std::string &text, float size, float width) const
    {
        const auto begin = draw()->VtxBuffer.Size;
        Text(x, y, Upper(text), size, Palette::Text, f.heading, width);
        const auto end = draw()->VtxBuffer.Size;
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(
            draw(), begin, end, P(x, y), P(x + std::min(width, size * static_cast<float>(text.size()) * 0.85f), y),
            Palette::Text, Palette::Copper);
    }
    void Panel(float x, float y, float w, float h, const char *title) const
    {
        Fill(x, y + 5, w, h, IM_COL32(0, 0, 0, 75), 14);
        const auto begin = draw()->VtxBuffer.Size;
        Fill(x, y, w, h, IM_COL32(42, 33, 24, 235), 14);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw(), begin, draw()->VtxBuffer.Size, P(x, y), P(x, y + h),
                                                      IM_COL32(42, 33, 24, 255), IM_COL32(20, 16, 11, 255));
        Border(x, y, w, h, IM_COL32(244, 235, 216, 20), 14);
        Text(x + 20, y + 14, title, 11, Palette::CopperLight, f.bold, w - 40);
    }
    bool Item(const std::string &id, float x, float y, float w, float h, Section section, bool enabled = true,
              bool focusTarget = true)
    {
        ImGui::SetCursorScreenPos(P(x, y));
        if (interactive && enabled && focusTarget && state.focusRequested && state.section == section)
        {
            ImGui::SetKeyboardFocusHere();
            state.focusRequested = false;
        }
        ImGui::BeginDisabled(!interactive || !enabled);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, state.scopedNavigation && section != state.section);
        const bool pressed = ImGui::InvisibleButton(id.c_str(), {std::max(1.0f, w * s), std::max(1.0f, h * s)},
                                                    ImGuiButtonFlags_EnableNav);
        ImGui::PopItemFlag();
        if (pressed || (ImGui::IsItemFocused() && !state.focusRequested &&
                        ImGui::GetCurrentContext()->NavJustMovedToId == ImGui::GetItemID()))
            state.section = section;
        const ImVec2 p = P(x, y);
        result.targets.push_back({id, {p.x, p.y, w * s, h * s}, interactive && enabled});
        ImGui::EndDisabled();
        return pressed;
    }
    bool Button(const std::string &id, const std::string &label, float x, float y, float w, float h, Section section,
                bool active = false, bool danger = false, bool enabled = true, float size = 12.5f, ImU32 tint = 0)
    {
        const bool pressed = Item(id, x, y, w, h, section, enabled);
        const bool hovered = ImGui::IsItemHovered(), focused = ImGui::IsItemFocused();
        const auto accent = tint ? tint : danger ? Palette::Health : Palette::Amber;
        Fill(x, y, w, h, Alpha(accent, active ? 0.20f : hovered ? 0.13f : danger ? 0.07f : 0.035f), 7);
        Border(x, y, w, h, Alpha(accent, focused ? 1.0f : active ? 0.45f : 0.20f), 7);
        const auto tw = f.medium->CalcTextSizeA(size * s * FontMetricScale, FLT_MAX, 0, label.c_str()).x / s;
        Text(x + std::max(4.0f, (w - tw) / 2), y + (h - size * FontMetricScale) / 2, label, size,
             Alpha(tint     ? tint
                   : danger ? IM_COL32(253, 164, 175, 255)
                   : active ? Palette::Amber
                            : Palette::Text,
                   enabled ? 1.0f : 0.4f),
             f.medium, w - 8);
        return pressed;
    }
    bool Check(const std::string &id, const std::string &label, bool checked, float x, float y, float width,
               Section section)
    {
        const bool pressed = Item(id, x, y, width, 24, section);
        Fill(x, y + 4, 16, 16, checked ? IM_COL32(217, 119, 6, 255) : IM_COL32(244, 235, 216, 10), 3);
        Border(x, y + 4, 16, 16, checked || ImGui::IsItemFocused() ? Palette::Amber : Palette::Dim, 3);
        if (checked)
        {
            Line(x + 4, y + 12, x + 7, y + 15, IM_COL32(10, 8, 7, 255), 2);
            Line(x + 7, y + 15, x + 12, y + 8, IM_COL32(10, 8, 7, 255), 2);
        }
        Text(x + 24, y + 3, label, 12.5f, Palette::Muted, f.body, width - 24);
        if (ImGui::IsItemFocused())
            Border(x - 3, y, width + 3, 24, Palette::Amber, 4);
        if (ImGui::IsItemHovered() && !label.empty())
            ImGui::SetTooltip("%s", label.c_str());
        return pressed;
    }
    void Bar(float x, float y, float w, float h, float pct, ImU32 c) const
    {
        Fill(x, y, w, h, IM_COL32(10, 7, 4, 170), h / 2);
        if (pct > 0)
            Fill(x, y, std::max(h, w * pct), h, c, h / 2);
    }
    void Send(const char *name, Model data = Model::object())
    {
        result.actions.push_back({name, std::move(data)});
    }
};

void DrawRoster(Canvas &c, const Model &model, float half, float height)
{
    const auto &followers = Array(model, "followers");
    c.Title(36, 23, "HORDE", 26, 210);
    c.Text(166, 39, "v3.0", 11, Alpha(Palette::CopperLight, 0.6f), c.f.medium);
    c.Text(half - 250, 37,
           std::to_string(followers.size()) + " / " + Number(Num(model, "maxFollowers", 20)) + " followers", 13,
           Palette::Muted, c.f.body, 222, true);
    if (c.Button("summon-all", "Summon All", 36, 72, 108, 33, Section::Group))
        c.Send("hordeSummonAll");
    if (c.Button("follow-all", "Follow All", 152, 72, 102, 33, Section::Group))
        c.Send("hordeFollowAll");
    if (c.Button("wait-all", "Wait All", 262, 72, 89, 33, Section::Group))
        c.Send("hordeWaitAll");
    const bool passive = Bool(model, "allPassive");
    if (c.Button("passive-all", passive ? "Aggressive" : "Passive", 359, 72, 101, 33, Section::Group, passive))
        c.Send("hordePassiveAll", {{"passive", !passive}});
    const auto dc = Array(model, "dismissed").size();
    if (dc && c.Button("dismissed", "Dismissed (" + std::to_string(dc) + ")", half - 174, 72, 146, 33, Section::Group,
                       c.state.dismissed))
    {
        c.state.dismissed = true;
        c.state.section = Section::Commands;
        c.state.focusRequested = true;
        c.Send("hordeGetDismissed");
    }
    ImGui::SetCursorScreenPos(c.P(28, 119));
    ImGui::BeginChild("RosterScroll", {(half - 48) * c.s, (height - 185) * c.s}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (c.state.section == Section::Followers && c.state.rightScroll != 0)
    {
        ImGui::SetScrollY(ImGui::GetScrollY() + c.state.rightScroll);
        c.state.rightScroll = 0;
    }
    const float base = 119 - ImGui::GetScrollY() / c.s;
    const int slots = std::max(static_cast<int>(followers.size()),
                               std::clamp(static_cast<int>(Num(model, "maxFollowers", 20)), 1, 20));
    const float rowHeight = std::clamp((height - 185) / static_cast<float>(slots), 30.0f, 42.0f);
    const float w = half - 54;
    for (int i = 0; i < slots; ++i)
    {
        const float y = base + rowHeight * static_cast<float>(i);
        const bool occupied = i < static_cast<int>(followers.size());
        if (i % 2)
            c.Fill(28, y, w, rowHeight, IM_COL32(244, 235, 216, occupied ? 5 : 1));
        c.Line(28, y + rowHeight, w + 28, y + rowHeight, IM_COL32(244, 235, 216, occupied ? 13 : 4));
        if (!occupied)
            continue;
        const auto &f = followers[i];
        const auto id = ID(f);
        const bool selected = id == c.state.selected, waiting = Bool(f, "isWaiting");
        if (c.Item("follower-" + std::to_string(id), 28, y, w, rowHeight, Section::Followers, true, selected))
        {
            c.state.selected = id;
            c.state.dismissed = false;
            c.state.section = Section::Commands;
            c.state.focusRequested = true;
        }
        if (selected || ImGui::IsItemHovered())
            c.Fill(28, y, w, rowHeight, IM_COL32(245, 158, 11, selected ? 26 : 15));
        if (selected || ImGui::IsItemFocused())
            c.Border(28, y, w, rowHeight, Alpha(Palette::Amber, ImGui::IsItemFocused() ? 1.0f : 0.32f));
        c.Fill(28, y, 3, rowHeight, waiting ? Palette::Amber : Palette::Stamina);
        const float baseline = y + (rowHeight - 18) / 2;
        const float flexible = w - 230, nameWidth = flexible * 0.53f, classWidth = flexible - nameWidth;
        c.Text(43, baseline - 1, Str(f, "name", "Unknown"), 14, selected ? Palette::AmberLight : Palette::Text,
               c.f.medium, nameWidth - 18);
        const auto &skills = Object(f, "skills");
        const auto melee = Num(skills, "oneHanded") + Num(skills, "twoHanded") + Num(skills, "block");
        const auto magic = Num(skills, "destruction") + Num(skills, "restoration") + Num(skills, "conjuration");
        const auto ranged = Num(skills, "archery") * 2.5f;
        const bool mage = magic >= melee && magic >= ranged, archer = !mage && ranged >= melee;
        const bool spellsword = mage && melee >= magic * 0.6f;
        c.Icon(43 + nameWidth, baseline + 2,
               mage     ? Icons::destruction
               : archer ? Icons::archery
                        : Icons::sword,
               13,
               spellsword ? Palette::Amber
               : mage     ? Palette::Magicka
               : archer   ? Palette::Stamina
                          : Palette::Health);
        c.Text(62 + nameWidth, baseline + 2,
               "Lv " + Number(Num(f, "level", 1)) + "  ·  " + Str(f, "className", "Adventurer"), 11, Palette::Muted,
               c.f.body, classWidth - 31);
        const float bx = 28 + w - 205;
        c.Bar(bx, y + (rowHeight - 18) / 2, 56, 4, Ratio(f, "health", "healthMax"), Palette::Health);
        c.Bar(bx, y + (rowHeight - 18) / 2 + 7, 56, 4, Ratio(f, "magicka", "magickaMax"), Palette::Magicka);
        c.Bar(bx, y + (rowHeight - 18) / 2 + 14, 56, 4, Ratio(f, "stamina", "staminaMax"), Palette::Stamina);
        c.Text(bx + 65, baseline + 2, Number(Num(f, "distance")) + "m", 11, Palette::Muted, c.f.body, 41, true);
        c.Text(bx + 117, baseline + 3, waiting ? "WAITING" : "FOLLOWING", 10,
               waiting ? Palette::Amber : Palette::Stamina, c.f.bold, 80, true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s | %s | %s", Str(f, "name").c_str(), Str(f, "className").c_str(),
                              waiting ? "Waiting" : "Following");
    }
    ImGui::SetCursorScreenPos(c.P(28, base + rowHeight * static_cast<float>(slots) - 12));
    ImGui::Dummy({1, 1});
    ImGui::EndChild();

    const float fy = height - 52;
    c.Line(36, fy - 10, half - 28, fy - 10, Palette::Border);
    c.Text(36, fy + 7, "Follow:", 11, Palette::Muted);
    const auto distance = Str(model, "followDistance", "normal");
    const char *values[] = {"close", "normal", "far"};
    const char *labels[] = {"Close", "Normal", "Far"};
    constexpr float distanceButtonWidth = 58, distanceButtonGap = 8;
    for (int i = 0; i < 3; ++i)
        if (c.Button(std::string("distance-") + values[i], labels[i],
                     87 + (distanceButtonWidth + distanceButtonGap) * i, fy, distanceButtonWidth, 30, Section::Settings,
                     distance == values[i]))
            c.Send("hordeUpdateSettings", {{"followDistance", values[i]}});
    const bool enabled = Bool(model, "notificationsEnabled", true);
    if (c.Check("notifications", "Mute Notifications", !enabled, 301, fy + 3, 194, Section::Settings))
        c.Send("hordeUpdateSettings", {{"notificationsEnabled", !enabled}});
    if (c.Button("input-mode", Bool(model, "useKeybinds") ? "Enable Favorites" : "Use Keybinds", half - 166, fy, 138,
                 30, Section::Settings))
        c.Send("hordeToggleInputMode");
}

void DrawDossier(Canvas &c, const Model &model, float half, float width, float height)
{
    const auto *follower = FindFollower(model, c.state.selected);
    if (!follower)
    {
        c.Icon(half + (width - half) / 2 - 24, height / 2 - 110, Icons::sword, 48, Alpha(Palette::Copper, 0.55f));
        const auto tx = half + std::max(50.0f, (width - half - 405) / 2);
        c.Title(tx + 54, height / 2 - 45, "No followers yet", 18, width - half - 140);
        c.Text(tx + 24, height / 2 + 2, "Recruit a follower and their dossier opens here.", 13, Palette::Muted);
        c.Text(tx, height / 2 + 28, "The commands on the left act on your whole horde at once.", 13, Palette::Muted,
               c.f.body, width - half - 140);
        return;
    }
    const auto &f = *follower;
    const auto id = ID(f);
    const Model target = {{"formID", id}};
    const float x = half + 33, w = width - x - 30, gap = 16, card = (w - gap) / 2;
    const bool waiting = Bool(f, "isWaiting");
    ImGui::SetCursorScreenPos(c.P(x, 24));
    ImGui::BeginChild("DossierScroll", {w * c.s, (height - 48) * c.s}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (c.state.section == Section::Commands && c.state.rightScroll != 0)
    {
        ImGui::SetScrollY(ImGui::GetScrollY() + c.state.rightScroll);
        c.state.rightScroll = 0;
    }
    c.offsetY = -ImGui::GetScrollY() / c.s;
    c.Fill(x, 26, 4, 52, waiting ? Palette::Amber : Palette::Stamina);
    c.Title(x + 18, 22, Str(f, "name", "Unknown"), 30, w - 180);
    c.Text(x + 18, 61,
           Str(f, "race", "Unknown") + "  ·  " + Str(f, "className", "Adventurer") + "  ·  Level " +
               Number(Num(f, "level", 1)) + "  ·  " + Number(Num(f, "distance")) + "m away",
           12.5f, Palette::Muted, c.f.body, w - 178);
    const char *status = waiting ? "WAITING" : "FOLLOWING";
    const auto statusColor = waiting ? Palette::Amber : Palette::Stamina;
    const float statusWidth = c.f.bold->CalcTextSizeA(11 * c.s * FontMetricScale, FLT_MAX, 0, status).x / c.s;
    const float statusLeft = x + w - 43 - statusWidth;
    c.draw()->AddCircleFilled(c.P(statusLeft - 12, 55), 4 * c.s, statusColor);
    c.Text(statusLeft, 46, status, 11, statusColor, c.f.bold);
    const float top = 100, firstH = 228, second = top + firstH + 16, secondH = 235;
    c.Panel(x, top, card, firstH, "VITALS");
    c.Panel(x + card + gap, top, card, firstH, "BEHAVIOR");
    c.Panel(x, second, card, secondH, "COMBAT SKILLS");
    c.Panel(x + card + gap, second, card, secondH, "EQUIPMENT");
    const char *keys[] = {"health", "magicka", "stamina"};
    const char *maxKeys[] = {"healthMax", "magickaMax", "staminaMax"};
    const char *names[] = {"Health", "Magicka", "Stamina"};
    const ImU32 colors[] = {Palette::Health, Palette::Magicka, Palette::Stamina};
    const char *icons[] = {Icons::health, Icons::magicka, Icons::stamina};
    for (int i = 0; i < 3; ++i)
    {
        const float y = top + 47 + i * 35.0f;
        c.Icon(x + 20, y + 2, icons[i], 14, colors[i]);
        c.Text(x + 40, y, names[i], 12.5f, colors[i], c.f.medium);
        c.Bar(x + 108, y + 7, std::max(10.0f, card - 215), 9, Ratio(f, keys[i], maxKeys[i]), colors[i]);
        c.Text(x + card - 94, y, Number(Num(f, keys[i])) + " / " + Number(Num(f, maxKeys[i])), 12, Palette::Muted,
               c.f.body, 74, true);
    }
    c.Icon(x + 20, top + 165, Icons::armor, 14, Palette::CopperLight);
    c.Text(x + 40, top + 163, "Armor Rating", 12.5f, Palette::Muted);
    c.Text(x + 155, top + 163, Number(Num(f, "armorRating")), 12.5f, Palette::Text, c.f.medium);
    const float bx = x + card + gap + 20, controlX = bx + 98, controlW = card - 138;
    const float stateButtonGap = 8, stateButtonWidth = (controlW - stateButtonGap) / 2;
    c.Text(bx, top + 49, "State", 12.5f, Palette::Muted);
    if (c.Button("follow", "Follow", controlX, top + 43, stateButtonWidth, 29, Section::Commands, !waiting))
        c.Send("hordeSetFollow", target);
    if (c.Button("wait", "Wait", controlX + stateButtonWidth + stateButtonGap, top + 43, stateButtonWidth, 29,
                 Section::Commands, waiting))
        c.Send("hordeSetWait", target);
    struct Toggle
    {
        const char *id;
        const char *label;
        const char *key;
        const char *action;
        const char *payload;
        const char *on;
        const char *off;
        bool fallback;
    };
    const Toggle toggles[] = {{"passive", "Passive", "isPassive", "hordeSetPassive", "passive",
                               "Will not engage in combat", "Will fight enemies", false},
                              {"follow-close", "Follow Close", "isFollowClose", "hordeSetFollowClose", "enabled",
                               "Teleports when too far", "No auto-teleport", false},
                              {"sandbox", "Idle Sandbox", "isSandboxEnabled", "hordeSetSandbox", "enabled",
                               "Sandboxes in safe locations", "Sandbox disabled", false},
                              {"essential", "Essential", "isEssential", "hordeSetEssential", "essential",
                               "Cannot be killed", "Can be killed", true}};
    for (int i = 0; i < 4; ++i)
    {
        const auto &t = toggles[i];
        const bool value = Bool(f, t.key, t.fallback);
        float y = top + 82 + i * 32.0f;
        c.Text(bx, y + 4, t.label, 12.5f, Palette::Muted);
        if (c.Check(t.id, value ? t.on : t.off, value, controlX, y, controlW, Section::Commands))
        {
            auto data = target;
            data[t.payload] = !value;
            c.Send(t.action, std::move(data));
        }
    }
    const char *skillNames[] = {"One-Handed",  "Two-Handed",  "Archery",     "Block",      "Heavy Armor",
                                "Light Armor", "Destruction", "Restoration", "Conjuration"};
    const char *skillKeys[] = {"oneHanded",  "twoHanded",   "archery",     "block",      "heavyArmor",
                               "lightArmor", "destruction", "restoration", "conjuration"};
    const char *skillIcons[] = {Icons::oneHanded,   Icons::twoHanded,   Icons::archery,
                                Icons::block,       Icons::heavyArmor,  Icons::lightArmor,
                                Icons::destruction, Icons::restoration, Icons::conjuration};
    const auto &skills = Object(f, "skills");
    const float sw = (card - 56) / 2;
    for (int i = 0; i < 9; ++i)
    {
        const float sx = x + 20 + (i % 2) * (sw + 16), sy = second + 49 + (i / 2) * 30.0f;
        c.Icon(sx, sy + 1, skillIcons[i]);
        c.Text(sx + 20, sy, skillNames[i], 11.5f, Palette::Muted, c.f.body, sw - 49);
        c.Text(sx + sw - 27, sy, Number(Num(skills, skillKeys[i])), 11.5f, Palette::Text, c.f.bold, 27, true);
    }
    const char *slots[] = {"Right Hand", "Left Hand", "Head", "Body", "Hands", "Feet"};
    const char *equipKeys[] = {"rightHand", "leftHand", "head", "body", "hands", "feet"};
    const char *equipIcons[] = {Icons::sword,      Icons::shield,   Icons::helmet,
                                Icons::chestplate, Icons::gauntlet, Icons::boot};
    const auto &equipment = Object(f, "equipment");
    for (int i = 0; i < 6; ++i)
    {
        const auto &item = Object(equipment, equipKeys[i]);
        const auto stat = Num(item, "stat");
        std::string label = Str(item, "name", "None");
        if (stat > 0)
            label += " (" + Number(stat) + ")";
        const float y = second + 46 + i * 29.0f;
        c.Icon(bx, y + 1, equipIcons[i]);
        c.Text(bx + 21, y, slots[i], 11.5f, Palette::Muted);
        c.Text(bx + 105, y, label, 11.5f, Palette::Text, c.f.body, card - 145, true);
    }
    const bool home = Num(f, "homeWorldspace") != 0 || Bool(f, "homeWorldspace");
    const float homeY = std::max(second + secondH + 22, height - 120);
    c.Fill(x, homeY, w, 43, IM_COL32(42, 33, 24, 145), 14);
    c.Border(x, homeY, w, 43, IM_COL32(244, 235, 216, 20), 14);
    c.Text(x + 20, homeY + 13, "HOME", 11, Palette::CopperLight, c.f.bold);
    c.Text(x + 77, homeY + 13, home ? Str(f, "homeName", "Unknown") : "No home set at this time", 12.5f,
           home ? Palette::Text : Palette::Dim, c.f.body, w - 100);
    const float actionY = homeY + 55;
    const int buttons = home ? 4 : 3;
    const float buttonW = (w - 10 * (buttons - 1)) / buttons;
    if (c.Button("summon", "Summon", x, actionY, buttonW, 38, Section::Commands, true))
        c.Send("hordeSummon", target);
    if (c.Button("set-home", home ? "Change Home" : "Set Home", x + buttonW + 10, actionY, buttonW, 38,
                 Section::Commands))
        c.Send("hordeSetHome", target);
    if (home && c.Button("clear-home", "Clear Home", x + 2 * (buttonW + 10), actionY, buttonW, 38, Section::Commands))
        Confirm(c.state, "Clear Home", "Clear " + Str(f, "name") + "'s home assignment?", "Clear Home",
                {"hordeClearHome", target});
    if (c.Button("dismiss", "Dismiss", x + w - buttonW, actionY, buttonW, 38, Section::Commands, false, true))
        Confirm(c.state, "Confirm Dismiss",
                "Dismiss " + Str(f, "name") + " from your horde? They will return to their home.", "Dismiss",
                {"hordeDismiss", target});
    ImGui::SetCursorScreenPos(c.P(x, actionY + 35));
    ImGui::Dummy({1, 1});
    c.offsetY = 0;
    ImGui::EndChild();
}

void DrawRegistry(Canvas &c, const Model &model, float half, float width, float height)
{
    const float x = half + 32, w = width - x - 30;
    if (c.Button("registry-back", "< Back", x, 29, 72, 32, Section::Commands))
    {
        c.state.dismissed = false;
        c.state.section = Section::Followers;
        c.state.focusRequested = true;
    }
    c.Title(x + 87, 29, "Dismissed", 22, w - 170);
    ImGui::SetCursorScreenPos(c.P(x, 86));
    ImGui::BeginChild("RegistryScroll", {w * c.s, (height - 114) * c.s}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (c.state.section == Section::Commands && c.state.rightScroll != 0)
    {
        ImGui::SetScrollY(ImGui::GetScrollY() + c.state.rightScroll);
        c.state.rightScroll = 0;
    }
    const float base = 86 - ImGui::GetScrollY() / c.s;
    const auto &list = Array(model, "dismissed");
    if (list.empty())
    {
        c.Text(x + 20, base + 30, "No dismissed followers", 18, Palette::Text, c.f.bold);
        c.Text(x + 20, base + 67, "Followers you dismiss will appear here.", 13, Palette::Muted);
        ImGui::EndChild();
        return;
    }
    const float actionWidth = 324, nameWidth = (w - actionWidth) * 0.45f, homeWidth = w - actionWidth - nameWidth;
    c.Text(x + 10, base + 8, "NAME", 11, Palette::CopperLight, c.f.bold);
    c.Text(x + nameWidth + 10, base + 8, "HOME", 11, Palette::CopperLight, c.f.bold);
    c.Text(x + w - 100, base + 8, "ACTIONS", 11, Palette::CopperLight, c.f.bold, 80, true);
    c.Line(x, base + 35, x + w, base + 35, Palette::Border);
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto &d = list[i];
        const auto id = ID(d);
        const Model target = {{"formID", id}};
        const float y = base + 37 + static_cast<float>(i) * 49;
        c.Line(x, y + 48, x + w, y + 48, IM_COL32(244, 235, 216, 15));
        c.Text(x + 10, y + 15, Str(d, "name", "Unknown"), 13, Palette::Text, c.f.medium, nameWidth - 20);
        c.Text(x + nameWidth + 10, y + 15, Bool(d, "hasHome") ? Str(d, "homeName") : "No home", 13, Palette::Muted,
               c.f.body, homeWidth - 20);
        const float ax = x + w - actionWidth;
        const auto suffix = "-" + std::to_string(id);
        if (c.Button("registry-summon" + suffix, "Summon", ax, y + 10, 72, 29, Section::Commands, false, false, true,
                     11.5f, Palette::Magicka))
            c.Send("hordeSummonDismissed", target);
        if (c.Button("registry-home" + suffix, "Set Home", ax + 78, y + 10, 79, 29, Section::Commands, false, false,
                     true, 11.5f, Palette::CopperLight))
            c.Send("hordeSetDismissedHome", target);
        if (c.Button("registry-clear" + suffix, "Clear Home", ax + 163, y + 10, 89, 29, Section::Commands, false, false,
                     Bool(d, "hasHome"), 11.5f, Palette::Muted))
            c.Send("hordeClearDismissedHome", target);
        if (c.Button("registry-forget" + suffix, "Forget", ax + 258, y + 10, 64, 29, Section::Commands, false, true,
                     true, 11.5f))
            Confirm(c.state, "Forget Follower",
                    "Remove " + Str(d, "name") + " from the registry? Their home assignment will also be removed.",
                    "Forget", {"hordeForgetFollower", target});
    }
    c.Text(x + 10, base + static_cast<float>(list.size()) * 49 + 59, "Set Home assigns the player's current location.",
           12, Palette::Dim);
    ImGui::SetCursorScreenPos(c.P(x, base + static_cast<float>(list.size()) * 49 + 90));
    ImGui::Dummy({1, 1});
    ImGui::EndChild();
}
} // namespace

const Model *FindFollower(const Model &model, std::uint32_t id)
{
    if (!id)
        return nullptr;
    for (const auto &f : Array(model, "followers"))
        if (ID(f) == id)
            return &f;
    return nullptr;
}
void ReconcileSelection(const Model &model, ScreenState &state)
{
    if (!FindFollower(model, state.selected))
    {
        const auto &list = Array(model, "followers");
        state.selected = list.empty() ? 0 : ID(list[0]);
        state.focusRequested = true;
    }
    if (!state.pending.name.empty())
    {
        const auto id = ID(state.pending.data);
        bool valid = FindFollower(model, id) != nullptr;
        if (state.pending.name == "hordeForgetFollower")
        {
            valid = false;
            for (const auto &d : Array(model, "dismissed"))
                if (ID(d) == id)
                    valid = true;
        }
        if (!valid)
        {
            state.pending = {};
            state.confirmRequested = false;
        }
    }
}
void Confirm(ScreenState &state, std::string title, std::string message, std::string label, Action action)
{
    state.confirmTitle = std::move(title);
    state.confirmText = std::move(message);
    state.confirmLabel = std::move(label);
    state.pending = std::move(action);
    state.confirmRequested = true;
}

FrameResult DrawHorde(const Model &model, ScreenState &state, const Fonts &fonts, bool interactive)
{
    FrameResult result;
    ReconcileSelection(model, state);
    state.scopedNavigation = Bool(model, "_usingGamepad") && !Bool(model, "_controllerCursor");
    auto &io = ImGui::GetIO();
    const float scale = std::clamp(std::min(io.DisplaySize.x / 1600.0f, io.DisplaySize.y / 900.0f), 0.5f, 3.0f);
    const float width = io.DisplaySize.x / scale, height = io.DisplaySize.y / scale, half = width / 2;
    ApplyTheme(scale);
    ImGui::PushFont(fonts.body, 12.5f * scale * FontMetricScale);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("HordeWarTable", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoScrollbar);
    Canvas c{fonts, state, result, scale, interactive};
    // Background tint, frame, and column divider.
    c.draw()->AddRectFilledMultiColor({0, 0}, io.DisplaySize, IM_COL32(245, 158, 11, 17), 0, IM_COL32(184, 115, 51, 22),
                                      0);
    c.Border(10, 10, width - 20, height - 20, IM_COL32(245, 158, 11, 33), 6);
    const bool modal = ImGui::IsPopupOpen("HordeConfirm") || state.confirmRequested;
    if (interactive && !modal)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1))
        {
            const int delta = ImGui::IsKeyPressed(ImGuiKey_GamepadR1) ? 1 : 3;
            state.section = static_cast<Section>((static_cast<int>(state.section) + delta) % 4);
            state.focusRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft))
        {
            state.section = state.section == Section::Followers ? Section::Group : Section::Followers;
            state.focusRequested = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp) && !state.dismissed && state.section == Section::Followers &&
            !Array(model, "dismissed").empty())
        {
            state.dismissed = true;
            state.section = Section::Commands;
            state.focusRequested = true;
            c.Send("hordeGetDismissed");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
        {
            if (state.dismissed)
            {
                state.dismissed = false;
                state.section = Section::Followers;
                state.focusRequested = true;
            }
            else if (Bool(model, "_usingGamepad") && state.section != Section::Followers)
            {
                state.section = Section::Followers;
                state.focusRequested = true;
            }
            else
                c.Send("hordeClose");
        }
    }
    const auto legend = HordeControllerLegend(model, state, modal);
    const float legendHeight =
        Bool(model, "_usingGamepad") ? DrawControllerLegend(legend, fonts, scale, (width - 56) * scale) / scale : 0;
    const float contentHeight = height - legendHeight;
    c.Line(half, 28, half, contentHeight - 28, IM_COL32(245, 158, 11, 80), 1.6f);
    DrawRoster(c, model, half, contentHeight);
    if (state.dismissed)
        DrawRegistry(c, model, half, width, contentHeight);
    else
        DrawDossier(c, model, half, width, contentHeight);
    ImGui::SetCursorScreenPos(c.P(width - 54, 22));
    ImGui::BeginChild("CloseButton", {33 * scale, 33 * scale}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    if (c.Button("close", "", width - 54, 22, 32, 32, Section::Commands))
        c.Send("hordeClose");
    c.Line(width - 44, 32, width - 32, 44, Palette::CopperLight, 1.8f);
    c.Line(width - 32, 32, width - 44, 44, Palette::CopperLight, 1.8f);
    ImGui::EndChild();
    if (Bool(model, "_usingGamepad"))
    {
        const float top = contentHeight - 14;
        c.Line(28, top, width - 28, top, Palette::Border);
        DrawControllerLegend(legend, fonts, scale, (width - 56) * scale, c.draw(), top * scale, 28 * scale);
        result.controllerLegend = {28 * scale, top * scale, (width - 56) * scale, legendHeight * scale};
    }

    if (state.confirmRequested)
    {
        ImGui::OpenPopup("HordeConfirm");
        state.confirmRequested = false;
    }
    ImGui::SetNextWindowPos({io.DisplaySize.x / 2, io.DisplaySize.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380 * scale, 205 * scale});
    if (ImGui::BeginPopupModal("HordeConfirm", nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings))
    {
        state.scopedNavigation = false;
        Canvas m{fonts, state, result, scale, interactive};
        const auto pos = ImGui::GetWindowPos();
        m.offsetX = pos.x / scale;
        m.offsetY = pos.y / scale;
        m.Border(0, 0, 380, 205, Alpha(Palette::Amber, 0.3f), 14);
        const auto titleWidth =
            fonts.heading->CalcTextSizeA(17 * scale * FontMetricScale, FLT_MAX, 0, state.confirmTitle.c_str()).x /
            scale;
        m.Text(std::max(24.0f, (380 - titleWidth) / 2), 23, state.confirmTitle, 17, Palette::Amber, fonts.heading, 332);
        ImGui::SetCursorPos({24 * scale, 64 * scale});
        ImGui::BeginChild("ConfirmationText", {332 * scale, 73 * scale}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(state.confirmText.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        const bool canceled =
            interactive && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight));
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool cancel = m.Button("confirm-cancel", "Cancel", 24, 151, 161, 34, Section::Commands);
        ImGui::SetItemDefaultFocus();
        const bool confirm = m.Button("confirm-accept", state.confirmLabel, 195, 151, 161, 34, Section::Commands, false,
                                      true, !state.pending.name.empty());
        if (canceled || cancel || state.pending.name.empty())
        {
            state.pending = {};
            ImGui::CloseCurrentPopup();
            state.focusRequested = true;
        }
        else if (confirm)
        {
            result.actions.push_back(std::move(state.pending));
            state.pending = {};
            ImGui::CloseCurrentPopup();
            state.focusRequested = true;
        }
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::PopFont();
    return result;
}
} // namespace Horde::ImGuiUI
