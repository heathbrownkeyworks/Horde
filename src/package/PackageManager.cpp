#include "pch.h"
#include "package/PackageManager.h"
#include "follower/FollowerManager.h"
#include "Settings.h"

PackageManager& PackageManager::GetSingleton()
{
    static PackageManager singleton;
    return singleton;
}

// --- Quest / Alias Access ---

void PackageManager::InitQuestCache()
{
    _hordeQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("Horde_FollowerQuest");
    _questCached = true;
    if (_hordeQuest) {
        logger::info("PackageManager: Cached Horde_FollowerQuest {:08X}", _hordeQuest->GetFormID());
    } else {
        logger::error("PackageManager: Horde_FollowerQuest not found — is Horde.esp loaded?");
    }
}

RE::TESQuest* PackageManager::GetHordeQuest()
{
    if (!_questCached) {
        InitQuestCache();
    }
    return _hordeQuest;
}

RE::BGSBaseAlias* PackageManager::GetAlias(int slot)
{
    auto* quest = GetHordeQuest();
    if (!quest || slot < 0 || slot >= kMaxSlots) return nullptr;

    for (auto& alias : quest->aliases) {
        if (alias && alias->aliasID == static_cast<std::uint32_t>(slot)) {
            return alias;
        }
    }
    return nullptr;
}

int PackageManager::FindEmptySlot()
{
    auto* quest = GetHordeQuest();
    if (!quest) return -1;

    for (int i = 0; i < kMaxSlots; ++i) {
        auto* alias = GetAlias(i);
        if (!alias) continue;

        auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias);
        if (!refAlias) continue;

        auto ref = refAlias->GetReference();
        if (!ref) {
            return i;
        }
    }
    return -1;
}

void PackageManager::FillSlot(int slot, RE::Actor* actor)
{
    auto* quest = GetHordeQuest();
    if (!quest || !actor || slot < 0 || slot >= kMaxSlots) return;

    auto* alias = GetAlias(slot);
    if (!alias) {
        logger::error("PackageManager: Alias slot {} not found", slot);
        return;
    }

    auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias);
    if (!refAlias) {
        logger::error("PackageManager: Alias slot {} is not a ref alias", slot);
        return;
    }

    // Equivalent of Papyrus ForceRefTo:
    // 1. Set fill type to Forced and store the actor handle
    // 2. Update the quest's refAliasMap so GetIsAliasRef() works
    // 3. The engine's package evaluator reads refAliasMap to resolve alias packages
    refAlias->fillType = RE::BGSBaseAlias::FILL_TYPE::kForced;
    refAlias->fillData.forced.forcedRef = actor->GetHandle();

    {
        RE::BSWriteLockGuard questLock(quest->aliasAccessLock);
        quest->refAliasMap.erase(alias->aliasID);  // remove old entry if present
        quest->refAliasMap.insert({alias->aliasID, actor->GetHandle()});
    }

    // Inject the alias instance into the actor's ExtraAliasInstanceArray
    // so the engine evaluates our alias packages on this actor
    auto* aliasExtra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!aliasExtra) {
        auto* newExtra = new RE::ExtraAliasInstanceArray();
        actor->extraList.Add(newExtra);
        aliasExtra = newExtra;
    }

    // Check if already has our alias instance
    bool alreadyHas = false;
    {
        RE::BSReadLockGuard readLock(aliasExtra->lock);
        for (auto* inst : aliasExtra->aliases) {
            if (inst && inst->quest == quest && inst->alias == refAlias) {
                alreadyHas = true;
                break;
            }
        }
    }

    if (!alreadyHas) {
        // Build the instanced packages array from our ESP-defined packages.
        // The engine does NOT auto-populate this from alias PackageData when we
        // manually inject the alias instance — we must provide it.
        auto* pkgArray = new RE::BSTArray<RE::TESPackage*>();
        auto* waitPkg    = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_WaitPkg");
        auto* followPkg  = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_FollowPkg");
        if (waitPkg)    pkgArray->push_back(waitPkg);
        if (followPkg)  pkgArray->push_back(followPkg);

        // Allocate alias instance via engine allocator
        auto* instData = static_cast<RE::BGSRefAliasInstanceData*>(
            RE::malloc(sizeof(RE::BGSRefAliasInstanceData)));
        if (instData) {
            std::memset(instData, 0, sizeof(RE::BGSRefAliasInstanceData));
            instData->quest = quest;
            instData->alias = refAlias;
            instData->instancedPackages = pkgArray;

            RE::BSWriteLockGuard writeLock(aliasExtra->lock);
            aliasExtra->aliases.push_back(instData);
        } else {
            delete pkgArray;
        }
    }

    // Clear any stale ExtraPackage — it overrides alias packages
    actor->extraList.RemoveByType(RE::ExtraDataType::kPackage);

    logger::info("PackageManager: Filled slot {} with {} [{:08X}]",
        slot, actor->GetDisplayFullName(), actor->GetFormID());
}

void PackageManager::ClearSlot(int slot)
{
    auto* quest = GetHordeQuest();
    if (!quest || slot < 0 || slot >= kMaxSlots) return;

    auto* alias = GetAlias(slot);
    if (!alias) return;

    auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias);
    if (!refAlias) return;

    auto ref = refAlias->GetReference();
    RE::FormID fid = ref ? ref->GetFormID() : 0;

    // Remove from quest's refAliasMap
    {
        RE::BSWriteLockGuard questLock(quest->aliasAccessLock);
        quest->refAliasMap.erase(alias->aliasID);
    }

    // Remove alias instance from the actor's ExtraAliasInstanceArray
    if (ref) {
        auto* aliasExtra = ref->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (aliasExtra) {
            RE::BSWriteLockGuard writeLock(aliasExtra->lock);
            auto& arr = aliasExtra->aliases;
            for (std::int32_t i = static_cast<std::int32_t>(arr.size()) - 1; i >= 0; --i) {
                auto* inst = arr[i];
                if (inst && inst->quest == quest && inst->alias == refAlias) {
                    delete inst->instancedPackages;
                    RE::free(inst);
                    arr.erase(arr.begin() + i);
                    break;
                }
            }
        }
    }

    // Clear the forced ref
    refAlias->fillData.forced.forcedRef.reset();

    logger::info("PackageManager: Cleared slot {} (was {:08X})", slot, fid);
}

bool PackageManager::IsSlotFilled(int slot)
{
    auto* quest = GetHordeQuest();
    if (!quest || slot < 0 || slot >= kMaxSlots) return false;

    auto* alias = GetAlias(slot);
    if (!alias) return false;

    auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias);
    if (!refAlias) return false;

    return refAlias->GetReference() != nullptr;
}

// --- DialogueFollower ---

bool PackageManager::HasDialogueFollowerAlias(RE::Actor* actor) const
{
    if (!actor) return false;

    auto* aliasExtra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!aliasExtra) return false;

    auto* dfQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DialogueFollower");
    if (!dfQuest) return false;

    RE::BSReadLockGuard readLock(aliasExtra->lock);
    for (auto* inst : aliasExtra->aliases) {
        if (!inst) continue;
        if (inst->quest == dfQuest && inst->instancedPackages && !inst->instancedPackages->empty()) {
            return true;
        }
    }
    return false;
}

void PackageManager::ClearDialogueFollowerAlias(RE::Actor* actor)
{
    if (!actor) return;

    auto* aliasExtra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!aliasExtra) return;

    auto* dfQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DialogueFollower");
    if (!dfQuest) return;

    // Phase 1: Under aliasExtra->lock, collect and remove DialogueFollower instances
    struct RemovedAlias {
        RE::TESQuest* quest;
        std::uint32_t aliasID;
    };
    std::vector<RemovedAlias> removed;

    {
        RE::BSWriteLockGuard writeLock(aliasExtra->lock);
        auto& arr = aliasExtra->aliases;

        for (std::int32_t i = static_cast<std::int32_t>(arr.size()) - 1; i >= 0; --i) {
            auto* inst = arr[i];
            if (!inst || inst->quest != dfQuest) continue;

            if (inst->alias) {
                removed.push_back({inst->quest, inst->alias->aliasID});
            }
            arr.erase(arr.begin() + i);
        }
    }

    // Phase 2: Update refAliasMaps (no nested locks)
    for (auto& r : removed) {
        if (r.quest) {
            RE::BSWriteLockGuard questLock(r.quest->aliasAccessLock);
            r.quest->refAliasMap.erase(r.aliasID);
        }
    }

    if (!removed.empty()) {
        logger::info("PackageManager: Cleared {} DialogueFollower alias(es) from {:08X}",
            removed.size(), actor->GetFormID());
    }
}

void PackageManager::ClearCompetingAliasPackages(RE::Actor* actor)
{
    ClearDialogueFollowerAlias(actor);
}

// --- Rebuild alias instance (the engine caches instanced packages on creation) ---

void PackageManager::RebuildHordeAliasInstance(RE::Actor* actor, bool includeSandboxActive)
{
    auto* quest = GetHordeQuest();
    if (!quest || !actor) return;

    auto* aliasExtra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!aliasExtra) {
        logger::warn("PackageManager: Rebuild — no ExtraAliasInstanceArray on {:08X}",
            actor->GetFormID());
        return;
    }

    // Phase 1: Find and remove the existing Horde alias instance
    const RE::BGSBaseAlias* hordeAlias = nullptr;
    {
        RE::BSWriteLockGuard writeLock(aliasExtra->lock);
        auto& arr = aliasExtra->aliases;
        for (std::int32_t i = static_cast<std::int32_t>(arr.size()) - 1; i >= 0; --i) {
            auto* inst = arr[i];
            if (!inst || inst->quest != quest || !inst->alias) continue;

            hordeAlias = inst->alias;
            delete inst->instancedPackages;
            RE::free(inst);
            arr.erase(arr.begin() + i);
            break;
        }
    }

    if (!hordeAlias) {
        logger::warn("PackageManager: Rebuild — no Horde alias found on {:08X}",
            actor->GetFormID());
        return;
    }

    // Phase 2: Build new package array
    auto* pkgArray = new RE::BSTArray<RE::TESPackage*>();

    if (includeSandboxActive) {
        auto* sandboxActivePkg = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_SandboxActivePkg");
        if (sandboxActivePkg) {
            pkgArray->push_back(sandboxActivePkg);
        } else {
            logger::error("PackageManager: Horde_SandboxActivePkg not found — is Horde.esp loaded?");
        }
    }

    auto* waitPkg    = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_WaitPkg");
    auto* followPkg  = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_FollowPkg");
    if (waitPkg)    pkgArray->push_back(waitPkg);
    if (followPkg)  pkgArray->push_back(followPkg);

    logger::info("PackageManager: Rebuild — {} packages for {:08X} (sandbox={})",
        pkgArray->size(), actor->GetFormID(), includeSandboxActive);

    // Phase 3: Create fresh alias instance
    auto* instData = static_cast<RE::BGSRefAliasInstanceData*>(
        RE::malloc(sizeof(RE::BGSRefAliasInstanceData)));
    if (!instData) {
        delete pkgArray;
        logger::error("PackageManager: Rebuild — malloc failed for alias instance");
        return;
    }

    std::memset(instData, 0, sizeof(RE::BGSRefAliasInstanceData));
    instData->quest = quest;
    instData->alias = hordeAlias;
    instData->instancedPackages = pkgArray;

    {
        RE::BSWriteLockGuard writeLock(aliasExtra->lock);
        aliasExtra->aliases.push_back(instData);
    }

    // Phase 4: Re-insert refAliasMap so the engine still sees this actor as alias-bound.
    // Without this, the package evaluator may skip our alias's packages because
    // GetIsAliasRef() returns false.
    {
        RE::BSWriteLockGuard questLock(quest->aliasAccessLock);
        quest->refAliasMap.erase(hordeAlias->aliasID);
        quest->refAliasMap.insert({hordeAlias->aliasID, actor->GetHandle()});
    }

    // Phase 5: Clear stale package execution state and force re-evaluation.
    // ExtraPackage holds the currently-running package index/state — must be cleared.
    // ExtraPackageStartLocation holds the stale start position from the old package.
    actor->extraList.RemoveByType(RE::ExtraDataType::kPackage);
    actor->extraList.RemoveByType(RE::ExtraDataType::kPackageStartLocation);
    actor->EvaluatePackage(true, false);

    logger::info("PackageManager: Rebuild complete for {:08X}", actor->GetFormID());
}

// --- Sandbox ---

void PackageManager::ApplySandbox(RE::Actor* actor, bool silent)
{
    if (!actor) return;

    RE::FormID formID = actor->GetFormID();
    if (_sandboxActors.count(formID)) return;

    if (FollowerManager::GetSingleton().IsWaiting(formID)) {
        logger::info("PackageManager: Skipped active sandbox for waiting follower {:08X}; Horde_WaitPkg handles wait sandbox",
            formID);
        return;
    }

    // Move the shared idle sandbox marker to the sandbox center.
    // Waiting followers never use this package; Horde_WaitPkg sandboxes them
    // around their current position without a shared marker.
    auto* idleMarker = RE::TESForm::LookupByEditorID<RE::TESObjectREFR>("Horde_IdleSandboxMarker");
    if (idleMarker && _sandboxActors.empty()) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            idleMarker->MoveTo(player);
            logger::info("PackageManager: Moved idle sandbox marker to player position");
        }
    }

    // Clear vanilla DialogueFollower only. Third-party quest aliases may carry
    // unrelated packages and should remain intact.
    ClearCompetingAliasPackages(actor);

    RebuildHordeAliasInstance(actor, true);
    _sandboxActors.insert(formID);

    if (!silent) {
        auto msg = std::string(actor->GetDisplayFullName()) + " is now sandboxing.";
        Settings::Notify(msg.c_str());
    }

    logger::info("PackageManager: Sandbox applied to {} [{:08X}]",
        actor->GetDisplayFullName(), formID);
}

void PackageManager::RemoveSandbox(RE::Actor* actor, bool silent)
{
    if (!actor) return;

    RE::FormID formID = actor->GetFormID();
    auto it = _sandboxActors.find(formID);
    if (it == _sandboxActors.end()) return;

    RebuildHordeAliasInstance(actor, false);
    _sandboxActors.erase(it);

    if (!silent) {
        auto msg = std::string(actor->GetDisplayFullName()) + " stopped sandboxing.";
        Settings::Notify(msg.c_str());
    }

    logger::info("PackageManager: Sandbox removed for {} [{:08X}]",
        actor->GetDisplayFullName(), formID);
}

bool PackageManager::HasSandbox(RE::Actor* actor) const
{
    if (!actor) return false;
    return _sandboxActors.count(actor->GetFormID()) > 0;
}

void PackageManager::ForgetSandboxActor(RE::FormID formID)
{
    if (_sandboxActors.erase(formID) > 0) {
        logger::info("PackageManager: Dropped sandbox bookkeeping for {:08X}", formID);
    }
}

void PackageManager::SuspendSandbox()
{
    if (_sandboxActors.empty() && !_followersAreSandboxing) return;

    // Snapshot first — RemoveSandbox mutates _sandboxActors.
    std::vector<RE::FormID> active(_sandboxActors.begin(), _sandboxActors.end());
    for (auto id : active) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (actor) {
            RemoveSandbox(actor, true);
        } else {
            // Actor no longer resolvable — drop the entry so the shared idle
            // marker is not pinned forever by a dead FormID.
            ForgetSandboxActor(id);
        }
    }

    _sandboxActors.clear();
    _followersAreSandboxing = false;
    _idleTimer = 0.0f;
    _idleTickValid = false;

    logger::info("PackageManager: Sandbox suspended ({} actor(s) released)", active.size());
}

void PackageManager::ClearDismissedSandbox(RE::Actor* actor)
{
    if (!actor) return;

    auto* quest = GetHordeQuest();
    if (!quest) return;

    auto* aliasExtra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!aliasExtra) return;

    // Dismissed sandbox instances have quest = Horde_FollowerQuest, alias = nullptr
    RE::BSWriteLockGuard writeLock(aliasExtra->lock);
    auto& arr = aliasExtra->aliases;

    for (std::int32_t i = static_cast<std::int32_t>(arr.size()) - 1; i >= 0; --i) {
        auto* inst = arr[i];
        if (!inst) continue;
        if (inst->quest != quest) continue;
        if (inst->alias != nullptr) continue;  // our real alias instances have alias set

        // This is a dismissed sandbox — clean it up
        delete inst->instancedPackages;
        RE::free(inst);
        arr.erase(arr.begin() + i);

        logger::info("PackageManager: Cleared stale dismissed sandbox from {:08X}",
            actor->GetFormID());
    }
}

void PackageManager::UpdateIdleSandbox()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto& mgr = FollowerManager::GetSingleton();
    auto& followers = mgr.GetFollowers();

    // Accumulate real elapsed seconds. The idle threshold used to be added in
    // fixed 5.0f steps that assumed the poll interval, which made a single tick
    // of stillness trip it and coupled the threshold to the polling cadence.
    auto now = std::chrono::steady_clock::now();
    float elapsed = 0.0f;
    if (_idleTickValid) {
        elapsed = std::chrono::duration<float>(now - _lastIdleTick).count();
        // Guard against a stalled game / long load producing a huge delta.
        if (elapsed < 0.0f || elapsed > 30.0f) elapsed = 0.0f;
    }
    _lastIdleTick = now;
    _idleTickValid = true;

    // Prune bookkeeping for actors that are no longer tracked followers, so a
    // dismissed or unloaded actor can never pin the shared idle marker.
    if (!_sandboxActors.empty()) {
        for (auto it = _sandboxActors.begin(); it != _sandboxActors.end();) {
            if (!mgr.IsTracked(*it)) {
                logger::info("PackageManager: Pruned stale sandbox entry {:08X}", *it);
                it = _sandboxActors.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool anySandboxEnabled = false;
    for (auto& f : followers) {
        if (f.isSandboxEnabled && !f.isWaiting) {
            anySandboxEnabled = true;
            break;
        }
    }

    if (!anySandboxEnabled) {
        if (_followersAreSandboxing) {
            for (auto& f : followers) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
                if (actor && HasSandbox(actor)) {
                    RemoveSandbox(actor, true);
                }
            }
            _followersAreSandboxing = false;
            _idleTimer = 0.0f;
        }
        return;
    }

    // Combat cancels sandbox outright. Standing still to aim a bow or hold a
    // spell is not idling, and followers wandering off mid-fight reads as a bug.
    bool inCombat = player->IsInCombat();
    if (!inCombat) {
        for (auto& f : followers) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
            if (actor && !actor->IsDead() && actor->IsInCombat()) {
                inCombat = true;
                break;
            }
        }
    }

    auto playerPos = player->GetPosition();
    float dist = playerPos.GetDistance(_lastPlayerPos);

    // Crouching signals combat intent — treat as movement to cancel sandbox
    if (inCombat || dist > kMovementThreshold || player->IsSneaking()) {
        _lastPlayerPos = playerPos;
        _idleTimer = 0.0f;

        if (_followersAreSandboxing) {
            for (auto& f : followers) {
                if (f.isSandboxEnabled && !f.isWaiting) {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
                    if (actor && HasSandbox(actor)) {
                        RemoveSandbox(actor, true);
                    }
                }
            }
            _followersAreSandboxing = false;
            logger::info("PackageManager: {} — followers resume following",
                inCombat ? "Combat started" : "Player moving");
        }
    } else {
        _idleTimer += elapsed;

        if (!_followersAreSandboxing && _idleTimer >= kIdleThreshold) {
            for (auto& f : followers) {
                if (f.isSandboxEnabled && !f.isWaiting) {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
                    if (actor && !HasSandbox(actor)) {
                        ApplySandbox(actor, true);
                    }
                }
            }
            _followersAreSandboxing = true;
            logger::info("PackageManager: Player idle — followers sandboxing");
        } else if (_followersAreSandboxing) {
            // Apply sandbox to any followers enabled after the initial batch
            for (auto& f : followers) {
                if (f.isSandboxEnabled && !f.isWaiting) {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
                    if (actor && !HasSandbox(actor)) {
                        ApplySandbox(actor, true);
                    }
                }
            }
        }
    }
}

void PackageManager::ApplyFollowDistance(const std::string& preset)
{
    float range, distance;
    if (preset == "close") {
        range = 128.0f;
        distance = 128.0f;
    } else if (preset == "far") {
        range = 400.0f;
        distance = 384.0f;
    } else {
        // "normal" (default)
        range = 200.0f;
        distance = 200.0f;
    }

    auto* followPkg = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_FollowPkg");
    if (!followPkg || !followPkg->data) {
        logger::error("PackageManager: Horde_FollowPkg not found for follow distance");
        return;
    }

    auto* customData = static_cast<RE::TESCustomPackageData*>(followPkg->data);
    auto& dataList = customData->data;

    for (std::uint16_t i = 0; i < dataList.dataSize; i++) {
        auto uid = dataList.uids[i];
        auto* pkgData = dataList.data[i];
        if (!pkgData) continue;

        if (uid == 1 || uid == 2) {
            // BGSPackageDataFloat layout: vtable (8 bytes) + Data union (8 bytes)
            // The Data union sits at the same offset as BGSNamedPackageData<IPackageData>::data
            auto* named = reinterpret_cast<RE::BGSNamedPackageData<RE::IPackageData>*>(pkgData);
            named->data.f = (uid == 1) ? range : distance;
        }
    }

    // Force all tracked followers to re-evaluate packages
    auto& mgr = FollowerManager::GetSingleton();
    for (auto& f : mgr.GetFollowers()) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(f.actorFormID);
        if (actor && !actor->IsDead()) {
            actor->EvaluatePackage(true, false);
        }
    }

    logger::info("PackageManager: Follow distance set to '{}' (range={}, distance={})", preset, range, distance);
}

void PackageManager::Reset()
{
    _hordeQuest = nullptr;
    _questCached = false;
    _sandboxActors.clear();
    _lastPlayerPos = {0, 0, 0};
    _idleTimer = 0.0f;
    _followersAreSandboxing = false;
    _idleTickValid = false;
    logger::info("PackageManager: Reset");
}
