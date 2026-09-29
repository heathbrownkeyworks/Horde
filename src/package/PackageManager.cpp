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
    _residenceQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("Horde_ResidenceQuest");
    _residencePackage = RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_ResidencePkg");
    if (!_residenceQuest || !_residencePackage) {
        logger::error("PackageManager: Home records missing; install the matching Horde.esp");
    }
    _sandboxEnabled = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Horde_SandBoxEnabled");
    _sandboxFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("Horde_SandboxEnabledFaction");
    _questCached = true;
    if (!_sandboxEnabled || !_sandboxFaction) {
        logger::error("PackageManager: Sandbox records missing; install the matching Horde.esp");
    }
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

    // Match ForceRefTo: set the handle and refAliasMap for conditions and packages.
    refAlias->fillType = RE::BGSBaseAlias::FILL_TYPE::kForced;
    refAlias->fillData.forced.forcedRef = actor->GetHandle();

    {
        RE::BSWriteLockGuard questLock(quest->aliasAccessLock);
        quest->refAliasMap.erase(alias->aliasID);  // remove old entry if present
        quest->refAliasMap.insert({alias->aliasID, actor->GetHandle()});
    }

    // Register the alias packages with the actor's package evaluator.
    auto* aliasExtra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!aliasExtra) {
        auto* newExtra = new RE::ExtraAliasInstanceArray();
        actor->extraList.Add(newExtra);
        aliasExtra = newExtra;
    }

    // Populate the same fixed stack as the ESP. Refresh saved instances too:
    // older cosaves may contain only wait/follow or the old active sandbox stack.
    auto* pkgArray = new RE::BSTArray<RE::TESPackage*>();
    for (auto* editorID : {"Horde_SandboxWaitPkg", "Horde_SandboxActivePkg",
                           "Horde_WaitPkg", "Horde_FollowPkg"}) {
        auto* package = RE::TESForm::LookupByEditorID<RE::TESPackage>(editorID);
        if (package) {
            pkgArray->push_back(package);
        } else {
            logger::error("PackageManager: Missing package {}", editorID);
        }
    }

    auto* instData = static_cast<RE::BGSRefAliasInstanceData*>(
        RE::malloc(sizeof(RE::BGSRefAliasInstanceData)));
    if (!instData) {
        delete pkgArray;
        logger::error("PackageManager: Cannot allocate alias instance for slot {}", slot);
        return;
    }
    std::memset(instData, 0, sizeof(RE::BGSRefAliasInstanceData));
    instData->quest = quest;
    instData->alias = refAlias;
    instData->instancedPackages = pkgArray;

    {
        RE::BSWriteLockGuard writeLock(aliasExtra->lock);
        auto& arr = aliasExtra->aliases;
        for (std::int32_t i = static_cast<std::int32_t>(arr.size()) - 1; i >= 0; --i) {
            auto* previous = arr[i];
            if (previous && previous->quest == quest && previous->alias == refAlias) {
                delete previous->instancedPackages;
                RE::free(previous);
                arr.erase(arr.begin() + i);
            }
        }
        arr.push_back(instData);
    }

    // Clear any stale ExtraPackage — it overrides alias packages
    actor->extraList.RemoveByType(RE::ExtraDataType::kPackage);
    actor->extraList.RemoveByType(RE::ExtraDataType::kPackageStartLocation);

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

    // Remove only DialogueFollower instances while holding the actor alias lock.
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

    // Release the actor lock before taking quest locks.
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

bool PackageManager::HasAnimalAlias(RE::Actor* actor) const
{
    if (!actor) return false;
    auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DialogueFollower");
    if (!quest) return false;
    RE::BSReadLockGuard lock(quest->aliasAccessLock);
    auto it = quest->refAliasMap.find(1);  // DialogueFollower's Animal alias
    return it != quest->refAliasMap.end() && it->second.get().get() == actor;
}

void PackageManager::ReleaseAnimal(RE::Actor* actor)
{
    if (!actor) return;
    actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kVariable04, 0.0f);
    // Another animal may already occupy the vanilla slot during replacement.
    auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DialogueFollower");
    if (quest) {
        RE::BSReadLockGuard lock(quest->aliasAccessLock);
        auto it = quest->refAliasMap.find(1);
        if (it != quest->refAliasMap.end()) {
            auto current = it->second.get();
            if (current && current.get() != actor) return;
        }
    }
    if (auto* count = RE::TESForm::LookupByEditorID<RE::TESGlobal>("PlayerAnimalCount")) {
        count->value = 0.0f;
    }
}

bool PackageManager::EnsureResidencePackage(RE::Actor* actor)
{
    if (!_questCached) InitQuestCache();
    if (!actor || !_residenceQuest || !_residencePackage) return false;
    auto* extra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!extra) {
        extra = new RE::ExtraAliasInstanceArray();
        actor->extraList.Add(extra);
    }
    RE::BSWriteLockGuard lock(extra->lock);
    for (auto* instance : extra->aliases) {
        if (instance && instance->quest == _residenceQuest && !instance->alias) return false;
    }
    auto* instance = static_cast<RE::BGSRefAliasInstanceData*>(RE::malloc(sizeof(RE::BGSRefAliasInstanceData)));
    if (!instance) return false;
    std::memset(instance, 0, sizeof(RE::BGSRefAliasInstanceData));
    instance->quest = _residenceQuest;
    auto* packages = new RE::BSTArray<RE::TESPackage*>();
    packages->push_back(_residencePackage);
    instance->instancedPackages = packages;
    extra->aliases.push_back(instance);
    return true;
}

bool PackageManager::ClearResidencePackage(RE::Actor* actor)
{
    if (!_questCached) InitQuestCache();
    if (!actor || !_residenceQuest) return false;
    auto* extra = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
    if (!extra) return false;
    bool changed = false;
    RE::BSWriteLockGuard lock(extra->lock);
    for (std::int32_t i = static_cast<std::int32_t>(extra->aliases.size()) - 1; i >= 0; --i) {
        auto* instance = extra->aliases[i];
        if (!instance || instance->quest != _residenceQuest || instance->alias) continue;
        delete instance->instancedPackages;
        RE::free(instance);
        extra->aliases.erase(extra->aliases.begin() + i);
        changed = true;
    }
    return changed;
}

// --- Engine-driven sandbox settings ---

void PackageManager::SyncSandboxSettings()
{
    if (!_questCached) InitQuestCache();
    if (!_sandboxEnabled || !_sandboxFaction) return;

    const auto& followers = FollowerManager::GetSingleton().GetFollowers();
    bool anyEnabled = false;
    for (const auto& follower : followers) {
        anyEnabled = anyEnabled || follower.isSandboxEnabled;
    }
    const float enabledValue = anyEnabled ? 1.0f : 0.0f;
    const bool globalChanged = _sandboxEnabled->value != enabledValue;
    _sandboxEnabled->value = enabledValue;

    for (const auto& follower : followers) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(follower.actorFormID);
        if (!actor) continue;
        const bool preferenceChanged = actor->IsInFaction(_sandboxFaction) != follower.isSandboxEnabled;
        if (preferenceChanged) {
            if (follower.isSandboxEnabled) {
                actor->AddToFaction(_sandboxFaction, 0);
            } else {
                actor->RemoveFromFaction(_sandboxFaction);
            }
        }
        if ((preferenceChanged || globalChanged) && !actor->IsDead()) {
            actor->EvaluatePackage(true, false);
        }
    }
}

void PackageManager::ClearSandboxState(RE::Actor* actor)
{
    if (actor && _sandboxFaction && actor->IsInFaction(_sandboxFaction)) {
        actor->RemoveFromFaction(_sandboxFaction);
    }
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

        delete inst->instancedPackages;
        RE::free(inst);
        arr.erase(arr.begin() + i);

        logger::info("PackageManager: Cleared stale dismissed sandbox from {:08X}",
            actor->GetFormID());
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
    // Revert calls this before discarding the old save's followers.
    if (_sandboxEnabled) _sandboxEnabled->value = 0.0f;
    for (const auto& follower : FollowerManager::GetSingleton().GetFollowers()) {
        ClearSandboxState(RE::TESForm::LookupByID<RE::Actor>(follower.actorFormID));
    }
    _hordeQuest = nullptr;
    _residenceQuest = nullptr;
    _residencePackage = nullptr;
    _sandboxEnabled = nullptr;
    _sandboxFaction = nullptr;
    _questCached = false;
    logger::info("PackageManager: Reset");
}
