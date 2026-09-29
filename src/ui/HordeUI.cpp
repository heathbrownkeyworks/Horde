#include "pch.h"
#include "ui/HordeUI.h"
#include "ui/imgui/ImGuiHost.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "Settings.h"

using Horde::ImGuiUI::ImGuiHost;

// Handler bodies execute inside Dispatch's guarded game-thread task.
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
    ImGuiHost::GetSingleton().Initialize();
}
bool HordeUI::IsOpen() const
{
    return ImGuiHost::GetSingleton().IsOpen();
}
void HordeUI::Close()
{
    ImGuiHost::GetSingleton().RequestClose();
}
void HordeUI::Toggle()
{
    SKSE::GetTaskInterface()->AddTask([this] {
        auto &host = ImGuiHost::GetSingleton();
        if (host.IsOpen())
        {
            Close();
            return;
        }
        RE::FormID selected = 0;
        auto *crosshairPick = RE::CrosshairPickData::GetSingleton();
        if (crosshairPick)
        {
            auto refPtr = crosshairPick->GetActiveTarget().get();
            auto *actor = refPtr ? refPtr->As<RE::Actor>() : nullptr;
            if (actor && FollowerManager::GetSingleton().IsTracked(actor->GetFormID()))
                selected = actor->GetFormID();
        }
        if (host.RequestOpen(selected))
            PushStateToView();
    });
}
void HordeUI::Dispatch(Horde::ImGuiUI::Action action, std::uint64_t generation)
{
    SKSE::GetTaskInterface()->AddTask([this, action = std::move(action), generation] {
        auto &host = ImGuiHost::GetSingleton();
        if (!host.IsOpen() || !host.HasFocus() || host.Generation() != generation)
            return;
        const auto handler = _handlers.find(action.name);
        if (handler == _handlers.end())
        {
            logger::warn("HordeUI: unknown native action {}", action.name);
            return;
        }
        const auto payload = action.data.dump();
        handler->second(payload.c_str());
    });
}
void HordeUI::RefreshState()
{
    if (IsOpen())
        PushStateToView();
}
void HordeUI::PushStateToView()
{
    if (!IsOpen())
        return;
    auto state = nlohmann::json::parse(BuildStateJSON());
    state.update(nlohmann::json::parse(BuildDismissedJSON()));
    ImGuiHost::GetSingleton().Publish(std::move(state));
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

    return j.dump();
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
    return j.dump();
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
