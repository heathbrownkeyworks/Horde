#include "pch.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "Settings.h"

#include <cmath>
#include <filesystem>
#include <thread>

FollowerManager& FollowerManager::GetSingleton()
{
    static FollowerManager singleton;
    return singleton;
}

RE::TESFaction* FollowerManager::GetCurrentFollowerFaction() const
{
    return RE::TESForm::LookupByEditorID<RE::TESFaction>("CurrentFollowerFaction");
}

RE::TESFaction* FollowerManager::GetHordeFollowerFaction() const
{
    return RE::TESForm::LookupByEditorID<RE::TESFaction>("Horde_FollowerFaction");
}

RE::TESGlobal* FollowerManager::GetPlayerFollowerCount() const
{
    return RE::TESForm::LookupByEditorID<RE::TESGlobal>("PlayerFollowerCount");
}

RE::Actor* FollowerManager::ResolveActor(RE::FormID formID) const
{
    return RE::TESForm::LookupByID<RE::Actor>(formID);
}

FollowerData* FollowerManager::FindFollower(RE::FormID formID)
{
    for (auto& f : _followers) {
        if (f.actorFormID == formID) {
            return &f;
        }
    }
    return nullptr;
}

// --- Essential / protection ---
//
// Essential lives on the shared TESNPC, so it must be applied and unwound with
// care: two followers built on the same actor base share one flag, and the flag
// survives in the global form pool across save loads unless we release it.

void FollowerManager::ApplyProtection(RE::TESNPC* actorBase, int protection)
{
    if (!actorBase) return;

    actorBase->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kEssential);
    actorBase->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kProtected);
    if (protection == 2) {
        actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
    } else if (protection == 1) {
        actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kProtected);
    }
}

void FollowerManager::ForceEssential(RE::Actor* actor)
{
    if (!actor) return;
    if (auto* actorBase = actor->GetActorBase()) {
        actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
    }
}

int FollowerManager::CaptureOriginalProtection(RE::Actor* actor, RE::TESNPC* actorBase) const
{
    if (!actorBase) return 0;

    // A follower already tracked on this same base means Horde has clobbered
    // the flags. Reading them now would record "essential" as the original.
    for (const auto& other : _followers) {
        auto* otherActor = ResolveActor(other.actorFormID);
        if (otherActor && otherActor != actor && otherActor->GetActorBase() == actorBase) {
            logger::info("Horde: Inherited originalProtection={} from {} (shared actor base {:08X})",
                other.originalProtection, other.name, actorBase->GetFormID());
            return other.originalProtection;
        }
    }

    if (actorBase->IsEssential()) return 2;
    if (actorBase->IsProtected()) return 1;
    return 0;
}

void FollowerManager::ReleaseEssential(RE::Actor* actor, RE::FormID formID, int originalProtection)
{
    if (!actor) return;

    auto* actorBase = actor->GetActorBase();
    if (!actorBase) return;

    // Another tracked follower sharing this base may still need the override.
    for (const auto& other : _followers) {
        if (other.actorFormID == formID) continue;
        if (!other.isEssential) continue;

        auto* otherActor = ResolveActor(other.actorFormID);
        if (otherActor && otherActor->GetActorBase() == actorBase) {
            logger::info("Horde: Kept essential on base {:08X} — still shared with {}",
                actorBase->GetFormID(), other.name);
            return;
        }
    }

    ApplyProtection(actorBase, originalProtection);
    logger::info("Horde: Restored {:08X} protection to {}", formID, originalProtection);
}

void FollowerManager::ReleaseAllEssential()
{
    for (const auto& f : _followers) {
        auto* actor = ResolveActor(f.actorFormID);
        if (!actor) continue;
        if (auto* actorBase = actor->GetActorBase()) {
            ApplyProtection(actorBase, f.originalProtection);
        }
    }
}

// --- SKSE Cosave: per-save-game persistence ---

void FollowerManager::OnCosaveRevert()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // Unwind Horde's essential override before dropping the follower list.
    // TESNPC flags live in the global form pool, so without this a follower made
    // essential in save A would stay essential after loading save B.
    ReleaseAllEssential();

    _followers.clear();
    _registry.clear();
    _rejectedCustomFollowers.clear();
    PackageManager::GetSingleton().Reset();
    logger::info("Horde: Cosave revert — essential overrides released, follower list and registry cleared");
}

void FollowerManager::OnCosaveSave(SKSE::SerializationInterface* a_intfc)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // Record 1: follower list
    {
        nlohmann::json j = _followers;
        std::string data = j.dump();

        if (!a_intfc->OpenRecord(kFollowerRecord, kCosaveVersion)) {
            logger::error("Horde: Failed to open FLWR cosave record");
            return;
        }

        std::uint32_t len = static_cast<std::uint32_t>(data.size());
        a_intfc->WriteRecordData(len);
        a_intfc->WriteRecordData(data.c_str(), len);

        logger::info("Horde: Cosave saved {} followers ({} bytes)", _followers.size(), len);
    }

    // Record 2: follower registry
    {
        // Serialize as {"formID": RegistryEntry, ...} with string keys
        nlohmann::json j;
        for (auto& [fid, entry] : _registry) {
            j[std::to_string(fid)] = entry;
        }
        std::string data = j.dump();

        if (!a_intfc->OpenRecord(kRegistryRecord, kCosaveVersion)) {
            logger::error("Horde: Failed to open RGST cosave record");
            return;
        }

        std::uint32_t len = static_cast<std::uint32_t>(data.size());
        a_intfc->WriteRecordData(len);
        a_intfc->WriteRecordData(data.c_str(), len);

        logger::info("Horde: Cosave saved {} registry entries ({} bytes)", _registry.size(), len);
    }
}

void FollowerManager::OnCosaveLoad(SKSE::SerializationInterface* a_intfc)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    std::uint32_t type, version, length;
    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (version != kCosaveVersion) {
            logger::warn("Horde: Cosave version mismatch (got {}, expected {})", version, kCosaveVersion);
            continue;
        }

        std::uint32_t strLen = 0;
        a_intfc->ReadRecordData(strLen);
        std::string data(strLen, '\0');
        a_intfc->ReadRecordData(data.data(), strLen);

        if (type == kFollowerRecord) {
            try {
                nlohmann::json j = nlohmann::json::parse(data);
                _followers = j.get<std::vector<FollowerData>>();

                for (auto& f : _followers) {
                    RE::FormID resolved = 0;
                    if (a_intfc->ResolveFormID(f.actorFormID, resolved)) {
                        f.actorFormID = resolved;
                    } else {
                        logger::warn("Horde: Failed to resolve actor FormID {:08X}, marking for removal", f.actorFormID);
                        f.actorFormID = 0;
                    }

                    if (f.homeWorldspace != 0) {
                        RE::FormID resolvedHome = 0;
                        if (a_intfc->ResolveFormID(f.homeWorldspace, resolvedHome)) {
                            f.homeWorldspace = resolvedHome;
                        } else {
                            f.homeWorldspace = 0;
                        }
                    }
                }

                auto before = _followers.size();
                _followers.erase(
                    std::remove_if(_followers.begin(), _followers.end(),
                        [](const FollowerData& f) { return f.actorFormID == 0; }),
                    _followers.end());

                if (_followers.size() < before) {
                    logger::warn("Horde: Removed {} followers with unresolvable FormIDs", before - _followers.size());
                }

                logger::info("Horde: Cosave loaded {} followers", _followers.size());
            } catch (const std::exception& e) {
                logger::error("Horde: Failed to parse FLWR cosave: {}", e.what());
                _followers.clear();
            }
        } else if (type == kRegistryRecord) {
            try {
                nlohmann::json j = nlohmann::json::parse(data);
                _registry.clear();

                for (auto& [key, val] : j.items()) {
                    RE::FormID oldID = static_cast<RE::FormID>(std::stoul(key));
                    RE::FormID resolved = 0;
                    if (!a_intfc->ResolveFormID(oldID, resolved)) {
                        logger::warn("Horde: Registry — failed to resolve {:08X}, dropping", oldID);
                        continue;
                    }

                    RegistryEntry entry = val.get<RegistryEntry>();

                    if (entry.homeWorldspace != 0) {
                        RE::FormID resolvedHome = 0;
                        if (a_intfc->ResolveFormID(entry.homeWorldspace, resolvedHome)) {
                            entry.homeWorldspace = resolvedHome;
                        } else {
                            entry.homeWorldspace = 0;
                            entry.homeX = entry.homeY = entry.homeZ = 0.0f;
                            entry.homeName.clear();
                        }
                    }

                    if (entry.originalEditorLocFormID != 0) {
                        RE::FormID resolvedOrig = 0;
                        if (a_intfc->ResolveFormID(entry.originalEditorLocFormID, resolvedOrig)) {
                            entry.originalEditorLocFormID = resolvedOrig;
                        } else {
                            // Original editor loc form no longer exists — drop it.
                            // We'll recapture next time the follower is recruited.
                            entry.originalEditorLocFormID = 0;
                            entry.originalEditorLocX = entry.originalEditorLocY = entry.originalEditorLocZ = 0.0f;
                            entry.originalEditorLocRot = 0.0f;
                        }
                    }

                    _registry[resolved] = std::move(entry);
                }

                logger::info("Horde: Cosave loaded {} registry entries", _registry.size());
            } catch (const std::exception& e) {
                logger::error("Horde: Failed to parse RGST cosave: {}", e.what());
                _registry.clear();
            }
        } else {
            logger::warn("Horde: Unknown cosave record type {:08X}", type);
        }
    }
}

void FollowerManager::Initialize()
{
    // Follower data is now loaded via cosave callbacks, not from JSON
    logger::info("Horde: FollowerManager initialized");
}

bool FollowerManager::IsAtCap() const
{
    return GetCount() >= Settings::GetSingleton().GetMaxFollowers();
}

void FollowerManager::UpdateDialogueGate()
{
    auto* global = GetPlayerFollowerCount();
    if (!global) {
        logger::warn("Horde: Could not find PlayerFollowerCount global");
        return;
    }

    float newVal = IsAtCap() ? 1.0f : 0.0f;
    if (global->value != newVal) {
        global->value = newVal;
        logger::info("Horde: Dialogue gate {} ({}/{})", newVal > 0 ? "CLOSED" : "OPEN",
            GetCount(), Settings::GetSingleton().GetMaxFollowers());
    }
}

bool FollowerManager::TrackFollower(RE::Actor* actor)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (!actor) {
        logger::warn("Horde: TrackFollower called with null actor");
        return false;
    }

    RE::FormID formID = actor->GetFormID();

    if (FindFollower(formID)) {
        logger::info("Horde: Actor {:08X} already tracked", formID);
        return false;
    }

    if (IsAtCap()) {
        logger::warn("Horde: Cannot track actor {:08X}, at follower cap", formID);
        return false;
    }

    // Only track vanilla DialogueFollower-based followers
    auto& pkgMgr = PackageManager::GetSingleton();
    if (!pkgMgr.HasDialogueFollowerAlias(actor)) {
        logger::info("Horde: Rejecting {} ({:08X}) — no DialogueFollower alias (custom follower system)",
            actor->GetDisplayFullName(), formID);
        return false;
    }

    // Find an empty alias slot in our quest
    int slot = pkgMgr.FindEmptySlot();
    if (slot == -1) {
        logger::warn("Horde: No empty alias slot for {:08X}", formID);
        Settings::Notify("Follower limit reached.");
        return false;
    }

    FollowerData data;
    data.actorFormID = formID;
    data.name = actor->GetDisplayFullName();
    data.level = actor->GetLevel();
    data.aliasSlot = slot;

    auto* actorBase = actor->GetActorBase();
    if (actorBase && actorBase->npcClass) {
        data.className = actorBase->npcClass->GetFullName();
    }

    data.originalAggression = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);

    // Save original protection level and make essential
    if (actorBase) {
        data.originalProtection = CaptureOriginalProtection(actor, actorBase);
        actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
    }

    auto& settings = Settings::GetSingleton();
    data.isSandboxEnabled = settings.GetDefaultSandboxEnabled();

    data.isPassive = false;
    data.isWaiting = false;
    data.isFollowClose = settings.GetDefaultFollowClose();

    _followers.push_back(data);
    UpsertRegistry(formID, data.name);
    CaptureOriginalEditorLoc(formID, actor);
    RestoreHome(_followers.back());
    if (_followers.back().homeWorldspace != 0) {
        ApplyHomeEditorLocation(
            actor,
            _followers.back().homeWorldspace,
            _followers.back().homeX,
            _followers.back().homeY,
            _followers.back().homeZ,
            _followers.back().homeName);
    }

    // Clean up any stale dismissed sandbox from a previous dismiss cycle
    pkgMgr.ClearDismissedSandbox(actor);

    // Fill our alias slot — this gives the actor our follow/wait/sandbox packages.
    // We intentionally leave the DialogueFollower alias intact so vanilla's
    // dismiss/wait/follow dialogue options continue to work.
    pkgMgr.FillSlot(slot, actor);

    if (auto* faction = GetCurrentFollowerFaction(); faction && !actor->IsInFaction(faction)) {
        actor->AddToFaction(faction, 0);
    }
    actor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);

    if (auto* hordeFaction = GetHordeFollowerFaction()) {
        actor->AddToFaction(hordeFaction, 1);
    }

    // Set sandbox opt-in if enabled
    if (data.isSandboxEnabled) {
        // sandbox opt-in is tracked in FollowerData; applied dynamically by UpdateIdleSandbox
    }

    actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 0.0f);
    actor->EvaluatePackage(true, false);

    logger::info("Horde: Tracked follower {} ({:08X}) in slot {}, protection={}, total: {}",
        data.name, formID, slot, data.originalProtection, _followers.size());

    Save();
    UpdateDialogueGate();
    return true;
}

bool FollowerManager::UntrackFollower(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = std::find_if(_followers.begin(), _followers.end(),
        [formID](const FollowerData& d) { return d.actorFormID == formID; });

    if (it == _followers.end()) {
        logger::warn("Horde: Cannot untrack {:08X}, not found", formID);
        return false;
    }

    auto* actor = ResolveActor(formID);
    if (actor) {
        // Restore original aggression
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, it->originalAggression);

        // Restore original protection level
        auto* actorBase = actor->GetActorBase();
        ReleaseEssential(actor, formID, it->originalProtection);

        // Remove from factions
        auto* faction = GetCurrentFollowerFaction();
        if (faction) {
            actor->AddToFaction(faction, -1);
        }
        // Mark as a dismissed follower, mirroring vanilla DialogueFollowerScript.DismissFollower.
        // Required so custom followers' "Follow me" re-recruit dialogue — which is gated on
        // GetInFaction DismissedFollowerFaction == 1 — reappears after a Horde-UI dismiss.
        // (SoftUntrack does not need this: that path means vanilla dialogue already dismissed
        // them and set the faction itself.)
        if (auto* dismissedFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("DismissedFollowerFaction")) {
            if (!actor->IsInFaction(dismissedFaction)) {
                actor->AddToFaction(dismissedFaction, 0);
            }
        }
        auto* hordeFaction = GetHordeFollowerFaction();
        if (hordeFaction && actor->IsInFaction(hordeFaction)) {
            actor->RemoveFromFaction(hordeFaction);
        }
        PackageManager::GetSingleton().RemoveSandbox(actor, true);

        // Clear teammate flag
        actor->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);

        // Set WaitingForPlayer to 0
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 0.0f);

        // Set editor location to assigned home. This lets the normal package
        // chain take over after dismissal without a slot-scoped Horde alias.
        if (it->homeWorldspace != 0) {
            ApplyHomeEditorLocation(actor, it->homeWorldspace, it->homeX, it->homeY, it->homeZ, it->homeName);
        }

        // Clear our alias slot
        bool hasHome = it->homeWorldspace != 0;
        PackageManager::GetSingleton().ClearSlot(it->aliasSlot);

        // Clear the vanilla DialogueFollower alias. This matches what
        // vanilla DialogueFollowerScript.DismissFollower() does via
        // pFollowerAlias.Clear() and is required to actually stop the
        // follower from following. Without this, the vanilla follow
        // package on the DialogueFollower alias keeps running until the
        // next scan cycle notices faction/teammate state has gone stale.
        // (v1.6.1 masked this by injecting a higher-priority Horde sandbox
        // package that overrode the vanilla package; v1.7.0's conditional
        // gate removed that mask and exposed the missing cleanup.)
        PackageManager::GetSingleton().ClearDialogueFollowerAlias(actor);
        PackageManager::GetSingleton().ClearDismissedSandbox(actor);

        // Release the follower to the regular editor-location package chain.
        // Dismissed followers must not use per-slot Horde home packages because
        // those slots are reused by active followers.
        if (hasHome) {
            logger::info("Horde: Released {:08X} to editor-location package chain (home={})",
                formID, it->homeName);
        } else {
            logger::info("Horde: Released {:08X} to vanilla package chain (no Horde home)", formID);
        }
        actor->EvaluatePackage(true, false);

        // Notify player
        auto msg = std::string(it->name) + " has been dismissed from your service.";
        Settings::Notify(msg.c_str());

        logger::info("Horde: Dismissed {:08X} from player service", formID);

        // Hireling re-hire fix
        {
            auto* hirelingFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("CurrentHireling");
            if (hirelingFaction && actor->IsInFaction(hirelingFaction)) {
                actor->RemoveFromFaction(hirelingFaction);
                logger::info("Horde: Removed {:08X} from CurrentHireling faction", formID);

                if (actorBase) {
                    std::string rehireID = "CanRehire";
                    rehireID += actorBase->GetFormEditorID();
                    auto* rehireGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>(rehireID);
                    if (rehireGlobal) {
                        auto* daysPassed = RE::TESForm::LookupByEditorID<RE::TESGlobal>("GameDaysPassed");
                        rehireGlobal->value = (daysPassed ? daysPassed->value : 0.0f) + 9999.0f;
                        logger::info("Horde: Set {} = {} (re-hire enabled)", rehireID, rehireGlobal->value);
                    }
                }
            }
        }
    }

    // Drop sandbox bookkeeping unconditionally — if ResolveActor failed above,
    // the block that calls RemoveSandbox was skipped entirely and the FormID
    // would otherwise stay in the sandbox set forever, pinning the idle marker.
    PackageManager::GetSingleton().ForgetSandboxActor(formID);

    std::string name = it->name;
    _followers.erase(it);

    logger::info("Horde: Untracked follower {} ({:08X}), total: {}", name, formID, _followers.size());

    Save();
    UpdateDialogueGate();
    return true;
}

void FollowerManager::SoftUntrack(RE::FormID formID)
{
    // Called when vanilla (dialogue dismiss) already cleared faction/teammate/alias.
    auto it = std::find_if(_followers.begin(), _followers.end(),
        [formID](const FollowerData& d) { return d.actorFormID == formID; });

    if (it == _followers.end()) return;

    auto* actor = ResolveActor(formID);
    if (actor) {
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, it->originalAggression);

        // Restore original protection level
        ReleaseEssential(actor, formID, it->originalProtection);

        PackageManager::GetSingleton().RemoveSandbox(actor, true);
        auto* hordeFaction = GetHordeFollowerFaction();
        if (hordeFaction && actor->IsInFaction(hordeFaction)) {
            actor->RemoveFromFaction(hordeFaction);
        }
    }

    // Drop sandbox bookkeeping unconditionally — an unresolvable actor would
    // otherwise leave a dead FormID pinning the shared idle marker.
    PackageManager::GetSingleton().ForgetSandboxActor(formID);

    // Clear alias slot
    if (it->aliasSlot >= 0) {
        PackageManager::GetSingleton().ClearSlot(it->aliasSlot);
    }

    std::string name = it->name;
    _followers.erase(it);

    logger::info("Horde: Soft-untracked {} ({:08X}) — dismissed via dialogue, total: {}",
        name, formID, _followers.size());
}

void FollowerManager::ApplyPassive(RE::Actor* actor, bool passive, FollowerData& data)
{
    if (passive) {
        if (!data.isPassive) {
            data.originalAggression = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);
        }
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 0.0f);

        // Aggression only governs whether an actor *starts* a fight — it does
        // not end one already in progress. Break combat explicitly so the
        // follower disengages and the follow package pulls them back to the
        // player instead of finishing their current target.
        actor->StopCombat();
        actor->StopAlarmOnActor();
        actor->EvaluatePackage(true, false);

        logger::info("Horde: Set {:08X} to passive (aggression 0, combat broken)", actor->GetFormID());
    } else {
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, data.originalAggression);
        actor->EvaluatePackage(true, false);
        logger::info("Horde: Restored {:08X} aggression to {}", actor->GetFormID(), data.originalAggression);
    }
}

void FollowerManager::Summon(RE::FormID formID, bool silent)
{
    auto* actor = ResolveActor(formID);
    if (!actor) {
        logger::warn("Horde: Cannot summon {:08X}, actor not found", formID);
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // MoveTo handles cell/worldspace transfer, then reposition behind the player
    actor->MoveTo(player);

    float heading = player->GetAngleZ();
    constexpr float kBehindDist = 250.0f;  // ~3-4 paces behind
    auto pos = player->GetPosition();
    RE::NiPoint3 behind{
        pos.x - kBehindDist * std::sin(heading),
        pos.y - kBehindDist * std::cos(heading),
        pos.z
    };
    actor->SetPosition(behind, true);

    if (!silent) {
        auto msg = std::string("You have summoned ") + actor->GetDisplayFullName() + ".";
        Settings::Notify(msg.c_str());
    }
    logger::info("Horde: Summoned {:08X} behind player", formID);
}

void FollowerManager::SummonAll()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (auto& f : _followers) {
        Summon(f.actorFormID, true);  // silent — single group notification below
    }
    if (!_followers.empty()) {
        Settings::Notify("You have summoned all of your followers.");
    }
}

void FollowerManager::SetFollow(RE::FormID formID, bool silent)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetFollow - {:08X} not tracked", formID);
        return;
    }

    data->isWaiting = false;

    auto* actor = ResolveActor(formID);
    if (actor) {
        // Remove wait-sandbox if active — the idle system will re-apply if needed
        if (PackageManager::GetSingleton().HasSandbox(actor)) {
            PackageManager::GetSingleton().RemoveSandbox(actor, true);
        }
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 0.0f);
        actor->EvaluatePackage(true, false);
    }

    if (!silent) {
        auto msg = data->name + " is following you.";
        Settings::Notify(msg.c_str());
    }

    Save();
    logger::info("Horde: {:08X} set to follow", formID);
}

void FollowerManager::SetWait(RE::FormID formID, bool silent)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetWait - {:08X} not tracked", formID);
        return;
    }

    data->isWaiting = true;

    auto* actor = ResolveActor(formID);
    if (actor) {
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 1.0f);
        if (PackageManager::GetSingleton().HasSandbox(actor)) {
            PackageManager::GetSingleton().RemoveSandbox(actor, true);
        }
        actor->EvaluatePackage(true, false);
    }

    if (!silent) {
        // Horde_WaitPkg holds position — it deliberately has none of the sandbox
        // behaviour keys enabled, so a waiting follower stays put regardless of
        // their idle-sandbox setting. Don't claim otherwise.
        auto msg = data->name + " is now waiting.";
        Settings::Notify(msg.c_str());
    }

    Save();
    logger::info("Horde: {:08X} set to wait", formID);
}

void FollowerManager::SetPassive(RE::FormID formID, bool passive, bool silent)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetPassive - {:08X} not tracked", formID);
        return;
    }

    auto* actor = ResolveActor(formID);
    if (!actor) {
        // Still record the intent — OnPostLoadGame re-applies passive when the
        // actor loads back in.
        data->isPassive = passive;
        logger::warn("Horde: SetPassive - actor {:08X} not loaded, state recorded only", formID);
        Save();
        return;
    }

    ApplyPassive(actor, passive, *data);
    data->isPassive = passive;

    if (!silent) {
        auto msg = data->name + (passive ? " is now passive." : " is no longer passive.");
        Settings::Notify(msg.c_str());
    }

    Save();
}

void FollowerManager::SetPassiveAll(bool passive)
{
    std::vector<RE::FormID> ids;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& f : _followers) {
            ids.push_back(f.actorFormID);
        }
    }

    for (auto id : ids) {
        SetPassive(id, passive, true);  // silent — single group notification below
    }

    if (!ids.empty()) {
        Settings::Notify(passive
            ? "Your followers stand down."
            : "Your followers are ready to fight.");
    }

    logger::info("Horde: Group passive set to {} for {} follower(s)", passive, ids.size());
}

void FollowerManager::SetEssential(RE::FormID formID, bool essential)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetEssential - {:08X} not tracked", formID);
        return;
    }

    auto* actor = ResolveActor(formID);
    if (!actor) {
        logger::warn("Horde: SetEssential - actor {:08X} not found", formID);
        return;
    }

    auto* actorBase = actor->GetActorBase();
    if (!actorBase) {
        logger::warn("Horde: SetEssential - {:08X} has no actor base", formID);
        return;
    }

    data->isEssential = essential;

    if (essential) {
        // Re-apply Horde's forced essential.
        actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
    } else {
        // Clear our forced essential and restore the actor's original protection
        // level. This lets quests like Boethiah's Calling kill the follower.
        // ReleaseEssential keeps the flag if another tracked follower shares
        // this actor base and still wants the override.
        ReleaseEssential(actor, formID, data->originalProtection);
    }

    auto msg = data->name + (essential
        ? " is now essential and cannot be killed."
        : " is no longer essential and can be killed.");
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: {:08X} essential set to {} (originalProtection={})",
        formID, essential, data->originalProtection);
}

void FollowerManager::SetSandbox(RE::FormID formID, bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetSandbox - {:08X} not tracked", formID);
        return;
    }

    data->isSandboxEnabled = enabled;

    auto* actor = ResolveActor(formID);
    if (actor) {
        auto& pkgMgr = PackageManager::GetSingleton();
        if (data->isWaiting) {
            // Waiting followers already sandbox via Horde_WaitPkg. Do not use
            // the shared idle marker for them.
            if (pkgMgr.HasSandbox(actor)) {
                pkgMgr.RemoveSandbox(actor, true);
            }
        } else if (!enabled) {
            pkgMgr.RemoveSandbox(actor, true);
        }
        actor->EvaluatePackage(true, false);
    }

    auto msg = data->name + (enabled ? " will sandbox when idle." : " will no longer sandbox.");
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: {:08X} sandbox set to {}", formID, enabled);
}

bool FollowerManager::IsTracked(RE::FormID formID) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (const auto& f : _followers) {
        if (f.actorFormID == formID) return true;
    }
    return false;
}

bool FollowerManager::IsWaiting(RE::FormID formID) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (const auto& f : _followers) {
        if (f.actorFormID == formID) return f.isWaiting;
    }
    return false;
}

bool FollowerManager::IsPassive(RE::FormID formID) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (const auto& f : _followers) {
        if (f.actorFormID == formID) return f.isPassive;
    }
    return false;
}

bool FollowerManager::IsAllPassive() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_followers.empty()) return false;
    for (const auto& f : _followers) {
        if (!f.isPassive) return false;
    }
    return true;
}

void FollowerManager::FollowAll()
{
    std::vector<RE::FormID> ids;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& f : _followers) {
            ids.push_back(f.actorFormID);
        }
    }
    for (auto id : ids) {
        SetFollow(id, true);  // silent — single group notification below
    }
    if (!ids.empty()) {
        Settings::Notify("All followers are now following.");
    }
}

void FollowerManager::WaitAll()
{
    std::vector<RE::FormID> ids;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& f : _followers) {
            ids.push_back(f.actorFormID);
        }
    }
    for (auto id : ids) {
        SetWait(id, true);  // silent — single group notification below
    }
    if (!ids.empty()) {
        Settings::Notify("All followers are now waiting.");
    }
}


void FollowerManager::ScanForFollowers()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* faction = GetCurrentFollowerFaction();
    if (!faction) return;

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return;

    auto& pkgMgr = PackageManager::GetSingleton();

    // Build set of currently detected follower FormIDs
    std::set<RE::FormID> detectedIDs;
    std::set<RE::FormID> trackedBeforeScan;
    for (const auto& f : _followers) {
        trackedBeforeScan.insert(f.actorFormID);
    }

    for (auto& handle : processLists->highActorHandles) {
        auto actorPtr = handle.get();
        if (!actorPtr) continue;
        auto* actor = actorPtr.get();
        if (!actor) continue;
        if (actor->IsInFaction(faction) && actor->IsPlayerTeammate()) {
            detectedIDs.insert(actor->GetFormID());
        }
    }

    bool newFollowerDetectedThisScan = false;
    for (auto id : detectedIDs) {
        if (trackedBeforeScan.find(id) == trackedBeforeScan.end()) {
            newFollowerDetectedThisScan = true;
            break;
        }
    }

    // Track new followers
    for (auto id : detectedIDs) {
        if (FindFollower(id)) continue;

        auto* actor = ResolveActor(id);
        if (!actor) continue;

        // Reject custom-voiced followers (no DialogueFollower alias)
        if (!pkgMgr.HasDialogueFollowerAlias(actor)) {
            if (_rejectedCustomFollowers.insert(id).second) {
                logger::info("Horde: Skipping {} ({:08X}) — custom follower system",
                    actor->GetDisplayFullName(), id);
                auto msg = std::string(actor->GetDisplayFullName()) +
                    " uses a custom follower system and was not added to Horde.";
                Settings::Notify(msg.c_str());
            }
            continue;
        }

        if (IsAtCap()) continue;

        int slot = pkgMgr.FindEmptySlot();
        if (slot == -1) continue;

        FollowerData data;
        data.actorFormID = id;
        data.name = actor->GetDisplayFullName();
        data.level = actor->GetLevel();
        data.aliasSlot = slot;

        auto* actorBase = actor->GetActorBase();
        if (actorBase && actorBase->npcClass) {
            data.className = actorBase->npcClass->GetFullName();
        }

        data.originalAggression = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);

        // Save original protection level and make essential
        if (actorBase) {
            data.originalProtection = CaptureOriginalProtection(actor, actorBase);
            actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
        }

        auto& settings = Settings::GetSingleton();
        data.isSandboxEnabled = settings.GetDefaultSandboxEnabled();

        data.isFollowClose = settings.GetDefaultFollowClose();

        _followers.push_back(data);
        UpsertRegistry(id, data.name);
        CaptureOriginalEditorLoc(id, actor);
        RestoreHome(_followers.back());
        if (_followers.back().homeWorldspace != 0) {
            ApplyHomeEditorLocation(
                actor,
                _followers.back().homeWorldspace,
                _followers.back().homeX,
                _followers.back().homeY,
                _followers.back().homeZ,
                _followers.back().homeName);
        }

        pkgMgr.ClearDismissedSandbox(actor);
        pkgMgr.FillSlot(slot, actor);

        if (data.isSandboxEnabled) {
            // sandbox opt-in is tracked in FollowerData; applied dynamically by UpdateIdleSandbox
        }

        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 0.0f);
        actor->EvaluatePackage(true, false);

        // Mark as Horde-tracked (suppresses vanilla dismiss/wait/follow dialogue)
        auto* hordeFaction = GetHordeFollowerFaction();
        if (hordeFaction) {
            actor->AddToFaction(hordeFaction, 1);
        }

        logger::info("Horde: Scan detected new follower {} ({:08X}) in slot {}, protection={}",
            data.name, id, slot, data.originalProtection);
    }

    // Detect dialogue-dismissed followers: if a tracked follower has lost BOTH
    // faction membership AND teammate status, vanilla's dismiss dialogue ran.
    // SoftUntrack them instead of fighting the dismiss by re-adding.
    //
    // IMPORTANT: When vanilla recruits a NEW follower, it dismisses the PREVIOUS
    // one first (clears faction + teammate) before adding the new one. If we detect
    // a new untracked follower in the same scan, the old follower's state loss is
    // vanilla's recruit-dismiss cycle — we should re-add, not SoftUntrack.
    {
        if (!newFollowerDetectedThisScan) {
            std::vector<RE::FormID> dialogueDismissed;
            for (auto& f : _followers) {
                auto* actor = ResolveActor(f.actorFormID);
                if (!actor) continue;

                if (!actor->IsInFaction(faction) && !actor->IsPlayerTeammate()) {
                    dialogueDismissed.push_back(f.actorFormID);
                }
            }
            for (auto id : dialogueDismissed) {
                logger::info("Horde: Detected dialogue dismiss for {:08X}", id);
                SoftUntrack(id);
            }
        }
    }

    // Re-ensure state for tracked followers (only those still tracked after dismiss detection)
    for (auto& f : _followers) {
        auto* actor = ResolveActor(f.actorFormID);
        if (!actor || actor->IsDead()) continue;

        // Re-apply Horde's essential override only if the user has it enabled.
        auto* actorBase = actor->GetActorBase();
        if (f.isEssential && actorBase && !actorBase->IsEssential()) {
            actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
            logger::info("Horde: Re-applied essential to {}", f.name);
        }

        bool fixed = false;

        if (!actor->IsInFaction(faction)) {
            actor->AddToFaction(faction, 0);
            fixed = true;
        }

        // Ensure Horde tracker faction membership (rank 1 = tracked by Horde).
        // Used by EventHandler to identify tracked followers for branch suppression.
        auto* hordeFaction = GetHordeFollowerFaction();
        if (!hordeFaction) {
            logger::warn("Horde: GetHordeFollowerFaction() returned null for {}", f.name);
        } else if (!actor->IsInFaction(hordeFaction)) {
            actor->AddToFaction(hordeFaction, 1);
            logger::info("Horde: Added {} to Horde_FollowerFaction (rank 1)", f.name);
            fixed = true;
        }

        if (!actor->IsPlayerTeammate()) {
            actor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            fixed = true;
        }

        // Verify alias slot is filled
        if (f.aliasSlot >= 0 && !pkgMgr.IsSlotFilled(f.aliasSlot)) {
            pkgMgr.FillSlot(f.aliasSlot, actor);
            fixed = true;
            logger::info("Horde: Re-filled alias slot {} for {}", f.aliasSlot, f.name);
        }

        // Re-sync WaitingForPlayer
        float expectedWait = f.isWaiting ? 1.0f : 0.0f;
        float currentWait = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer);
        if (currentWait != expectedWait) {
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, expectedWait);
            fixed = true;
        }

        if (fixed) {
            actor->EvaluatePackage(true, false);
            logger::info("Horde: Restored state for {} ({:08X})", f.name, f.actorFormID);
        }
    }

    Save();
    UpdateDialogueGate();
    logger::trace("Horde: Scan complete, {} followers tracked", _followers.size());
}

void FollowerManager::OnDialogueClose()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // Sync Horde state with any changes vanilla's dialogue scripts made.
    // Vanilla's TIF fragments fire Wait/Follow/Dismiss commands that change
    // WaitingForPlayer AV and faction/teammate status. Instead of fighting
    // these changes, we detect and sync to them.
    for (auto& f : _followers) {
        auto* actor = ResolveActor(f.actorFormID);
        if (!actor) continue;

        // Check if vanilla changed WaitingForPlayer (wait/follow via dialogue)
        float currentWait = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer);
        bool vanillaWaiting = currentWait >= 1.0f;

        if (vanillaWaiting != f.isWaiting) {
            f.isWaiting = vanillaWaiting;

            if (vanillaWaiting) {
                // Vanilla set wait — remove sandbox if active
                PackageManager::GetSingleton().RemoveSandbox(actor, true);
            }

            auto msg = f.name + (vanillaWaiting ? " is now waiting." : " is following you.");
            Settings::Notify(msg.c_str());

            logger::info("Horde: Synced {} to {} (vanilla dialogue)",
                f.name, vanillaWaiting ? "waiting" : "following");
        }
    }

    Save();
    ScanForFollowers();
}

void FollowerManager::OnPostLoadGame()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* faction = GetCurrentFollowerFaction();
    auto& pkgMgr = PackageManager::GetSingleton();

    // Initialize quest cache first
    pkgMgr.InitQuestCache();

    int recovered = 0;
    int removed = 0;

    auto it = _followers.begin();
    while (it != _followers.end()) {
        auto* actor = ResolveActor(it->actorFormID);

        if (!actor) {
            logger::info("Horde: Post-load removing {} ({:08X}) - actor not found",
                it->name, it->actorFormID);
            it = _followers.erase(it);
            removed++;
            continue;
        }

        // Re-fill alias slot — ForceRefTo aliases are runtime-only, not persisted
        if (it->aliasSlot >= 0) {
            pkgMgr.FillSlot(it->aliasSlot, actor);
        } else {
            // No slot assigned — assign one
            int slot = pkgMgr.FindEmptySlot();
            if (slot >= 0) {
                it->aliasSlot = slot;
                pkgMgr.FillSlot(slot, actor);
            } else {
                logger::error("Horde: No slot available for {} ({:08X})", it->name, it->actorFormID);
            }
        }

        // Re-ensure CurrentFollowerFaction membership
        if (faction && !actor->IsInFaction(faction)) {
            actor->AddToFaction(faction, 0);
        }

        // Re-ensure Horde tracker faction (suppresses vanilla dismiss/wait/follow dialogue)
        auto* hordeFaction = GetHordeFollowerFaction();
        if (hordeFaction && !actor->IsInFaction(hordeFaction)) {
            actor->AddToFaction(hordeFaction, 1);
        }
        if (!actor->IsPlayerTeammate()) {
            actor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
        }

        // Re-apply passive
        if (it->isPassive) {
            ApplyPassive(actor, true, *it);
        }

        // Re-apply essential override (user may have toggled it off pre-save)
        if (auto* actorBase = actor->GetActorBase()) {
            if (it->isEssential) {
                actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
            } else {
                ApplyProtection(actorBase, it->originalProtection);
            }
        }

        // Re-apply wait state
        actor->AsActorValueOwner()->SetActorValue(
            RE::ActorValue::kWaitingForPlayer, it->isWaiting ? 1.0f : 0.0f);

        // Re-apply the home as an editor location. ForceRefTo aliases and actor
        // runtime data are not persisted, so this has to be redone every load.
        if (it->homeWorldspace != 0) {
            ApplyHomeEditorLocation(actor, it->homeWorldspace, it->homeX, it->homeY, it->homeZ, it->homeName);
        }

        // Ensure follower exists in registry (backfill for saves made before registry existed)
        UpsertRegistry(it->actorFormID, it->name);
        SyncRegistryHome(*it);

        actor->EvaluatePackage(true, false);

        recovered++;
        ++it;
    }

    // Apply saved follow distance preset to package data
    pkgMgr.ApplyFollowDistance(Settings::GetSingleton().GetFollowDistance());

    UpdateDialogueGate();
    Save();

    logger::info("Horde: Post-load recovery complete: {} recovered, {} removed, {} registry entries",
        recovered, removed, _registry.size());
}

void FollowerManager::SetFollowClose(RE::FormID formID, bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetFollowClose - {:08X} not tracked", formID);
        return;
    }

    data->isFollowClose = enabled;

    auto msg = data->name + (enabled ? " will now teleport when falling behind." : " will no longer auto-teleport.");
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: {:08X} followClose set to {}", formID, enabled);
}

void FollowerManager::SetHome(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: SetHome - {:08X} not tracked", formID);
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (data->aliasSlot < 0) {
        logger::warn("Horde: SetHome - {:08X} has no alias slot", formID);
        return;
    }

    // Store home data for display and editor location on dismiss.
    // Homes are driven entirely by the actor's editor location — the old
    // per-slot XMarker / Horde_HomeSandbox package pair is gone.
    auto* worldspace = player->GetWorldspace();
    if (!worldspace) {
        auto* cell = player->GetParentCell();
        if (!cell) {
            logger::warn("Horde: SetHome - player has no worldspace or cell");
            return;
        }
        data->homeWorldspace = cell->GetFormID();
    } else {
        data->homeWorldspace = worldspace->GetFormID();
    }

    auto pos = player->GetPosition();
    data->homeX = pos.x;
    data->homeY = pos.y;
    data->homeZ = pos.z;

    auto* loc = player->GetCurrentLocation();
    if (loc && loc->GetFullName()[0]) {
        data->homeName = loc->GetFullName();
    } else {
        data->homeName = "Unknown Location";
    }

    SyncRegistryHome(*data);
    if (auto* actor = ResolveActor(formID)) {
        ApplyHomeEditorLocation(actor, data->homeWorldspace, data->homeX, data->homeY, data->homeZ, data->homeName);
    }

    auto msg = data->name + "'s home has been set to " + data->homeName + ".";
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: {:08X} home set to {} ({}, {}, {})",
        formID, data->homeName, data->homeX, data->homeY, data->homeZ);
}

void FollowerManager::ClearHome(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* data = FindFollower(formID);
    if (!data) {
        logger::warn("Horde: ClearHome - {:08X} not tracked", formID);
        return;
    }

    data->homeWorldspace = 0;
    data->homeX = 0.0f;
    data->homeY = 0.0f;
    data->homeZ = 0.0f;
    data->homeName.clear();
    SyncRegistryHome(*data);

    if (auto* actor = ResolveActor(formID)) {
        if (auto regIt = _registry.find(formID); regIt != _registry.end()) {
            RestoreOriginalEditorLocation(actor, regIt->second, formID);
        }
        PackageManager::GetSingleton().ClearDismissedSandbox(actor);
        actor->EvaluatePackage(true, false);
    }

    auto msg = data->name + " no longer has an assigned home.";
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: {:08X} home cleared", formID);
}

// --- Registry helpers ---

void FollowerManager::UpsertRegistry(RE::FormID formID, const std::string& name)
{
    auto it = _registry.find(formID);
    if (it != _registry.end()) {
        it->second.name = name;  // refresh display name, keep home data
    } else {
        _registry[formID] = RegistryEntry{name};
    }
}

void FollowerManager::CaptureOriginalEditorLoc(RE::FormID formID, RE::Actor* actor)
{
    if (!actor) return;

    auto it = _registry.find(formID);
    if (it == _registry.end()) return;

    // Already captured — don't overwrite. Horde may have clobbered editorLocForm
    // since the first capture, and we want to keep the true original.
    if (it->second.originalEditorLocFormID != 0) return;

    auto& rt = actor->GetActorRuntimeData();
    if (!rt.editorLocForm) return;

    it->second.originalEditorLocFormID = rt.editorLocForm->GetFormID();
    it->second.originalEditorLocX = rt.editorLocCoord.x;
    it->second.originalEditorLocY = rt.editorLocCoord.y;
    it->second.originalEditorLocZ = rt.editorLocCoord.z;
    it->second.originalEditorLocRot = rt.editorLocRot;

    logger::info("Horde: Captured original editor location for {:08X} -> {:08X} ({}, {}, {})",
        formID, it->second.originalEditorLocFormID,
        it->second.originalEditorLocX, it->second.originalEditorLocY, it->second.originalEditorLocZ);
}
bool FollowerManager::ApplyHomeEditorLocation(
    RE::Actor* actor,
    RE::FormID homeFormID,
    float x,
    float y,
    float z,
    const std::string& homeName)
{
    if (!actor || homeFormID == 0) return false;

    auto* homeForm = RE::TESForm::LookupByID(homeFormID);
    if (!homeForm) {
        logger::warn("Horde: Cannot apply home editor location for {:08X}; form {:08X} not found",
            actor->GetFormID(), homeFormID);
        return false;
    }

    auto& runtimeData = actor->GetActorRuntimeData();
    runtimeData.editorLocCoord = {x, y, z};
    runtimeData.editorLocRot = 0.0f;
    runtimeData.editorLocForm = homeForm;

    logger::info("Horde: Set {:08X} editor location to {} ({}, {}, {})",
        actor->GetFormID(), homeName, x, y, z);
    return true;
}

bool FollowerManager::RestoreOriginalEditorLocation(RE::Actor* actor, const RegistryEntry& entry, RE::FormID formID)
{
    if (!actor || entry.originalEditorLocFormID == 0) return false;

    auto* origForm = RE::TESForm::LookupByID(entry.originalEditorLocFormID);
    if (!origForm) {
        logger::warn("Horde: Cannot restore original editor location for {:08X}; form {:08X} not found",
            formID, entry.originalEditorLocFormID);
        return false;
    }

    auto& rt = actor->GetActorRuntimeData();
    rt.editorLocForm = origForm;
    rt.editorLocCoord = {
        entry.originalEditorLocX,
        entry.originalEditorLocY,
        entry.originalEditorLocZ
    };
    rt.editorLocRot = entry.originalEditorLocRot;

    logger::info("Horde: Restored original editor location for {:08X} -> {:08X}",
        formID, entry.originalEditorLocFormID);
    return true;
}

void FollowerManager::SyncRegistryHome(const FollowerData& data)
{
    auto it = _registry.find(data.actorFormID);
    if (it == _registry.end()) return;

    it->second.homeWorldspace = data.homeWorldspace;
    it->second.homeX = data.homeX;
    it->second.homeY = data.homeY;
    it->second.homeZ = data.homeZ;
    it->second.homeName = data.homeName;
}

void FollowerManager::RestoreHome(FollowerData& data)
{
    auto it = _registry.find(data.actorFormID);
    if (it == _registry.end()) return;
    if (it->second.homeWorldspace == 0) return;

    data.homeWorldspace = it->second.homeWorldspace;
    data.homeX = it->second.homeX;
    data.homeY = it->second.homeY;
    data.homeZ = it->second.homeZ;
    data.homeName = it->second.homeName;

    logger::info("Horde: Restored home from registry for {:08X} -> {}", data.actorFormID, data.homeName);
}

// --- Dismissed follower operations ---

void FollowerManager::SummonDismissed(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (_registry.find(formID) == _registry.end()) {
        logger::warn("Horde: SummonDismissed — {:08X} not in registry", formID);
        return;
    }

    // Don't summon active followers through this path
    if (FindFollower(formID)) {
        logger::warn("Horde: SummonDismissed — {:08X} is active, use Summon instead", formID);
        return;
    }

    auto* actor = ResolveActor(formID);
    if (!actor) {
        Settings::Notify("Cannot summon — follower is not loaded.");
        logger::warn("Horde: SummonDismissed — {:08X} not loaded", formID);
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    actor->MoveTo(player);

    float heading = player->GetAngleZ();
    constexpr float kBehindDist = 250.0f;
    auto pos = player->GetPosition();
    RE::NiPoint3 behind{
        pos.x - kBehindDist * std::sin(heading),
        pos.y - kBehindDist * std::cos(heading),
        pos.z
    };
    actor->SetPosition(behind, true);

    auto msg = std::string("You have summoned ") + actor->GetDisplayFullName() + ".";
    Settings::Notify(msg.c_str());
    logger::info("Horde: Summoned dismissed {:08X} behind player", formID);
}

void FollowerManager::SetDismissedHome(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = _registry.find(formID);
    if (it == _registry.end()) {
        logger::warn("Horde: SetDismissedHome — {:08X} not in registry", formID);
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* worldspace = player->GetWorldspace();
    if (!worldspace) {
        auto* cell = player->GetParentCell();
        if (!cell) return;
        it->second.homeWorldspace = cell->GetFormID();
    } else {
        it->second.homeWorldspace = worldspace->GetFormID();
    }

    auto pos = player->GetPosition();
    it->second.homeX = pos.x;
    it->second.homeY = pos.y;
    it->second.homeZ = pos.z;

    auto* loc = player->GetCurrentLocation();
    if (loc && loc->GetFullName()[0]) {
        it->second.homeName = loc->GetFullName();
    } else {
        it->second.homeName = "Unknown Location";
    }

    if (auto* actor = ResolveActor(formID)) {
        CaptureOriginalEditorLoc(formID, actor);
        ApplyHomeEditorLocation(
            actor,
            it->second.homeWorldspace,
            it->second.homeX,
            it->second.homeY,
            it->second.homeZ,
            it->second.homeName);
        PackageManager::GetSingleton().ClearDismissedSandbox(actor);
        actor->EvaluatePackage(true, false);
    }

    auto msg = it->second.name + "'s home has been set to " + it->second.homeName + ".";
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: Dismissed {:08X} home set to {}", formID, it->second.homeName);
}

void FollowerManager::ClearDismissedHome(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = _registry.find(formID);
    if (it == _registry.end()) {
        logger::warn("Horde: ClearDismissedHome — {:08X} not in registry", formID);
        return;
    }

    // Restore the actor's vanilla editor location if we previously clobbered
    // it during dismissal. This lets Hearthfire stewards, third-party home
    // mods, etc. resume normal behavior after the user clears a Horde home.
    if (auto* actor = ResolveActor(formID)) {
        RestoreOriginalEditorLocation(actor, it->second, formID);
        PackageManager::GetSingleton().ClearDismissedSandbox(actor);
        actor->EvaluatePackage(true, false);
    }

    it->second.homeWorldspace = 0;
    it->second.homeX = it->second.homeY = it->second.homeZ = 0.0f;
    it->second.homeName.clear();

    auto msg = it->second.name + " no longer has an assigned home.";
    Settings::Notify(msg.c_str());

    Save();
    logger::info("Horde: Dismissed {:08X} home cleared", formID);
}

void FollowerManager::ForgetFollower(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = _registry.find(formID);
    if (it == _registry.end()) return;

    // Safety: don't forget active followers
    if (FindFollower(formID)) {
        logger::warn("Horde: ForgetFollower — {:08X} is active, cannot forget", formID);
        return;
    }

    std::string name = it->second.name;
    _registry.erase(it);

    auto msg = name + " has been forgotten.";
    Settings::Notify(msg.c_str());

    logger::info("Horde: Forgot {:08X} ({})", formID, name);
}

std::vector<FollowerManager::DismissedInfo> FollowerManager::GetDismissedFollowers() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    std::vector<DismissedInfo> result;
    for (auto& [fid, entry] : _registry) {
        // Skip active followers — they're managed from their detail cards
        bool isActive = false;
        for (auto& f : _followers) {
            if (f.actorFormID == fid) { isActive = true; break; }
        }
        if (isActive) continue;

        result.push_back({fid, entry.name, entry.homeName, entry.homeWorldspace != 0});
    }
    return result;
}

void FollowerManager::UpdateFollowCloseLeash()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    constexpr float kLeashDistance = 3000.0f;   // ~42 meters — teleport threshold
    constexpr float kBehindDist   = 250.0f;     // same offset as Summon

    auto playerPos = player->GetPosition();
    float heading  = player->GetAngleZ();

    for (auto& f : _followers) {
        if (!f.isFollowClose || f.isWaiting) continue;

        auto* actor = ResolveActor(f.actorFormID);
        if (!actor || actor->IsDead()) continue;

        float dist = actor->GetPosition().GetDistance(playerPos);
        if (dist < kLeashDistance) continue;

        // Teleport behind the player — same logic as Summon
        actor->MoveTo(player);
        RE::NiPoint3 behind{
            playerPos.x - kBehindDist * std::sin(heading),
            playerPos.y - kBehindDist * std::cos(heading),
            playerPos.z
        };
        actor->SetPosition(behind, true);
        logger::info("Horde: Leash teleported {} ({:08X}), was {} units away",
            f.name, f.actorFormID, static_cast<int>(dist));
    }
}

void FollowerManager::TeleportStrandedFollowers()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto* playerCell = player->GetParentCell();
    if (!playerCell) return;

    auto* playerWorld = player->GetWorldspace();

    // NOTE: sandboxing followers are deliberately NOT skipped here. They are the
    // ones most likely to be stranded — a follower wandering 1024 units from the
    // player misses the engine's teammate hand-off when the player uses a load
    // door. Callers suspend sandbox before invoking this.
    for (auto& f : _followers) {
        if (f.isWaiting) continue;

        auto* actor = ResolveActor(f.actorFormID);
        if (!actor || actor->IsDead()) continue;

        auto* actorCell = actor->GetParentCell();
        if (actorCell == playerCell) continue;

        // Only teleport if the follower is in a different worldspace/cell type.
        // Same exterior worldspace = follower can pathfind on their own.
        auto* actorWorld = actor->GetWorldspace();
        if (playerWorld && actorWorld && playerWorld == actorWorld) continue;

        // Different worldspace or interior/exterior mismatch — teleport to player
        actor->MoveTo(player);

        logger::info("Horde: Stranded follower {} — teleported to player's cell", f.name);
    }
}
