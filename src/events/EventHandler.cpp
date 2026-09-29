#include "pch.h"
#include "events/EventHandler.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "ui/HordeUI.h"
#include <thread>
#include <chrono>
#include <unordered_set>
#include <cmath>

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
    if (_polling.exchange(true)) return;

    // One worker polls every 5s, or every 250ms during a fast-scan window.
    std::thread([this]() {
        while (_polling.load()) {
            const auto session = Session();
            bool fast = _scanWindow.Read(NowMs()).active;
            std::this_thread::sleep_for(std::chrono::milliseconds(fast ? 250 : 5000));
            if (!_polling.load()) break;

            bool syncDialogue = _scanWindow.Read(NowMs()).dialogue;

            SKSE::GetTaskInterface()->AddTask([this, syncDialogue, session]() {
                if (!_sessionReady.load() || !IsCurrentSession(session)) return;
                auto& mgr = FollowerManager::GetSingleton();
                if (syncDialogue) {
                    mgr.OnDialogueClose();  // syncs vanilla wait/follow, then scans
                } else {
                    mgr.ScanForFollowers();
                }
                mgr.UpdateFollowCloseLeash();
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
    _scanWindow.Request(NowMs(), windowMs, syncDialogue);
}

void EventHandler::SuspendSession()
{
    _sessionReady.store(false);
    ++_session;
    _scanWindow.Reset();
    _standDownPending.store(false);
    _lastCellLoadScan.store(0);
}

void EventHandler::ResumeSession()
{
    _sessionReady.store(true);
}

bool EventHandler::ShouldBlockTeamDamage(RE::Actor* target, RE::Actor* attacker, float damage)
{
    if (!_sessionReady.load() || !target || !attacker || target == attacker ||
        !std::isfinite(damage) || damage >= 0.0f) return false;
    auto& manager = FollowerManager::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    const bool targetFollower = manager.IsTracked(target->GetFormID());
    const bool attackerFollower = manager.IsTracked(attacker->GetFormID());
    const bool block = (targetFollower && (attackerFollower || attacker == player)) ||
        (target == player && attackerFollower);
    if (block) RequestTeamStandDown();
    return block;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!event) return RE::BSEventNotifyControl::kContinue;

    // Menu transitions may consume the release of a held modifier.
    {
        extern void ResetKeybindModifiers();
        ResetKeybindModifiers();
    }

    if (event->menuName == RE::MainMenu::MENU_NAME && event->opening) SuspendSession();

    // Papyrus recruitment may finish after dialogue closes; scan quickly for 6s.
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
            // Scan across the conversation window to catch delayed SetFollower calls.
            RequestFastScan(6000, false);
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl EventHandler::ProcessEvent(
    const RE::TESCellFullyLoadedEvent*,
    RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*)
{
    // Coalesce cell-load events into one scan per 2s.
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = _lastCellLoadScan.load();
    if (now - last < 2000) {
        return RE::BSEventNotifyControl::kContinue;
    }
    _lastCellLoadScan.store(now);

    // Let the cell settle before scanning and recovering stranded followers.
    const auto session = Session();
    std::thread([this, session]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        SKSE::GetTaskInterface()->AddTask([this, session]() {
            if (!_sessionReady.load() || !IsCurrentSession(session)) return;
            auto& mgr = FollowerManager::GetSingleton();

            // Recover stranded followers regardless of their selected package.
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
    if (!event || !_sessionReady.load()) return RE::BSEventNotifyControl::kContinue;
    auto target = event->target.get();
    auto aggressor = event->cause.get();
    // Damage is filtered before application. Hit events only request deferred
    // combat cleanup; sampled health cannot attribute damage to this hit.
    if (target && aggressor) {
        ShouldBlockTeamDamage(target->As<RE::Actor>(), aggressor->As<RE::Actor>(), -1.0f);
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

    // End friendly combat even when the follower is not passive.
    bool shouldStop =
        (actorIsFollower && targetIsFollower) ||
        (actorIsFollower && targetIsPlayer);

    // Passive followers also stand down when attacked by enemies.
    bool passiveStandDown = actorIsFollower && mgr.IsPassive(actor->GetFormID());

    if (!shouldStop && !passiveStandDown) return RE::BSEventNotifyControl::kContinue;

    // StopCombat/EvaluatePackage can re-enter this event. Defer changes to a
    // coalesced game-thread task to avoid recursive combat dispatch.
    RequestTeamStandDown();

    return RE::BSEventNotifyControl::kContinue;
}

void EventHandler::RequestTeamStandDown()
{
    if (!_sessionReady.load()) return;
    // exchange() collapses any number of combat events into one queued pass.
    if (_standDownPending.exchange(true)) return;

    const auto session = Session();
    SKSE::GetTaskInterface()->AddTask([this, session]() {
        if (!_sessionReady.load() || !IsCurrentSession(session)) return;
        RunTeamStandDown();
    });
}

void EventHandler::RunTeamStandDown()
{
    _standDownPending.store(false);
    if (!_sessionReady.load()) return;
    auto& mgr = FollowerManager::GetSingleton();
    const auto followers = mgr.GetFollowers();
    auto* player = RE::PlayerCharacter::GetSingleton();
    for (const auto& follower : followers) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(follower.actorFormID);
        if (!actor || actor->IsDead() || !actor->IsInCombat()) continue;
        auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
        const bool teammate = target && (target.get() == player || mgr.IsTracked(target->GetFormID()));
        if (!follower.isPassive && !teammate) continue;
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
