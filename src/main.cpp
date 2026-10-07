#include "pch.h"

#include "RuntimeCompatibility.h"
#include "Settings.h"
#include "follower/FollowerManager.h"
#include "events/EventHandler.h"
#include "events/HealthDamageHook.h"
#include "ui/HordeUI.h"
#include "ui/imgui/InputDispatchHook.h"
#include "keyhandler/keyhandler.h"

SKSE_PLUGIN_VERSION = Plugin::RuntimeCompatibility::MakePluginVersionData();

static_assert(
    (Plugin::RuntimeCompatibility::MakePluginVersionData().versionIndependence &
     SKSE::PluginVersionData::kVersionIndependent_AddressLibraryPostAE) != 0,
    "Horde must advertise Address Library runtime independence");

static_assert(
    (Plugin::RuntimeCompatibility::MakePluginVersionData().versionIndependenceEx &
     SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5) != 0,
    "Horde must advertise Address Library v5 compatibility for Skyrim 1.7.99 and newer");

static_assert(
    (Plugin::RuntimeCompatibility::MakePluginVersionData().versionIndependenceEx &
     SKSE::PluginVersionData::kVersionIndependentEx_NoStructUse) != 0,
    "Horde must advertise structure-layout independence for its SE/AE build");

static_assert(
    Plugin::RuntimeCompatibility::MakePluginVersionData().pluginVersion ==
        REL::Version{ 3, 0, 1, 0 }.pack(),
    "Horde plugin metadata must remain synchronized with version 3.0.1");

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* a_pluginInfo)
{
    a_pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
    a_pluginInfo->name = Plugin::NAME.data();
    a_pluginInfo->version = Plugin::VERSION.pack();
    return true;
}


// Menus can consume key releases. EventHandler resets these on menu transitions.
namespace {
    std::atomic<bool> g_modifierHeld{false};
    std::atomic<bool> g_groupModHeld{false};
}

void ResetKeybindModifiers()
{
    g_modifierHeld = false;
    g_groupModHeld = false;
}

// --- SKSE Cosave callbacks ---

static constexpr std::uint32_t kHordeID = 'HORD';

static void OnCosaveSave(SKSE::SerializationInterface* a_intfc)
{
    FollowerManager::GetSingleton().OnCosaveSave(a_intfc);
}

static void OnCosaveLoad(SKSE::SerializationInterface* a_intfc)
{
    FollowerManager::GetSingleton().OnCosaveLoad(a_intfc);
}

static void OnCosaveRevert(SKSE::SerializationInterface*)
{
    HordeUI::GetSingleton().Close();
    EventHandler::GetSingleton().SuspendSession();
    FollowerManager::GetSingleton().OnCosaveRevert();
}

// --- Messaging callbacks ---

static void OnDataLoaded()
{
    auto& settings = Settings::GetSingleton();
    settings.Load();

    FollowerManager::GetSingleton().Initialize();

    EventHandler::Register();
    EventHandler::GetSingleton().StartPolling();

    HordeUI::GetSingleton().Initialize();

    // Register Horde's configurable hotkeys.
    KeyHandler::RegisterSink();
    auto* kh = KeyHandler::GetSingleton();

    uint32_t modKey = settings.GetModifierKey();
    uint32_t actKey = settings.GetActivateKey();
    static bool useModifier = (modKey != 0);

    if (useModifier) {
        (void)kh->Register(modKey, KeyEventType::KEY_DOWN, []() { g_modifierHeld = true; });
        (void)kh->Register(modKey, KeyEventType::KEY_UP, []() { g_modifierHeld = false; });
    }

    (void)kh->Register(actKey, KeyEventType::KEY_DOWN, []() {
        if (!Settings::GetSingleton().GetUseKeybinds()) return;
        if (!useModifier || g_modifierHeld) {
            HordeUI::GetSingleton().Toggle();
        }
    });

    // Group command shortcuts (default: Alt+F/W/S/P)
    uint32_t groupMod   = settings.GetGroupModifierKey();
    uint32_t followKey  = settings.GetFollowAllKey();
    uint32_t waitKey    = settings.GetWaitAllKey();
    uint32_t summonKey  = settings.GetSummonAllKey();
    uint32_t passiveKey = settings.GetPassiveAllKey();
    static bool useGroupMod = (groupMod != 0);

    if (useGroupMod) {
        (void)kh->Register(groupMod, KeyEventType::KEY_DOWN, []() { g_groupModHeld = true; });
        (void)kh->Register(groupMod, KeyEventType::KEY_UP,   []() { g_groupModHeld = false; });
    }

    (void)kh->Register(followKey, KeyEventType::KEY_DOWN, []() {
        if (!Settings::GetSingleton().GetUseKeybinds()) return;
        if (useGroupMod && !g_groupModHeld) return;
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().FollowAll();
        });
    });

    (void)kh->Register(waitKey, KeyEventType::KEY_DOWN, []() {
        if (!Settings::GetSingleton().GetUseKeybinds()) return;
        if (useGroupMod && !g_groupModHeld) return;
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().WaitAll();
        });
    });

    (void)kh->Register(summonKey, KeyEventType::KEY_DOWN, []() {
        if (!Settings::GetSingleton().GetUseKeybinds()) return;
        if (useGroupMod && !g_groupModHeld) return;
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().SummonAll();
        });
    });

    // Toggles, mirroring the UI button and the Horde: Passive power.
    (void)kh->Register(passiveKey, KeyEventType::KEY_DOWN, []() {
        if (!Settings::GetSingleton().GetUseKeybinds()) return;
        if (useGroupMod && !g_groupModHeld) return;
        SKSE::GetTaskInterface()->AddTask([]() {
            auto& mgr = FollowerManager::GetSingleton();
            mgr.SetPassiveAll(!mgr.IsAllPassive());
        });
    });

    logger::info("Horde systems initialized");
}

static const char* kHordePowerEditorIDs[] = {
    "Horde_PowerUI", "Horde_PowerFollowAll",
    "Horde_PowerWaitAll", "Horde_PowerSummonAll",
    "Horde_PowerPassiveAll"
};

static void GrantHordePowers()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* magicFav = RE::MagicFavorites::GetSingleton();

    int found = 0;
    int missing = 0;

    for (auto* edid : kHordePowerEditorIDs) {
        auto* spell = RE::TESForm::LookupByEditorID<RE::SpellItem>(edid);
        if (!spell) {
            logger::warn("Horde: Spell record '{}' not found — is Horde.esp enabled in your load order?", edid);
            missing++;
            continue;
        }
        found++;

        if (!player->HasSpell(spell)) {
            player->AddSpell(spell);
            logger::info("Horde: Granted power {}", edid);
        }

        if (magicFav) {
            bool alreadyFav = false;
            for (auto* fav : magicFav->spells) {
                if (fav && fav->GetFormID() == spell->GetFormID()) {
                    alreadyFav = true;
                    break;
                }
            }
            if (!alreadyFav) {
                magicFav->SetFavorite(spell);
                logger::info("Horde: Added {} to favorites", edid);
            }
        }
    }

    if (found == 0) {
        logger::error("Horde: No power records resolved — Horde.esp is not loaded. Open MO2/Vortex and make sure Horde.esp is checked.");
        Settings::Notify("Horde: ESP not loaded — enable Horde.esp in your mod manager.");
    } else if (missing > 0) {
        logger::warn("Horde: Granted {}/{} powers; {} were missing", found, found + missing, missing);
    }
}

static void RemoveHordePowers()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* magicFav = RE::MagicFavorites::GetSingleton();

    for (auto* edid : kHordePowerEditorIDs) {
        auto* spell = RE::TESForm::LookupByEditorID<RE::SpellItem>(edid);
        if (!spell) continue;

        if (magicFav) magicFav->RemoveFavorite(spell);
        if (player->HasSpell(spell)) {
            player->RemoveSpell(spell);
            logger::info("Horde: Removed power {}", edid);
        }
    }
}

void ToggleInputMode()
{
    auto& settings = Settings::GetSingleton();
    bool switchToKeybinds = !settings.GetUseKeybinds();
    settings.SetUseKeybinds(switchToKeybinds);

    if (switchToKeybinds) {
        RemoveHordePowers();
        Settings::Notify("Horde: Keybinds enabled.");
    } else {
        GrantHordePowers();
        Settings::Notify("Horde: Favorites enabled.");
    }

    logger::info("Horde: Input mode set to {}", switchToKeybinds ? "keybinds" : "favorites");
}

static void OnPostLoadGame()
{
    const auto session = EventHandler::GetSingleton().Session();
    SKSE::GetTaskInterface()->AddTask([session]() {
        if (!EventHandler::GetSingleton().IsCurrentSession(session)) return;
        FollowerManager::GetSingleton().OnPostLoadGame();
        EventHandler::GetSingleton().ResumeSession();
        if (!Settings::GetSingleton().GetUseKeybinds()) {
            GrantHordePowers();
        } else {
            RemoveHordePowers();
        }
    });
}

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kPostPostLoad:
        Horde::ImGuiUI::InstallInputDispatchHook();
        Horde::InstallHealthDamageHook();
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        EventHandler::GetSingleton().SuspendSession();
        HordeUI::GetSingleton().Close();
        break;
    case SKSE::MessagingInterface::kDataLoaded:
        OnDataLoaded();
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
    case SKSE::MessagingInterface::kNewGame:
        OnPostLoadGame();
        break;
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();

    auto* g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(
        a_skse->QueryInterface(SKSE::LoadInterface::kMessaging)
    );

    if (!g_messaging) {
        logger::critical("Failed to load messaging interface! Plugin will not load.");
        return false;
    }

    logger::info("{} v{}"sv, Plugin::NAME, Plugin::VERSION.string());

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1 << 10);

    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

    // Register SKSE cosave for per-save-game follower persistence
    auto* serialization = reinterpret_cast<SKSE::SerializationInterface*>(
        a_skse->QueryInterface(SKSE::LoadInterface::kSerialization)
    );

    if (serialization) {
        serialization->SetUniqueID(kHordeID);
        serialization->SetSaveCallback(OnCosaveSave);
        serialization->SetLoadCallback(OnCosaveLoad);
        serialization->SetRevertCallback(OnCosaveRevert);
        logger::info("Horde: SKSE cosave registered");
    } else {
        logger::error("Horde: Failed to get serialization interface — follower data will not persist!");
    }

    return true;
}
