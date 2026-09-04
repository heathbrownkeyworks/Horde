#include "pch.h"
#include "events/EventHandler.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "ui/HordeUI.h"
#include <thread>
#include <chrono>
#include <unordered_set>

EventHandler& EventHandler::GetSingleton()
{
    static EventHandler singleton;
    return singleton;
}

void EventHandler::Register()
{
    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&GetSingleton());
    }

    auto* sourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (sourceHolder) {
        sourceHolder->AddEventSink<RE::TESActivateEvent>(&GetSingleton());
        sourceHolder->AddEventSink<RE::TESCellFullyLoadedEvent>(&GetSingleton());
        sourceHolder->AddEventSink<RE::TESHitEvent>(&GetSingleton());
        sourceHolder->AddEventSink<RE::TESCombatEvent>(&GetSingleton());
        sourceHolder->AddEventSink<RE::TESSpellCastEvent>(&GetSingleton());
    }

    logger::info("EventHandler registered");
}

static int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void EventHandler::StartPolling()
{
    _polling.store(true);

    // Single long-lived worker. It normally ticks every 5s, but drops to a 250ms
    // cadence while a fast-scan window is open. This replaces the old pattern of
    // spawning 4 detached threads per NPC activation and 8 per dialogue close,
    // which produced dozens of short-lived threads in a busy town.
    std::thread([this]() {
        while (_polling.load()) {
            bool fast = NowMs() < _fastScanUntil.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(fast ? 250 : 5000));
            if (!_polling.load()) break;

            bool syncDialogue = NowMs() < _fastScanUntil.load() && _fastScanSyncDialogue.load();

            SKSE::GetTaskInterface()->AddTask([syncDialogue]() {
                auto& mgr = FollowerManager::GetSingleton();
                if (syncDialogue) {
                    mgr.OnDialogueClose();  // syncs vanilla wait/follow, then scans
                } else {
                    mgr.ScanForFollowers();
                }
                mgr.UpdateFollowCloseLeash();
                PackageManager::GetSingleton().UpdateIdleSandbox();
                EventHandler::GetSingleton().RefreshHealthBaselines();
                EventHandler::GetSingleton().RunTeamStandDown();
            });
        }
    }).detach();

    logger::info("EventHandler polling started (5s idle / 250ms burst)");
}

void EventHandler::StopPolling()
{
    _polling.store(false);
}

void EventHandler::RequestFastScan(int windowMs, bool syncDialogue)
{
    auto deadline = NowMs() + windowMs;

    // Extend an in-flight window rather than restarting it.
    int64_t current = _fastScanUntil.load();
    while (deadline > current && !_fastScanUntil.compare_exchange_weak(current, deadline)) {
        // current is refreshed by compare_exchange_weak on failure
    }

    if (syncDialogue) {
        _fastScanSyncDialogue.store(true);
    } else if (NowMs() >= _fastScanUntil.load()) {
        _fastScanSyncDialogue.store(false);
    }
}

// --- Friendly-fire health baseline ---

void EventHandler::RefreshHealthBaseline(RE::Actor* actor)
{
    if (!actor) return;
    auto* avo = actor->AsActorValueOwner();
    if (!avo) return;

    std::lock_guard<std::mutex> lock(_healthBaselineMutex);
    _healthBaseline[actor->GetFormID()] = avo->GetActorValue(RE::ActorValue::kHealth);
}

void EventHandler::RefreshHealthBaselines()
{
    auto& mgr = FollowerManager::GetSingleton();

    std::unordered_set<RE::FormID> live;

    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        RefreshHealthBaseline(player);
        live.insert(player->GetFormID());
    }

    for (const auto& f : mgr.GetFollowers()) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
        if (!actor) continue;
        RefreshHealthBaseline(actor);
        live.insert(f.actorFormID);
    }

    // Drop entries for actors that are no longer part of the team.
    std::lock_guard<std::mutex> lock(_healthBaselineMutex);
    for (auto it = _healthBaseline.begin(); it != _healthBaseline.end();) {
        it = live.count(it->first) ? std::next(it) : _healthBaseline.erase(it);
    }
}

float EventHandler::ConsumeHealthDeficit(RE::Actor* actor)
{
    if (!actor) return 0.0f;
    auto* avo = actor->AsActorValueOwner();
    if (!avo) return 0.0f;

    float current = avo->GetActorValue(RE::ActorValue::kHealth);

    std::lock_guard<std::mutex> lock(_healthBaselineMutex);
    auto it = _healthBaseline.find(actor->GetFormID());
    if (it == _healthBaseline.end()) {
        // First time we've seen this actor — seed the baseline, restore nothing.
        _healthBaseline[actor->GetFormID()] = current;
        return 0.0f;
    }

    float deficit = it->second - current;
    if (deficit < 0.0f) {
        // Healed above the baseline since the last sample.
        it->second = current;
        return 0.0f;
    }
    return deficit;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!event) return RE::BSEventNotifyControl::kContinue;

    // Modifier desync defense: any time a menu opens or closes, reset
    // Horde's keybind modifier state. Modal menus (OStim, SexLab, console,
    // freecam UIs, even our own Meridian view) can swallow KEY_UP for the
    // modifier, leaving it stuck and bypassing the modifier requirement.
    {
        extern void ResetKeybindModifiers();
        ResetKeybindModifiers();
    }

    // On dialogue menu close, check for follower changes.
    // The recruit fragment's SetFollower can take a couple of seconds to execute
    // under Papyrus load, so run the poll at burst cadence for ~6s to catch the
    // new follower shortly after she enters CurrentFollowerFaction.
    if (event->menuName == RE::DialogueMenu::MENU_NAME && !event->opening) {
        RequestFastScan(6000, true);
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::TESActivateEvent* event,
    RE::BSTEventSource<RE::TESActivateEvent>*)
{
    if (!event) return RE::BSEventNotifyControl::kContinue;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return RE::BSEventNotifyControl::kContinue;

    if (event->actionRef.get() == player) {
        auto* target = event->objectActivated.get();
        auto* actor = target ? target->As<RE::Actor>() : nullptr;

        if (actor) {
            // Recruitment fires SetFollower mid/post-dialogue, so run the poll at
            // burst cadence across the conversation window. Vanilla Dismiss/Wait/
            // Follow options are hidden per-actor by the INFO conditions shipped
            // in Horde.esp — no runtime branch-flag mutation is needed.
            RequestFastScan(6000, false);
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::TESCellFullyLoadedEvent*,
    RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*)
{
    // Debounce: during save load, dozens of cells fire this event simultaneously.
    // Only allow one scan per 2 seconds to avoid exponential log spam.
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = _lastCellLoadScan.load();
    if (now - last < 2000) {
        return RE::BSEventNotifyControl::kContinue;
    }
    _lastCellLoadScan.store(now);

    // On cell load, re-scan and teleport stranded followers.
    // Use a short delay — the cell may still be settling when this fires.
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        SKSE::GetTaskInterface()->AddTask([]() {
            auto& mgr = FollowerManager::GetSingleton();

            // Drop sandbox BEFORE looking for stranded followers. A follower who
            // wandered away from the player misses the engine's teammate hand-off
            // at a load door, and TeleportStrandedFollowers used to skip exactly
            // those actors — so they were left behind with no recovery path until
            // the next cell load, by which time the poll had already cleared the
            // sandbox flag they were being skipped on.
            PackageManager::GetSingleton().SuspendSandbox();

            mgr.ScanForFollowers();
            mgr.UpdateDialogueGate();
            mgr.TeleportStrandedFollowers();
        });
    }).detach();

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::TESHitEvent* event,
    RE::BSTEventSource<RE::TESHitEvent>*)
{
    if (!event) return RE::BSEventNotifyControl::kContinue;

    auto* target    = event->target.get() ? event->target.get()->As<RE::Actor>() : nullptr;
    auto* aggressor = event->cause.get()  ? event->cause.get()->As<RE::Actor>()  : nullptr;

    if (!target || !aggressor || target == aggressor) return RE::BSEventNotifyControl::kContinue;

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto& mgr    = FollowerManager::GetSingleton();

    bool targetIsFollower    = mgr.IsTracked(target->GetFormID());
    bool targetIsPlayer      = (target == player);
    bool aggressorIsFollower = mgr.IsTracked(aggressor->GetFormID());
    bool aggressorIsPlayer   = (aggressor == player);

    // Only actors on the Horde team carry a health baseline.
    if (!targetIsFollower && !targetIsPlayer) return RE::BSEventNotifyControl::kContinue;

    // Block damage within the Horde team:
    //   follower → follower, follower → player, player → follower
    bool shouldBlock =
        (aggressorIsFollower && targetIsFollower) ||
        (aggressorIsFollower && targetIsPlayer)   ||
        (aggressorIsPlayer   && targetIsFollower);

    if (!shouldBlock) {
        // Hostile hit — re-baseline so the damage it dealt is never refunded by
        // a later friendly hit. Without this, any friendly graze would top the
        // target back up to full, which let a follower's stray arrow act as an
        // unlimited heal for the player.
        RefreshHealthBaseline(target);
        return RE::BSEventNotifyControl::kContinue;
    }

    // Undo only the damage this hit actually dealt, measured against the last
    // known-good health for this actor.
    float deficit = ConsumeHealthDeficit(target);
    if (deficit > 0.0f) {
        auto* avo = target->AsActorValueOwner();
        avo->RestoreActorValue(RE::ActorValue::kHealth, deficit);
    }
    RefreshHealthBaseline(target);

    // Clear alarm state on both sides so neither reacts with hostility
    target->StopAlarmOnActor();
    if (aggressor != player) {
        aggressor->StopAlarmOnActor();
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::TESCombatEvent* event,
    RE::BSTEventSource<RE::TESCombatEvent>*)
{
    if (!event) return RE::BSEventNotifyControl::kContinue;

    // Only care about entering combat (state 1)
    if (event->newState.underlying() != 1) return RE::BSEventNotifyControl::kContinue;

    auto* actor  = event->actor.get()       ? event->actor.get()->As<RE::Actor>()       : nullptr;
    auto* target = event->targetActor.get() ? event->targetActor.get()->As<RE::Actor>() : nullptr;

    if (!actor || !target) return RE::BSEventNotifyControl::kContinue;

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto& mgr    = FollowerManager::GetSingleton();

    bool actorIsFollower  = mgr.IsTracked(actor->GetFormID());
    bool targetIsFollower = mgr.IsTracked(target->GetFormID());
    bool targetIsPlayer   = (target == player);

    // Stop a tracked follower from entering combat with a team member
    // This prevents all damage types (melee, ranged, magic) proactively
    bool shouldStop =
        (actorIsFollower && targetIsFollower) ||
        (actorIsFollower && targetIsPlayer);

    // A passive follower stands down from every fight, not just friendly ones.
    // Aggression 0 alone stops them starting a fight but not being pulled into
    // one, so without this the group Passive toggle would appear to do nothing
    // the moment an enemy swung at them.
    bool passiveStandDown = actorIsFollower && mgr.IsPassive(actor->GetFormID());

    if (!shouldStop && !passiveStandDown) return RE::BSEventNotifyControl::kContinue;

    // CRITICAL: never mutate combat or package state from inside combat event
    // dispatch. StopCombat() here makes the attacker's combat controller
    // re-acquire the follower immediately, which re-fires this same event on the
    // same stack. With the old friendly-fire-only condition that was rare enough
    // to go unnoticed; once passive followers matched it on every combat entry it
    // recursed until the stack blew. A stack overflow unwinds through the crash
    // handler's own guard page, which is why no crash log was produced.
    //
    // Queue the stand-down onto the game thread instead and coalesce a storm of
    // combat events into a single pass.
    RequestTeamStandDown();

    return RE::BSEventNotifyControl::kContinue;
}

void EventHandler::RequestTeamStandDown()
{
    // exchange() collapses any number of combat events into one queued pass.
    if (_standDownPending.exchange(true)) return;

    SKSE::GetTaskInterface()->AddTask([]() {
        EventHandler::GetSingleton().RunTeamStandDown();
    });
}

void EventHandler::RunTeamStandDown()
{
    _standDownPending.store(false);

    auto& mgr = FollowerManager::GetSingleton();

    // Snapshot first — StopCombat can re-enter event sinks, and we must not be
    // walking FollowerManager's vector when that happens.
    std::vector<RE::FormID> passiveIDs;
    for (const auto& f : mgr.GetFollowers()) {
        if (f.isPassive) passiveIDs.push_back(f.actorFormID);
    }

    for (auto id : passiveIDs) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor || actor->IsDead()) continue;
        if (!actor->IsInCombat()) continue;

        actor->StopCombat();
        actor->StopAlarmOnActor();
    }
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::TESSpellCastEvent* event,
    RE::BSTEventSource<RE::TESSpellCastEvent>*)
{
    if (!event) return RE::BSEventNotifyControl::kContinue;

    auto* caster = event->object.get();
    if (!caster || caster != RE::PlayerCharacter::GetSingleton()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Match against our Horde power FormIDs
    auto* powerUI      = RE::TESForm::LookupByEditorID<RE::SpellItem>("Horde_PowerUI");
    auto* powerFollow  = RE::TESForm::LookupByEditorID<RE::SpellItem>("Horde_PowerFollowAll");
    auto* powerWait    = RE::TESForm::LookupByEditorID<RE::SpellItem>("Horde_PowerWaitAll");
    auto* powerSummon  = RE::TESForm::LookupByEditorID<RE::SpellItem>("Horde_PowerSummonAll");
    auto* powerPassive = RE::TESForm::LookupByEditorID<RE::SpellItem>("Horde_PowerPassiveAll");

    RE::FormID castID = event->spell;

    if (powerUI && castID == powerUI->GetFormID()) {
        SKSE::GetTaskInterface()->AddTask([]() {
            HordeUI::GetSingleton().Toggle();
        });
    } else if (powerFollow && castID == powerFollow->GetFormID()) {
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().FollowAll();
        });
    } else if (powerWait && castID == powerWait->GetFormID()) {
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().WaitAll();
        });
    } else if (powerSummon && castID == powerSummon->GetFormID()) {
        SKSE::GetTaskInterface()->AddTask([]() {
            FollowerManager::GetSingleton().SummonAll();
        });
    } else if (powerPassive && castID == powerPassive->GetFormID()) {
        // Toggle, mirroring the UI button: passive if any follower is still
        // aggressive, otherwise back to aggressive.
        SKSE::GetTaskInterface()->AddTask([]() {
            auto& mgr = FollowerManager::GetSingleton();
            mgr.SetPassiveAll(!mgr.IsAllPassive());
        });
    }

    return RE::BSEventNotifyControl::kContinue;
}
