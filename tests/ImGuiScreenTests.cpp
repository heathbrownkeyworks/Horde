#include "ui/imgui/HordeScreen.h"
#include "ui/imgui/InputMap.h"
#include "ui/imgui/ControllerLegend.h"
#include <imgui_internal.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace Horde::ImGuiUI;
namespace
{
int assertions = 0;
void Check(bool value, const std::string &message)
{
    ++assertions;
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
struct Harness
{
    Model model;
    ScreenState state;
    Fonts fonts;
    FrameResult last;
    Harness(bool nativeFonts = false)
    {
        std::ifstream file("tools/imgui-preview/fixture.json");
        file >> model;
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {1920, 1080};
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetCurrentContext()->ConfigNavWindowingWithGamepad = false;
        if (nativeFonts)
            Check(LoadFonts(io, "assets/fonts", fonts), "load shipped fonts for measured legend layout");
        else
        {
            fonts.body = io.Fonts->AddFontDefault();
            fonts.medium = fonts.bold = fonts.heading = fonts.body;
        }
        Frame();
        Frame();
    }
    ~Harness()
    {
        ImGui::DestroyContext();
    }
    const FrameResult &Frame(bool interactive = true)
    {
        ImGui::NewFrame();
        last = DrawHorde(model, state, fonts, interactive);
        ImGui::Render();
        Check(ImGui::GetDrawData()->Valid, "valid native draw data");
        return last;
    }
    HitTarget Target(const std::string &id)
    {
        auto it = std::find_if(last.targets.begin(), last.targets.end(), [&](const auto &t) { return t.id == id; });
        Check(it != last.targets.end(), "target exists: " + id);
        return *it;
    }
    FrameResult Click(const std::string &id, bool interactive = true)
    {
        Frame(interactive);
        const auto r = Target(id).bounds;
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(r.x + r.width / 2, r.y + r.height / 2);
        Frame(interactive);
        io.AddMouseButtonEvent(0, true);
        Frame(interactive);
        io.AddMouseButtonEvent(0, false);
        return Frame(interactive);
    }
    FrameResult Key(ImGuiKey key)
    {
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(key, true);
        auto result = Frame();
        io.AddKeyEvent(key, false);
        Frame();
        return result;
    }
    void Expect(const std::string &target, const std::string &action, Model payload = Model::object())
    {
        auto result = Click(target);
        Check(result.actions.size() == 1, "one action: " + target);
        Check(result.actions[0].name == action, "action identity: " + target);
        Check(result.actions[0].data == payload, "action payload: " + target);
    }
};
} // namespace
int main()
{
    {
        Harness h;
        Check(h.last.actions.empty(), "initial screen does not mutate followers");
        Check(h.state.selected == 1001, "first follower selected initially");
        h.state.selected = 1004;
        h.state.focusRequested = true;
        h.Frame();
        h.Frame();
        Check(ImGui::GetCurrentContext()->NavId == ImGui::GetCurrentContext()->NavWindow->GetID("follower-1004"),
              "crosshair selection receives keyboard focus by identity");
        std::reverse(h.model["followers"].begin(), h.model["followers"].end());
        h.Frame();
        Check(h.state.selected == 1004, "identity survives roster reorder");
        Check(ImGui::GetCurrentContext()->NavId == ImGui::GetCurrentContext()->NavWindow->GetID("follower-1004"),
              "roster reorder preserves keyboard focus by identity");
        h.model["followers"].erase(std::remove_if(h.model["followers"].begin(), h.model["followers"].end(),
                                                  [](const Model &f) { return f["formID"] == 1004; }),
                                   h.model["followers"].end());
        h.Frame();
        Check(h.state.selected == 1016, "removed selection falls back safely");
        h.model["followers"] = Model::array();
        h.Frame();
        Check(h.state.selected == 0, "empty roster clears selection");
        Check(
            std::none_of(h.last.targets.begin(), h.last.targets.end(), [](const auto &t) { return t.id == "dismiss"; }),
            "no actor commands for empty roster");
    }
    {
        Harness h;
        const Model target = {{"formID", 1001}};
        h.Expect("summon-all", "hordeSummonAll");
        h.Expect("follow-all", "hordeFollowAll");
        h.Expect("wait-all", "hordeWaitAll");
        h.Expect("passive-all", "hordePassiveAll", {{"passive", true}});
        h.model["allPassive"] = true;
        h.Expect("passive-all", "hordePassiveAll", {{"passive", false}});
        h.Expect("follow", "hordeSetFollow", target);
        h.Expect("wait", "hordeSetWait", target);
        h.Expect("passive", "hordeSetPassive", {{"formID", 1001}, {"passive", true}});
        h.Expect("follow-close", "hordeSetFollowClose", {{"formID", 1001}, {"enabled", false}});
        h.Expect("sandbox", "hordeSetSandbox", {{"formID", 1001}, {"enabled", false}});
        h.Expect("essential", "hordeSetEssential", {{"formID", 1001}, {"essential", false}});
        h.Expect("summon", "hordeSummon", target);
        h.Expect("set-home", "hordeSetHome", target);
        for (const char *value : {"close", "normal", "far"})
            h.Expect(std::string("distance-") + value, "hordeUpdateSettings", {{"followDistance", value}});
        h.Expect("notifications", "hordeUpdateSettings", {{"notificationsEnabled", false}});
        h.Expect("input-mode", "hordeToggleInputMode");
        auto result = h.Click("wait", false);
        Check(result.actions.empty(), "disabled frame rejects mutations");
        h.Expect("close", "hordeClose");
    }
    {
        Harness h;
        Check(h.Click("dismiss").actions.empty(), "dismiss opens confirmation without mutation");
        h.Frame();
        h.Frame();
        auto *modal = ImGui::FindWindowByName("HordeConfirm");
        Check(modal && modal->Active, "confirmation visible");
        Check(ImGui::GetCurrentContext()->NavId == modal->GetID("confirm-cancel"),
              "Cancel owns initial confirmation focus");
        Check(h.Click("confirm-cancel").actions.empty(), "cancel does not dismiss");
        h.Frame();
        h.Click("dismiss");
        h.Frame();
        h.Expect("confirm-accept", "hordeDismiss", {{"formID", 1001}});
        h.Frame();
        h.Click("dismiss");
        Check(h.Key(ImGuiKey_Escape).actions.empty(), "Escape only cancels confirmation");
        h.Frame();
        Check(h.state.pending.name.empty(), "Escape discards pending command");
        h.Click("dismiss");
        h.model["followers"] = Model::array();
        h.Frame();
        h.Frame();
        Check(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "actor removal closes stale confirmation");
    }
    {
        Harness h;
        h.state.selected = 1006;
        h.Frame();
        Check(h.Click("clear-home").actions.empty(), "active clear home needs confirmation");
        h.Frame();
        h.Expect("confirm-accept", "hordeClearHome", {{"formID", 1006}});
        h.Frame();
        h.Expect("dismissed", "hordeGetDismissed");
        Check(h.state.dismissed, "registry opens beside roster");
        h.Frame();
        h.Expect("registry-summon-2001", "hordeSummonDismissed", {{"formID", 2001}});
        h.Expect("registry-home-2001", "hordeSetDismissedHome", {{"formID", 2001}});
        h.Expect("registry-clear-2001", "hordeClearDismissedHome", {{"formID", 2001}});
        Check(h.Click("registry-forget-2001").actions.empty(), "forget needs confirmation");
        h.Frame();
        h.Expect("confirm-accept", "hordeForgetFollower", {{"formID", 2001}});
        h.Frame();
        h.Click("registry-back");
        Check(!h.state.dismissed, "registry back returns to dossier");
        h.Click("follower-1002");
        Check(h.state.selected == 1002 && h.state.section == Section::Commands,
              "roster click selects identity and commands");
    }
    {
        Harness h;
        h.model["_usingGamepad"] = true;
        h.Frame();
        h.Key(ImGuiKey_GamepadR1);
        h.Frame();
        Check(h.state.section == Section::Commands, "RB enters commands");
        h.Key(ImGuiKey_GamepadR1);
        h.Frame();
        Check(h.state.section == Section::Group, "RB enters group");
        h.Key(ImGuiKey_GamepadR1);
        h.Frame();
        Check(h.state.section == Section::Settings, "RB enters settings");
        h.Key(ImGuiKey_GamepadFaceRight);
        h.Frame();
        Check(h.state.section == Section::Followers, "B returns to followers");
        auto result = h.Key(ImGuiKey_GamepadFaceUp);
        Check(h.state.dismissed, "Y opens dismissed registry");
        Check(result.actions.size() == 1 && result.actions[0].name == "hordeGetDismissed",
              "Y requests registry snapshot");
        h.Key(ImGuiKey_GamepadFaceRight);
        h.Frame();
        Check(!h.state.dismissed, "B leaves registry");
        result = h.Key(ImGuiKey_GamepadFaceRight);
        Check(result.actions.size() == 1 && result.actions[0].name == "hordeClose", "B closes from followers");
    }
    {
        Harness h;
        for (const ImVec2 size : {ImVec2{1280, 720}, ImVec2{1920, 1080}, ImVec2{2560, 1440}, ImVec2{3440, 1440}})
        {
            ImGui::GetIO().DisplaySize = size;
            h.Frame();
            h.Frame();
            for (const auto &t : h.last.targets)
            {
                Check(t.bounds.x >= 0 && t.bounds.y >= 0, "target starts on screen: " + t.id);
                Check(t.bounds.x + t.bounds.width <= size.x && t.bounds.y + t.bounds.height <= size.y,
                      "target fits screen: " + t.id);
            }
        }
        h.model["followers"][0]["name"] = std::string(350, 'W');
        h.model["followers"][0]["healthMax"] = 0;
        h.Frame();
        Check(h.last.actions.empty(), "long names and zero maxima draw without mutation");
        h.Click("dismiss");
        h.Frame();
        Check(h.Click("confirm-cancel").actions.empty(), "long confirmation text leaves Cancel usable");
        h.Frame();
        h.state.dismissed = true;
        h.model["dismissed"] = Model::array();
        h.Frame();
        Check(h.last.actions.empty(), "empty registry draws without commands");
    }
    {
        Harness h(true);
        Check(h.last.controllerLegend.height == 0, "controller legend hidden for keyboard and mouse");
        h.model["_usingGamepad"] = true;
        while (h.model["followers"].size() < 20)
        {
            auto follower = h.model["followers"][0];
            follower["formID"] = 3000 + h.model["followers"].size();
            h.model["followers"].push_back(follower);
        }
        for (const auto *family : {"xbox", "playstation", "generic"})
        {
            h.model["_controllerGlyphs"] = family;
            for (const ImVec2 size : {ImVec2{1280, 720}, ImVec2{1920, 1080}, ImVec2{2560, 1440}, ImVec2{3440, 1440}})
            {
                ImGui::GetIO().DisplaySize = size;
                h.Frame();
                h.Frame();
                const auto rail = h.last.controllerLegend;
                Check(rail.height > 0 && rail.y + rail.height < size.y, "legend stays inside frame");
                for (const auto &target : h.last.targets)
                    Check(target.bounds.y + target.bounds.height <= rail.y,
                          "reserved legend never overlaps control: " + target.id);
            }
        }
        h.model["_controllerGlyphs"] = "playstation";
        Check(ControllerBinding(h.model, "South", "South") == "Cross", "PlayStation face buttons use symbol names");
        const auto hasLabel = [](const ControllerLegend &legend, const char *label) {
            return std::any_of(legend.hints.begin(), legend.hints.end(),
                               [&](const auto &hint) { return hint.label == label; });
        };
        h.state.section = Section::Followers;
        Check(hasLabel(HordeControllerLegend(h.model, h.state, false), "Dismissed"),
              "registry shortcut shown when available");
        h.model["dismissed"] = Model::array();
        Check(!hasLabel(HordeControllerLegend(h.model, h.state, false), "Dismissed"),
              "empty registry has no inactive shortcut");
        h.state.section = Section::Settings;
        Check(!hasLabel(HordeControllerLegend(h.model, h.state, false), "Scroll"),
              "settings has no inactive scroll hint");
        const auto modalLegend = HordeControllerLegend(h.model, h.state, true);
        Check(hasLabel(modalLegend, "Cancel") && !hasLabel(modalLegend, "Sections"),
              "confirmation only shows usable controls");
        h.model["_controllerCursor"] = true;
        const auto cursorLegend = HordeControllerLegend(h.model, h.state, false);
        Check(hasLabel(cursorLegend, "Move cursor") && hasLabel(cursorLegend, "Click") &&
                  hasLabel(cursorLegend, "Navigation"),
              "cursor mode describes movement, click and exit");
        h.model["_usingGamepad"] = false;
        h.Frame();
        Check(h.last.controllerLegend.height == 0, "mouse activity restores full content area");
    }
    Check(!input::IsDigitalControllerButton(9) && !input::IsDigitalControllerButton(10),
          "triggers do not corrupt held chord bits");
    Check(input::ControllerButton("LeftShoulder") == 0x100 && input::ControllerButton("North") == 0x8000,
          "section and registry button event IDs");
    Check(input::ScanCodeToImGuiKey(0x01) == ImGuiKey_Escape, "Escape mapping");
    std::cout << "Horde native UI: " << assertions << " assertions passed\n";
}
