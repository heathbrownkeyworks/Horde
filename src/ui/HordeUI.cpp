#include "pch.h"
#include "ui/HordeUI.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "Settings.h"
#include "GameText.h"
#include "MagelightUIAPI/MagelightUI_API.h"

#include <array>
#include <filesystem>
#include <memory>


namespace
{
// The page's native calls, each registered as a Magelight JS listener.
constexpr std::array<const char *, 23> kListeners = {
    "hordeGetState",         "hordeClose",           "hordeSetFollow",        "hordeSetWait",
    "hordeSetPassive",       "hordeSetSandbox",      "hordeSetFollowClose",   "hordeSetEssential",
    "hordeSummon",           "hordeSummonAll",       "hordeFollowAll",        "hordeWaitAll",
    "hordePassiveAll",       "hordeDismiss",         "hordeSetHome",          "hordeClearHome",
    "hordeUpdateSettings",   "hordeToggleInputMode", "hordeGetDismissed",     "hordeSummonDismissed",
    "hordeSetDismissedHome", "hordeClearDismissedHome", "hordeForgetFollower"};

// 1.1.0 added CancelUIModeRequest, which a close right after an open needs.
constexpr std::uint32_t kMagelightMinHost = MAGELIGHT_API::PackVersion(1, 1, 0);

// A page command copied off Magelight's render thread for the game thread.
struct PageCommand
{
    const char *name;
    std::string payload;
};

// Horde installs its page in Magelight's mod tree, Data/Magelight/Horde/.
std::string MagelightPagePath()
{
    wchar_t exe[MAX_PATH]{};
    if (!::GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return {};
    const auto page = std::filesystem::path(exe).parent_path() / L"Data" / L"Magelight" / L"Horde" / L"index.html";
    std::error_code error;
    if (!std::filesystem::exists(page, error))
        return {};
    const auto utf8 = page.u8string();
    return std::string(utf8.begin(), utf8.end());
}

RE::FormID CrosshairFollower()
{
    auto *crosshairPick = RE::CrosshairPickData::GetSingleton();
    if (!crosshairPick)
        return 0;
    auto refPtr = crosshairPick->GetActiveTarget().get();
    auto *actor = refPtr ? refPtr->As<RE::Actor>() : nullptr;
    return actor && FollowerManager::GetSingleton().IsTracked(actor->GetFormID()) ? actor->GetFormID() : 0;
}
} // namespace

// Handler bodies execute inside RunMagelightCommand, on the game thread.
template <class F> static void RunOnGameThread(F &&operation)
{
    operation();
}

HordeUI &HordeUI::GetSingleton()
{
    static HordeUI singleton;
    return singleton;
}
void HordeUI::Initialize()
{
    RegisterListeners();

    // navigator.language does not follow the game's language, so the page
    // takes it from each state push.
    auto *ini = RE::INISettingCollection::GetSingleton();
    auto *language = ini ? ini->GetSetting("sLanguage:General") : nullptr;
    const char *skyrimLanguage = language && language->GetString() ? language->GetString() : "";
    _language = Horde::LanguageTag(skyrimLanguage);
    logger::info("HordeUI: game language '{}', page language {}", skyrimLanguage, _language);

    if (!InitializeMagelight())
        logger::warn("HordeUI: no UI host; the menu needs Magelight UI 1.1 or newer");
}
void HordeUI::Toggle()
{
    SKSE::GetTaskInterface()->AddTask([this] {
        if (!HasHost())
        {
            // Shown even with notifications muted: it explains a menu that will not open.
            RE::SendHUDMessage::ShowHUDMessage("Horde's menu needs Magelight UI 1.1 or newer.");
            logger::warn("HordeUI: cannot open the menu without Magelight UI");
            return;
        }
        if (_open)
            Close();
        else
            Open();
    });
}

bool HordeUI::InitializeMagelight()
{
    using namespace MAGELIGHT_API;
    const auto *api = RequestApi4();
    if (!api)
        return false;
    if (api->hostVersionNumber < kMagelightMinHost)
    {
        logger::warn("HordeUI: Magelight UI {} is older than 1.1.0", api->hostVersion);
        return false;
    }
    const auto page = MagelightPagePath();
    if (page.empty())
    {
        logger::error("HordeUI: Data/Magelight/Horde/index.html is missing; reinstall Horde");
        return false;
    }

    ModDesc mod{};
    mod.size = sizeof(mod);
    mod.modId = "Horde";
    mod.displayName = "Horde";
    mod.modVersion = PackVersion(Plugin::VERSION.major(), Plugin::VERSION.minor(), Plugin::VERSION.patch());
    mod.minHostVersion = kMagelightMinHost;
    mod.callbackThread = CallbackThread::GameThread;
    mod.onEvent = &HordeUI::OnMagelightEvent;
    ModId modId = 0;
    if (const auto registered = api->RegisterMod(&mod, &modId); registered != Result::Ok)
    {
        logger::error("HordeUI: Magelight refused to register Horde ({})", static_cast<std::int32_t>(registered));
        return false;
    }

    ViewDesc desc{};
    desc.size = sizeof(desc);
    desc.name = "main";
    desc.htmlPath = page.c_str();
    desc.fullscreen = true;
    desc.layer = Layer::Panel;
    ViewId view = 0;
    if (const auto created = api->CreateViewEx(modId, &desc, &view); created != Result::Ok)
    {
        logger::error("HordeUI: Magelight could not create the Horde view ({}): {}", static_cast<std::int32_t>(created),
                      api->GetLastErrorMessage(modId));
        api->UnregisterMod(modId);
        return false;
    }
    _magelight = api;
    _magelightMod = modId;
    _view = view;

    for (const auto *name : kListeners)
    {
        if (!_handlers.contains(name))
            logger::error("HordeUI: no handler for page command {}", name);
        // The name doubles as the user pointer; kListeners has static storage.
        if (api->RegisterJSListenerEx(view, name, &HordeUI::OnMagelightListener, const_cast<char *>(name)) != Result::Ok)
            logger::error("HordeUI: Magelight refused listener {}: {}", name, api->GetLastErrorMessage(modId));
    }
    // The page closes its own dialogs on Escape, then sends hordeClose.
    api->SetEscapeCapture(view, true);
    logger::info("HordeUI: using Magelight UI {} ({})", api->hostVersion, page);
    logger::info("HordeUI: Magelight has no controller input on flat Skyrim; Horde opens with its hotkey");
    return true;
}

void HordeUI::Open()
{
    using namespace MAGELIGHT_API;
    if (_open || !_magelight->IsViewValid(_view))
        return;
    _magelight->ShowView(_view, true);
    const auto requested = _magelight->RequestUIMode(_view, kUIModeFlagPause);
    if (requested != Result::Ok)
    {
        _magelight->ShowView(_view, false);
        if (requested == Result::Busy)
            logger::info("HordeUI: another Magelight page holds the cursor; Horde stays closed");
        else
            logger::warn("HordeUI: Magelight refused the menu ({}): {}", static_cast<std::int32_t>(requested),
                         _magelight->GetLastErrorMessage(_magelightMod));
        return;
    }
    _open = true;
    // Open on the follower under the crosshair, if any.
    const auto selected = CrosshairFollower();
    ExecuteScript(selected ? "hordeShowPanel(" + std::to_string(selected) + ")" : std::string("hordeShowPanel()"));
    PushStateToView();
}

void HordeUI::Close()
{
    if (!_open)
        return;
    _open = false;
    ExecuteScript("window.hordeHidePanel && hordeHidePanel()");
    // Withdraw an entry the host has not run yet; release one it has.
    if (_magelight->CancelUIModeRequest(_magelightMod) != MAGELIGHT_API::Result::Ok)
        _magelight->ReleaseUIMode(_magelightMod);
    _magelight->ShowView(_view, false);
}

void HordeUI::OnMagelightListener(std::uint64_t, const char *argument, void *user)
{
    // Render thread, inside the frame: copy the argument and let the host post
    // it to the game thread. Posting to SKSE's task queue from here can hang.
    const auto *name = static_cast<const char *>(user);
    auto command = std::make_unique<PageCommand>(PageCommand{name, argument ? argument : ""});
    const auto *api = GetSingleton()._magelight;
    if (api && api->PostGameTask(&HordeUI::RunMagelightCommand, command.get()) == MAGELIGHT_API::Result::Ok)
        command.release();
    else
        logger::warn("HordeUI: could not post page command {}", name);
}

void HordeUI::RunMagelightCommand(void *user)
{
    const std::unique_ptr<PageCommand> command(static_cast<PageCommand *>(user));
    auto &ui = GetSingleton();
    const auto handler = ui._handlers.find(command->name);
    if (handler != ui._handlers.end())
        handler->second(command->payload.c_str());
}

void HordeUI::OnMagelightEvent(const MAGELIGHT_API::EventData *event, void *)
{
    using MAGELIGHT_API::Event;
    if (!event)
        return;
    auto &ui = GetSingleton();
    if (event->type == Event::RenderDead)
    {
        logger::error("HordeUI: Magelight turned its overlay off for this session; Horde's menu cannot open");
        return;
    }
    if (event->view != ui._view)
        return;

    switch (event->type)
    {
    case Event::ViewDomReady:
        if (ui._open)
        {
            ui.ExecuteScript("hordeShowPanel()");
            ui.PushStateToView();
        }
        break;
    case Event::UIModeEntered:
        // An entry that lands after Horde closed is given back.
        if (!ui._open)
            ui._magelight->ReleaseUIMode(ui._magelightMod);
        break;
    case Event::UIModeExited:
    case Event::UIModeRefused:
        // The host's toggle key, a save load or a lost race end UI mode too.
        if (ui._open)
        {
            ui._open = false;
            ui.ExecuteScript("window.hordeHidePanel && hordeHidePanel()");
            ui._magelight->ShowView(ui._view, false);
        }
        break;
    case Event::ViewLoadFailed:
        logger::error("HordeUI: Magelight could not load Horde's page: {}", event->detail ? event->detail : "");
        ui.Close();
        break;
    default:
        break;
    }
}

void HordeUI::ExecuteScript(const std::string &script)
{
    if (_magelight)
        _magelight->InvokeJS(_view, script.c_str());
}
void HordeUI::RefreshState()
{
    if (IsOpen())
        PushStateToView();
}
void HordeUI::PushStateToView()
{
    if (!IsOpen() || !HasHost())
        return;
    // Runs on the game thread: a bad name must cost one refresh, not the game.
    try
    {
        // Parsing the dumped text hands the page valid UTF-8.
        auto state = nlohmann::json::parse(BuildStateJSON());
        state.update(nlohmann::json::parse(BuildDismissedJSON()));
        state["language"] = _language;
        ExecuteScript("hordeUpdateState(" + Horde::DumpGameJSON(state) + ")");
    }
    catch (const std::exception &e)
    {
        logger::error("HordeUI: could not publish state: {}", e.what());
    }
}
void HordeUI::PushDismissedToView()
{
    PushStateToView();
}

std::string HordeUI::BuildStateJSON() const
{
    auto &mgr = FollowerManager::GetSingleton();
    auto &settings = Settings::GetSingleton();

    nlohmann::json j;
    j["maxFollowers"] = settings.GetMaxFollowers();
    j["count"] = mgr.GetCount();
    j["dismissedCount"] = static_cast<int>(mgr.GetDismissedFollowers().size());
    j["defaultSandboxEnabled"] = settings.GetDefaultSandboxEnabled();
    j["notificationsEnabled"] = settings.GetNotificationsEnabled();
    j["followDistance"] = settings.GetFollowDistance();
    j["useKeybinds"] = settings.GetUseKeybinds();
    j["uiScale"] = settings.GetUIScale();
    j["allPassive"] = mgr.IsAllPassive();

    nlohmann::json followersArray = nlohmann::json::array();
    for (const auto &f : mgr.GetFollowers())
    {
        nlohmann::json fj;
        to_json(fj, f);

        // Inject live actor values (not serialized to disk)
        auto *actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
        if (actor)
        {
            auto *avo = actor->AsActorValueOwner();
            fj["health"] = avo->GetActorValue(RE::ActorValue::kHealth);
            fj["healthMax"] = avo->GetPermanentActorValue(RE::ActorValue::kHealth);
            fj["magicka"] = avo->GetActorValue(RE::ActorValue::kMagicka);
            fj["magickaMax"] = avo->GetPermanentActorValue(RE::ActorValue::kMagicka);
            fj["level"] = actor->GetLevel(); // refresh live level

            // Stamina
            fj["stamina"] = avo->GetActorValue(RE::ActorValue::kStamina);
            fj["staminaMax"] = avo->GetPermanentActorValue(RE::ActorValue::kStamina);

            // Race
            auto *race = actor->GetRace();
            fj["race"] = race ? race->GetFullName() : "Unknown";

            // Armor Rating (DamageResist AV)
            fj["armorRating"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kDamageResist));

            // Display the current location, not the actor's editor location.
            auto *loc = actor->GetCurrentLocation();
            fj["location"] = (loc && loc->GetFullName()[0]) ? loc->GetFullName() : "Wilderness";

            // Distance from player
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (player)
            {
                auto d = actor->GetPosition().GetDistance(player->GetPosition());
                fj["distance"] = static_cast<int>(d / 71.0f);
            }
            else
            {
                fj["distance"] = 0;
            }

            // Combat skills
            nlohmann::json skills;
            skills["oneHanded"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kOneHanded));
            skills["twoHanded"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kTwoHanded));
            skills["archery"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kArchery));
            skills["block"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kBlock));
            skills["heavyArmor"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kHeavyArmor));
            skills["lightArmor"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kLightArmor));
            skills["destruction"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kDestruction));
            skills["restoration"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kRestoration));
            skills["conjuration"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kConjuration));
            fj["skills"] = skills;

            // Perks (via ActorBase's BGSPerkRankArray)
            nlohmann::json perksArray = nlohmann::json::array();
            auto *actorBase = actor->GetActorBase();
            if (actorBase && actorBase->perkCount > 0 && actorBase->perks)
            {
                for (std::uint32_t i = 0; i < actorBase->perkCount; ++i)
                {
                    auto &entry = actorBase->perks[i];
                    if (entry.perk)
                    {
                        const char *name = entry.perk->GetFullName();
                        if (name && name[0])
                        {
                            perksArray.push_back(name);
                        }
                    }
                }
            }
            fj["perks"] = perksArray;

            // Equipment
            nlohmann::json equip;

            auto weaponEntry = [](RE::TESForm *form) -> nlohmann::json {
                nlohmann::json e;
                if (!form)
                {
                    e["name"] = "None";
                    e["stat"] = 0;
                    e["isWeapon"] = false;
                    return e;
                }
                e["name"] = form->GetName();
                auto *weap = form->As<RE::TESObjectWEAP>();
                if (weap)
                {
                    e["stat"] = weap->GetAttackDamage();
                    e["isWeapon"] = true;
                }
                else
                {
                    auto *armor = form->As<RE::TESObjectARMO>();
                    e["stat"] = armor ? static_cast<int>(armor->GetArmorRating()) : 0;
                    e["isWeapon"] = false;
                }
                return e;
            };

            equip["rightHand"] = weaponEntry(actor->GetEquippedObject(false));
            equip["leftHand"] = weaponEntry(actor->GetEquippedObject(true));

            auto armorEntry = [&](RE::BGSBipedObjectForm::BipedObjectSlot slot) -> nlohmann::json {
                nlohmann::json e;
                auto *armor = actor->GetWornArmor(slot);
                if (!armor)
                {
                    e["name"] = "None";
                    e["stat"] = 0;
                    return e;
                }
                e["name"] = armor->GetFullName();
                e["stat"] = static_cast<int>(armor->GetArmorRating());
                return e;
            };

            equip["head"] = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kHead);
            equip["body"] = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
            equip["hands"] = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kHands);
            equip["feet"] = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kFeet);
            fj["equipment"] = equip;
        }

        followersArray.push_back(fj);
    }
    j["followers"] = followersArray;

    return Horde::DumpGameJSON(j);
}

std::string HordeUI::BuildDismissedJSON() const
{
    auto dismissed = FollowerManager::GetSingleton().GetDismissedFollowers();

    nlohmann::json j;
    nlohmann::json arr = nlohmann::json::array();
    for (auto &d : dismissed)
    {
        nlohmann::json dj;
        dj["formID"] = d.formID;
        dj["name"] = d.name;
        dj["homeName"] = d.homeName;
        dj["hasHome"] = d.hasHome;
        arr.push_back(dj);
    }
    j["dismissed"] = arr;
    return Horde::DumpGameJSON(j);
}

// Helper to parse FormID from JSON argument string
static RE::FormID ParseFormID(const char *data)
{
    try
    {
        auto j = nlohmann::json::parse(data);
        if (j.contains("formID"))
        {
            if (j["formID"].is_number())
            {
                return j["formID"].get<RE::FormID>();
            }
            if (j["formID"].is_string())
            {
                return static_cast<RE::FormID>(std::stoul(j["formID"].get<std::string>()));
            }
        }
        return 0;
    }
    catch (...)
    {
        return 0;
    }
}

void HordeUI::RegisterListeners()
{
    _handlers.clear();

    _handlers.emplace("hordeGetState",
                      [](const char *) { RunOnGameThread([]() { HordeUI::GetSingleton().PushStateToView(); }); });

    // Close from the in-UI close glyph / Escape key (mirrors the keybind toggle)
    _handlers.emplace("hordeClose", [](const char *) {
        RunOnGameThread([]() {
            auto &ui = HordeUI::GetSingleton();
            if (ui.IsOpen())
            {
                ui.Close();
            }
        });
    });

    _handlers.emplace("hordeSetFollow", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().SetFollow(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeSetWait", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().SetWait(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeSetPassive", [](const char *data) {
        try
        {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool passive = j.value("passive", false);
            RunOnGameThread([id, passive]() {
                FollowerManager::GetSingleton().SetPassive(id, passive);
                HordeUI::GetSingleton().PushStateToView();
            });
        }
        catch (...)
        {
        }
    });

    _handlers.emplace("hordeSetSandbox", [](const char *data) {
        try
        {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool enabled = j.value("enabled", false);
            RunOnGameThread([id, enabled]() {
                FollowerManager::GetSingleton().SetSandbox(id, enabled);
                HordeUI::GetSingleton().PushStateToView();
            });
        }
        catch (...)
        {
        }
    });

    _handlers.emplace("hordeSetFollowClose", [](const char *data) {
        try
        {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool enabled = j.value("enabled", false);
            RunOnGameThread([id, enabled]() {
                FollowerManager::GetSingleton().SetFollowClose(id, enabled);
                HordeUI::GetSingleton().PushStateToView();
            });
        }
        catch (...)
        {
        }
    });

    _handlers.emplace("hordeSetEssential", [](const char *data) {
        try
        {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool essential = j.value("essential", true);
            RunOnGameThread([id, essential]() {
                FollowerManager::GetSingleton().SetEssential(id, essential);
                HordeUI::GetSingleton().PushStateToView();
            });
        }
        catch (...)
        {
        }
    });

    _handlers.emplace("hordeSummon", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().Summon(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeSummonAll", [](const char *) {
        RunOnGameThread([]() {
            FollowerManager::GetSingleton().SummonAll();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeFollowAll", [](const char *) {
        RunOnGameThread([]() {
            FollowerManager::GetSingleton().FollowAll();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeWaitAll", [](const char *) {
        RunOnGameThread([]() {
            FollowerManager::GetSingleton().WaitAll();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    // An explicit passive value sets the state; omission toggles the party.
    _handlers.emplace("hordePassiveAll", [](const char *data) {
        bool explicitValue = false;
        bool passive = false;
        try
        {
            auto j = nlohmann::json::parse(data);
            if (j.contains("passive"))
            {
                passive = j["passive"].get<bool>();
                explicitValue = true;
            }
        }
        catch (...)
        {
        }

        RunOnGameThread([passive, explicitValue]() {
            auto &mgr = FollowerManager::GetSingleton();
            mgr.SetPassiveAll(explicitValue ? passive : !mgr.IsAllPassive());
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeDismiss", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().UntrackFollower(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeSetHome", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().SetHome(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeClearHome", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().ClearHome(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    _handlers.emplace("hordeUpdateSettings", [](const char *data) {
        try
        {
            auto j = nlohmann::json::parse(data);
            auto &s = Settings::GetSingleton();
            if (j.contains("maxFollowers"))
                s.SetMaxFollowers(j["maxFollowers"].get<int>());
            if (j.contains("defaultSandboxEnabled"))
                s.SetDefaultSandboxEnabled(j["defaultSandboxEnabled"].get<bool>());
            if (j.contains("notificationsEnabled"))
                s.SetNotificationsEnabled(j["notificationsEnabled"].get<bool>());
            if (j.contains("uiScale"))
                s.SetUIScale(j["uiScale"].get<int>());
            std::string distancePreset;
            if (j.contains("followDistance"))
            {
                distancePreset = j["followDistance"].get<std::string>();
                s.SetFollowDistance(distancePreset);
            }
            // Package edits and reevaluation must run on the game thread.
            RunOnGameThread([distancePreset]() {
                if (!distancePreset.empty())
                {
                    PackageManager::GetSingleton().ApplyFollowDistance(distancePreset);
                }
                HordeUI::GetSingleton().PushStateToView();
            });
        }
        catch (...)
        {
        }
    });

    _handlers.emplace("hordeToggleInputMode", [](const char *) {
        RunOnGameThread([]() {
            extern void ToggleInputMode();
            ToggleInputMode();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    // --- Dismissed follower listeners ---

    _handlers.emplace("hordeGetDismissed",
                      [](const char *) { RunOnGameThread([]() { HordeUI::GetSingleton().PushDismissedToView(); }); });

    _handlers.emplace("hordeSummonDismissed", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().SummonDismissed(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    _handlers.emplace("hordeSetDismissedHome", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().SetDismissedHome(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    _handlers.emplace("hordeClearDismissedHome", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().ClearDismissedHome(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    _handlers.emplace("hordeForgetFollower", [](const char *data) {
        auto id = ParseFormID(data);
        RunOnGameThread([id]() {
            FollowerManager::GetSingleton().ForgetFollower(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    logger::info("HordeUI: {} native actions registered", _handlers.size());
}
