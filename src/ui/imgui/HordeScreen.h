#pragma once
#include "Theme.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Horde::ImGuiUI
{
using Model = nlohmann::json;
struct Action
{
    std::string name;
    Model data = Model::object();
};
struct Rect
{
    float x{}, y{}, width{}, height{};
};
enum class Section
{
    Followers,
    Commands,
    Group,
    Settings
};
struct HitTarget
{
    std::string id;
    Rect bounds;
    bool enabled = true;
};
struct ScreenState
{
    std::uint32_t selected = 0;
    bool dismissed = false, focusRequested = true, confirmRequested = false;
    Section section = Section::Followers;
    std::string confirmTitle, confirmText, confirmLabel;
    Action pending;
    float rightScroll = 0;
    bool scopedNavigation = false;
};
struct FrameResult
{
    std::vector<Action> actions;
    std::vector<HitTarget> targets;
    Rect controllerLegend;
};
const Model *FindFollower(const Model &, std::uint32_t id);
void ReconcileSelection(const Model &, ScreenState &);
void Confirm(ScreenState &, std::string title, std::string message, std::string label, Action action);
FrameResult DrawHorde(const Model &, ScreenState &, const Fonts &, bool interactive = true);
} // namespace Horde::ImGuiUI
