#include "pch.h"
#include "ui/HordeUI.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "Settings.h"

extern Meridian::UI::View::IViewAPI* g_MeridianView;

HordeUI& HordeUI::GetSingleton()
{
    static HordeUI singleton;
    return singleton;
}

void HordeUI::Initialize()
{
    if (!g_MeridianView) {
        logger::error("HordeUI: Meridian.View/1 not available");
        return;
    }

    Meridian::UI::View::ViewCreateInfo viewInfo{};
    viewInfo.ownerName = "horde";
    viewInfo.viewName = "main";
    viewInfo.startUrl = "mod://horde/index.html";
    viewInfo.initiallyVisible = false;
    viewInfo.onDOMReady = [](Meridian::UI::View::ViewHandle) {
        logger::info("HordeUI: DOM ready");
    };
    _view = g_MeridianView->CreateView(&viewInfo);

    if (_view == Meridian::UI::View::INVALID_VIEW_HANDLE) {
        logger::error("HordeUI: failed to create Meridian view");
        return;
    }

    RegisterListeners();
    logger::info("HordeUI initialized");
}

void HordeUI::Toggle()
{
    if (!g_MeridianView || !g_MeridianView->IsValid(_view)) return;

    if (_isOpen) {
        g_MeridianView->Unfocus(_view);
        g_MeridianView->Hide(_view);
        _isOpen = false;
    } else {
        g_MeridianView->Show(_view);
        const auto focusResult = g_MeridianView->TryFocus(
            _view, Meridian::UI::View::FocusMode::PauseGame);
        if (focusResult == Meridian::UI::View::FocusResult::Granted ||
            focusResult == Meridian::UI::View::FocusResult::AlreadyFocused) {
            _isOpen = true;

            // If the crosshair is on a tracked follower, open directly to their detail card
            RE::FormID crosshairFollowerID = 0;
            auto* crosshairPick = RE::CrosshairPickData::GetSingleton();
            if (crosshairPick) {
                // CommonLib selects the flat crosshair or active VR controller target.
                auto refPtr = crosshairPick->GetActiveTarget().get();
                auto* ref   = refPtr.get();
                auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                if (actor && FollowerManager::GetSingleton().IsTracked(actor->GetFormID())) {
                    crosshairFollowerID = actor->GetFormID();
                }
            }

            if (crosshairFollowerID != 0) {
                std::string script = "hordeShowPanel(" + std::to_string(crosshairFollowerID) + ")";
                g_MeridianView->ExecuteJavaScript(_view, script.c_str());
            } else {
                g_MeridianView->ExecuteJavaScript(_view, "hordeShowPanel()");
            }
            PushStateToView();
        } else {
            g_MeridianView->Hide(_view);
            if (focusResult != Meridian::UI::View::FocusResult::Busy) {
                logger::warn("HordeUI: Meridian focus request failed ({})", static_cast<std::uint32_t>(focusResult));
            }
        }
    }
}

void HordeUI::RefreshState()
{
    if (_isOpen) {
        PushStateToView();
    }
}

void HordeUI::PushStateToView()
{
    if (!g_MeridianView || !g_MeridianView->IsValid(_view)) return;

    auto json = BuildStateJSON();
    std::string script = "hordeUpdateState(" + json + ")";
    g_MeridianView->ExecuteJavaScript(_view, script.c_str());
}

std::string HordeUI::BuildStateJSON() const
{
    auto& mgr = FollowerManager::GetSingleton();
    auto& settings = Settings::GetSingleton();

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
    for (const auto& f : mgr.GetFollowers()) {
        nlohmann::json fj;
        to_json(fj, f);

        // Inject live actor values (not serialized to disk)
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
        if (actor) {
            auto* avo = actor->AsActorValueOwner();
            fj["health"] = avo->GetActorValue(RE::ActorValue::kHealth);
            fj["healthMax"] = avo->GetPermanentActorValue(RE::ActorValue::kHealth);
            fj["magicka"] = avo->GetActorValue(RE::ActorValue::kMagicka);
            fj["magickaMax"] = avo->GetPermanentActorValue(RE::ActorValue::kMagicka);
            fj["level"] = actor->GetLevel(); // refresh live level

            // Stamina
            fj["stamina"] = avo->GetActorValue(RE::ActorValue::kStamina);
            fj["staminaMax"] = avo->GetPermanentActorValue(RE::ActorValue::kStamina);

            // Race
            auto* race = actor->GetRace();
            fj["race"] = race ? race->GetFullName() : "Unknown";

            // Armor Rating (DamageResist AV)
            fj["armorRating"] = static_cast<int>(avo->GetActorValue(RE::ActorValue::kDamageResist));

            // Location — must be the follower's CURRENT location. GetEditorLocation1()
            // returns the static editor-assigned location, which never changes as
            // they travel (Lydia read "Whiterun" while standing in Blackreach).
            auto* loc = actor->GetCurrentLocation();
            fj["location"] = (loc && loc->GetFullName()[0]) ? loc->GetFullName() : "Wilderness";

            // Distance from player
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                auto d = actor->GetPosition().GetDistance(player->GetPosition());
                fj["distance"] = static_cast<int>(d / 71.0f);
            } else {
                fj["distance"] = 0;
            }

            // Combat skills
            nlohmann::json skills;
            skills["oneHanded"]    = static_cast<int>(avo->GetActorValue(RE::ActorValue::kOneHanded));
            skills["twoHanded"]    = static_cast<int>(avo->GetActorValue(RE::ActorValue::kTwoHanded));
            skills["archery"]      = static_cast<int>(avo->GetActorValue(RE::ActorValue::kArchery));
            skills["block"]        = static_cast<int>(avo->GetActorValue(RE::ActorValue::kBlock));
            skills["heavyArmor"]   = static_cast<int>(avo->GetActorValue(RE::ActorValue::kHeavyArmor));
            skills["lightArmor"]   = static_cast<int>(avo->GetActorValue(RE::ActorValue::kLightArmor));
            skills["destruction"]  = static_cast<int>(avo->GetActorValue(RE::ActorValue::kDestruction));
            skills["restoration"]  = static_cast<int>(avo->GetActorValue(RE::ActorValue::kRestoration));
            skills["conjuration"]  = static_cast<int>(avo->GetActorValue(RE::ActorValue::kConjuration));
            fj["skills"] = skills;

            // Perks (via ActorBase's BGSPerkRankArray)
            nlohmann::json perksArray = nlohmann::json::array();
            auto* actorBase = actor->GetActorBase();
            if (actorBase && actorBase->perkCount > 0 && actorBase->perks) {
                for (std::uint32_t i = 0; i < actorBase->perkCount; ++i) {
                    auto& entry = actorBase->perks[i];
                    if (entry.perk) {
                        const char* name = entry.perk->GetFullName();
                        if (name && name[0]) {
                            perksArray.push_back(name);
                        }
                    }
                }
            }
            fj["perks"] = perksArray;

            // Equipment
            nlohmann::json equip;

            auto weaponEntry = [](RE::TESForm* form) -> nlohmann::json {
                nlohmann::json e;
                if (!form) {
                    e["name"] = "None";
                    e["stat"] = 0;
                    e["isWeapon"] = false;
                    return e;
                }
                e["name"] = form->GetName();
                auto* weap = form->As<RE::TESObjectWEAP>();
                if (weap) {
                    e["stat"] = weap->GetAttackDamage();
                    e["isWeapon"] = true;
                } else {
                    auto* armor = form->As<RE::TESObjectARMO>();
                    e["stat"] = armor ? static_cast<int>(armor->GetArmorRating() / 100.0f) : 0;
                    e["isWeapon"] = false;
                }
                return e;
            };

            equip["rightHand"] = weaponEntry(actor->GetEquippedObject(false));
            equip["leftHand"]  = weaponEntry(actor->GetEquippedObject(true));

            auto armorEntry = [&](RE::BGSBipedObjectForm::BipedObjectSlot slot) -> nlohmann::json {
                nlohmann::json e;
                auto* armor = actor->GetWornArmor(slot);
                if (!armor) {
                    e["name"] = "None";
                    e["stat"] = 0;
                    return e;
                }
                e["name"] = armor->GetFullName();
                e["stat"] = static_cast<int>(armor->GetArmorRating() / 100.0f);
                return e;
            };

            equip["head"]  = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kHead);
            equip["body"]  = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
            equip["hands"] = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kHands);
            equip["feet"]  = armorEntry(RE::BGSBipedObjectForm::BipedObjectSlot::kFeet);
            fj["equipment"] = equip;
        }

        followersArray.push_back(fj);
    }
    j["followers"] = followersArray;

    return j.dump();
}

void HordeUI::PushDismissedToView()
{
    if (!g_MeridianView || !g_MeridianView->IsValid(_view)) return;

    auto json = BuildDismissedJSON();
    std::string script = "hordeUpdateDismissed(" + json + ")";
    g_MeridianView->ExecuteJavaScript(_view, script.c_str());
}

std::string HordeUI::BuildDismissedJSON() const
{
    auto dismissed = FollowerManager::GetSingleton().GetDismissedFollowers();

    nlohmann::json j;
    nlohmann::json arr = nlohmann::json::array();
    for (auto& d : dismissed) {
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
static RE::FormID ParseFormID(const char* data)
{
    try {
        auto j = nlohmann::json::parse(data);
        if (j.contains("formID")) {
            if (j["formID"].is_number()) {
                return j["formID"].get<RE::FormID>();
            }
            if (j["formID"].is_string()) {
                return static_cast<RE::FormID>(std::stoul(j["formID"].get<std::string>()));
            }
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

void HordeUI::RegisterListeners()
{
    if (!g_MeridianView) return;

    g_MeridianView->RegisterListener(_view, "hordeGetState", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    // Close from the in-UI close glyph / Escape key (mirrors the keybind toggle)
    g_MeridianView->RegisterListener(_view, "hordeClose", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            auto& ui = HordeUI::GetSingleton();
            if (ui.IsOpen()) {
                ui.Toggle();
            }
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSetFollow", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().SetFollow(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSetWait", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().SetWait(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSetPassive", [](const char* data) {
        try {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool passive = j.value("passive", false);
            SKSE::GetTaskInterface()->AddTask([id, passive]() {
                FollowerManager::GetSingleton().SetPassive(id, passive);
                HordeUI::GetSingleton().PushStateToView();
            });
        } catch (...) {}
    });

    g_MeridianView->RegisterListener(_view, "hordeSetSandbox", [](const char* data) {
        try {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool enabled = j.value("enabled", false);
            SKSE::GetTaskInterface()->AddTask([id, enabled]() {
                FollowerManager::GetSingleton().SetSandbox(id, enabled);
                HordeUI::GetSingleton().PushStateToView();
            });
        } catch (...) {}
    });

    g_MeridianView->RegisterListener(_view, "hordeSetFollowClose", [](const char* data) {
        try {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool enabled = j.value("enabled", false);
            SKSE::GetTaskInterface()->AddTask([id, enabled]() {
                FollowerManager::GetSingleton().SetFollowClose(id, enabled);
                HordeUI::GetSingleton().PushStateToView();
            });
        } catch (...) {}
    });

    g_MeridianView->RegisterListener(_view, "hordeSetEssential", [](const char* data) {
        try {
            auto id = ParseFormID(data);
            auto j = nlohmann::json::parse(data);
            bool essential = j.value("essential", true);
            SKSE::GetTaskInterface()->AddTask([id, essential]() {
                FollowerManager::GetSingleton().SetEssential(id, essential);
                HordeUI::GetSingleton().PushStateToView();
            });
        } catch (...) {}
    });

    g_MeridianView->RegisterListener(_view, "hordeSummon", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().Summon(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSummonAll", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().SummonAll();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeFollowAll", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().FollowAll();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeWaitAll", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().WaitAll();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    // Group stand-down toggle. The UI sends the state it wants; if "passive" is
    // absent we flip whatever the current group state is.
    g_MeridianView->RegisterListener(_view, "hordePassiveAll", [](const char* data) {
        bool explicitValue = false;
        bool passive = false;
        try {
            auto j = nlohmann::json::parse(data);
            if (j.contains("passive")) {
                passive = j["passive"].get<bool>();
                explicitValue = true;
            }
        } catch (...) {}

        SKSE::GetTaskInterface()->AddTask([passive, explicitValue]() {
            auto& mgr = FollowerManager::GetSingleton();
            mgr.SetPassiveAll(explicitValue ? passive : !mgr.IsAllPassive());
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeDismiss", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().UntrackFollower(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSetHome", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().SetHome(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeClearHome", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().ClearHome(id);
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeUpdateSettings", [](const char* data) {
        try {
            auto j = nlohmann::json::parse(data);
            auto& s = Settings::GetSingleton();
            if (j.contains("maxFollowers")) s.SetMaxFollowers(j["maxFollowers"].get<int>());
            if (j.contains("defaultSandboxEnabled")) s.SetDefaultSandboxEnabled(j["defaultSandboxEnabled"].get<bool>());
            if (j.contains("notificationsEnabled")) s.SetNotificationsEnabled(j["notificationsEnabled"].get<bool>());
            std::string distancePreset;
            if (j.contains("followDistance")) {
                distancePreset = j["followDistance"].get<std::string>();
                s.SetFollowDistance(distancePreset);
            }
            // ApplyFollowDistance mutates live TESPackage data and calls
            // EvaluatePackage on every follower, so it must run on the game
            // thread — this listener fires from Meridian's CEF callback thread.
            SKSE::GetTaskInterface()->AddTask([distancePreset]() {
                if (!distancePreset.empty()) {
                    PackageManager::GetSingleton().ApplyFollowDistance(distancePreset);
                }
                HordeUI::GetSingleton().PushStateToView();
            });
        } catch (...) {}
    });

    g_MeridianView->RegisterListener(_view, "hordeToggleInputMode", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            extern void ToggleInputMode();
            ToggleInputMode();
            HordeUI::GetSingleton().PushStateToView();
        });
    });

    // --- Dismissed follower listeners ---

    g_MeridianView->RegisterListener(_view, "hordeGetDismissed", [](const char*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSummonDismissed", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().SummonDismissed(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeSetDismissedHome", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().SetDismissedHome(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeClearDismissedHome", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().ClearDismissedHome(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    g_MeridianView->RegisterListener(_view, "hordeForgetFollower", [](const char* data) {
        auto id = ParseFormID(data);
        SKSE::GetTaskInterface()->AddTask([id]() {
            FollowerManager::GetSingleton().ForgetFollower(id);
            HordeUI::GetSingleton().PushDismissedToView();
        });
    });

    logger::info("HordeUI: {} listeners registered", 25);
}
