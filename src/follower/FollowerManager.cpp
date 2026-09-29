#include "pch.h"
#include "follower/FollowerManager.h"
#include "package/PackageManager.h"
#include "Settings.h"
#include "follower/CosaveReader.h"

#include <cmath>
#include <charconv>
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

// Essential flags are shared by all references to a TESNPC and survive save loads.

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

    // A shared base may already carry Horde's override; reuse its saved protection.
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
            logger::info("Horde: Kept essential on base {:08X} - still shared with {}",
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

    // Restore shared actor bases before discarding the previous save's state.
    ReleaseAllEssential();

    for (const auto& [id, entry] : _registry) {
        if (auto* actor = ResolveActor(id)) {
            PackageManager::GetSingleton().ClearResidencePackage(actor);
            if (entry.homeRestorePending) RestoreOriginalEditorLocation(actor, entry, id);
        }
    }
    PackageManager::GetSingleton().Reset();
    _followers.clear();
    _registry.clear();
    _rejectedCustomFollowers.clear();
    logger::info("Horde: Cosave revert - essential overrides released, follower list and registry cleared");
}

void FollowerManager::OnCosaveSave(SKSE::SerializationInterface* a_intfc)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // Record 1: follower list
    {
        nlohmann::json j = _followers;
        std::string data = j.dump();
        if (data.size() > Horde::kMaxCosavePayload) {
            logger::error("Horde: Cosave payload exceeds the supported size");
            return;
        }

        if (!a_intfc->OpenRecord(kFollowerRecord, kCosaveVersion)) {
            logger::error("Horde: Failed to open FLWR cosave record");
            return;
        }

        std::uint32_t len = static_cast<std::uint32_t>(data.size());
        if (!a_intfc->WriteRecordData(len) || !a_intfc->WriteRecordData(data.c_str(), len)) {
            logger::error("Horde: Failed to write complete cosave record");
            return;
        }

        logger::info("Horde: Cosave saved {} followers ({} bytes)", _followers.size(), len);
    }

    // Record 2: follower registry
    {
        // Serialize as {"formID": RegistryEntry, ...} with string keys
        nlohmann::json j = nlohmann::json::object();
        for (auto& [fid, entry] : _registry) {
            j[std::to_string(fid)] = entry;
        }
        std::string data = j.dump();
        if (data.size() > Horde::kMaxCosavePayload) {
            logger::error("Horde: Cosave payload exceeds the supported size");
            return;
        }

        if (!a_intfc->OpenRecord(kRegistryRecord, kCosaveVersion)) {
            logger::error("Horde: Failed to open RGST cosave record");
            return;
        }

        std::uint32_t len = static_cast<std::uint32_t>(data.size());
        if (!a_intfc->WriteRecordData(len) || !a_intfc->WriteRecordData(data.c_str(), len)) {
            logger::error("Horde: Failed to write complete cosave record");
            return;
        }

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

        if (type != kFollowerRecord && type != kRegistryRecord) continue;
        std::optional<std::string> payload;
        try {
            payload = Horde::ReadCosavePayload(*a_intfc, length);
        } catch (const std::exception& e) {
            logger::error("Horde: Cannot read cosave record: {}", e.what());
            continue;
        }
        if (!payload) {
            logger::warn("Horde: Skipping truncated or oversized cosave record {:08X}", type);
            continue;
        }
        const auto& data = *payload;

        if (type == kFollowerRecord) {
            try {
                nlohmann::json j = nlohmann::json::parse(data);

                // Decode entry by entry so a single bad record costs one
                // follower instead of the entire roster.
                if (!j.is_array()) {
                    logger::error("Horde: FLWR cosave payload is not an array");
                    continue;
                }
                std::vector<FollowerData> loadedFollowers;
                std::uint32_t skipped = 0;
                for (const auto& entry : j) {
                    try {
                        loadedFollowers.push_back(entry.get<FollowerData>());
                    } catch (const std::exception& e) {
                        logger::warn("Horde: Skipping unreadable follower record: {}", e.what());
                        skipped++;
                    }
                }

                for (auto& f : loadedFollowers) {
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

                auto before = loadedFollowers.size();
                loadedFollowers.erase(
                    std::remove_if(loadedFollowers.begin(), loadedFollowers.end(),
                        [](const FollowerData& f) { return f.actorFormID == 0; }),
                    loadedFollowers.end());

                if (loadedFollowers.size() < before) {
                    logger::warn("Horde: Removed {} followers with unresolvable FormIDs", before - loadedFollowers.size());
                }

                std::unordered_set<RE::FormID> seenActors;
                std::unordered_set<int> seenSlots;
                std::erase_if(loadedFollowers, [&](const FollowerData& f) { return !seenActors.insert(f.actorFormID).second; });
                for (auto& f : loadedFollowers) {
                    if (f.aliasSlot < 0 || f.aliasSlot >= 20 || !seenSlots.insert(f.aliasSlot).second) f.aliasSlot = -1;
                }
                // Reserve valid saved slots before assigning replacements, so a
                // repaired entry cannot steal a later follower's valid slot.
                for (auto& f : loadedFollowers) {
                    if (f.aliasSlot >= 0) continue;
                    for (int slot = 0; slot < 20; ++slot) {
                        if (seenSlots.insert(slot).second) { f.aliasSlot = slot; break; }
                    }
                }
                _followers = std::move(loadedFollowers);
                logger::info("Horde: Cosave loaded {} followers ({} unreadable entries dropped)",
                    _followers.size(), skipped);
            } catch (const std::exception& e) {
                logger::error("Horde: Failed to parse FLWR cosave: {}", e.what());
            }
        } else if (type == kRegistryRecord) {
            try {
                nlohmann::json j = nlohmann::json::parse(data);
                if (!j.is_object()) {
                    logger::error("Horde: RGST cosave payload is not an object");
                    continue;
                }
                std::unordered_map<RE::FormID, RegistryEntry> loadedRegistry;

                for (auto& [key, val] : j.items()) {
                    // Recover valid entries independently of malformed siblings.
                    RE::FormID oldID = 0;
                    const auto result = std::from_chars(key.data(), key.data() + key.size(), oldID);
                    if (result.ec != std::errc{} || result.ptr != key.data() + key.size() || oldID == 0) {
                        logger::warn("Horde: Registry has invalid FormID key '{}', dropping", key);
                        continue;
                    }

                    RE::FormID resolved = 0;
                    if (!a_intfc->ResolveFormID(oldID, resolved)) {
                        logger::warn("Horde: Registry - failed to resolve {:08X}, dropping", oldID);
                        continue;
                    }

                    RegistryEntry entry;
                    try {
                        entry = val.get<RegistryEntry>();
                    } catch (const std::exception& e) {
                        logger::warn("Horde: Registry - unreadable entry for {:08X}: {}", oldID, e.what());
                        continue;
                    }

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
                            // Original editor loc form no longer exists - drop it.
                            // Keep the capture flag so the current Horde home
                            // cannot become the original on re-recruitment.
                            entry.originalEditorLocFormID = 0;
                            entry.originalEditorLocX = entry.originalEditorLocY = entry.originalEditorLocZ = 0.0f;
                            entry.originalEditorLocRot = 0.0f;
                        }
                    }

                    loadedRegistry[resolved] = std::move(entry);
                }

                _registry = std::move(loadedRegistry);
                logger::info("Horde: Cosave loaded {} registry entries", _registry.size());
            } catch (const std::exception& e) {
                logger::error("Horde: Failed to parse RGST cosave: {}", e.what());
            }
        } else {
            logger::warn("Horde: Unknown cosave record type {:08X}", type);
        }
    }
}

void FollowerManager::Initialize()
{
    logger::info("Horde: FollowerManager initialized");
}

bool FollowerManager::IsAtCap() const
{
    return GetCount() >= Settings::GetSingleton().GetMaxFollowers();
}

void FollowerManager::UpdateDialogueGate()
{
    PackageManager::GetSingleton().SyncSandboxSettings();
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
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, it->originalAggression);

        auto* actorBase = actor->GetActorBase();
        ReleaseEssential(actor, formID, it->originalProtection);

        auto* faction = GetCurrentFollowerFaction();
        if (faction) {
            actor->AddToFaction(faction, -1);
        }
        // Match vanilla dismissal so dialogue gated by DismissedFollowerFaction reappears.
        if (auto* dismissedFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("DismissedFollowerFaction")) {
            if (!actor->IsInFaction(dismissedFaction)) {
                actor->AddToFaction(dismissedFaction, 0);
            }
        }
        auto* hordeFaction = GetHordeFollowerFaction();
        if (hordeFaction && actor->IsInFaction(hordeFaction)) {
            actor->RemoveFromFaction(hordeFaction);
        }
        PackageManager::GetSingleton().ClearSandboxState(actor);

        actor->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);

        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 0.0f);

        // Release the animal gate before removing the alias that identifies it.
        auto& packages = PackageManager::GetSingleton();
        if (it->isAnimal || packages.HasAnimalAlias(actor)) packages.ReleaseAnimal(actor);

        PackageManager::GetSingleton().ClearSlot(it->aliasSlot);

        // Clearing faction and teammate state does not remove vanilla alias packages.
        PackageManager::GetSingleton().ClearDialogueFollowerAlias(actor);
        PackageManager::GetSingleton().ClearDismissedSandbox(actor);

        actor->EvaluatePackage(true, false);

        auto msg = std::string(it->name) + " has been dismissed from your service.";
        Settings::Notify(msg.c_str());

        logger::info("Horde: Dismissed {:08X} from player service", formID);

        // Clear hireling employment and preserve eligibility for rehire.
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

    std::string name = it->name;
    _followers.erase(it);
    RefreshHomes();

    logger::info("Horde: Untracked follower {} ({:08X}), total: {}", name, formID, _followers.size());

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

        ReleaseEssential(actor, formID, it->originalProtection);

        PackageManager::GetSingleton().ClearSandboxState(actor);
        auto* hordeFaction = GetHordeFollowerFaction();
        if (hordeFaction && actor->IsInFaction(hordeFaction)) {
            actor->RemoveFromFaction(hordeFaction);
        }
    }

    // Clear alias slot
    if (it->aliasSlot >= 0) {
        PackageManager::GetSingleton().ClearSlot(it->aliasSlot);
    }

    std::string name = it->name;
    _followers.erase(it);
    RefreshHomes();

    logger::info("Horde: Soft-untracked {} ({:08X}) - dismissed via dialogue, total: {}",
        name, formID, _followers.size());
}

void FollowerManager::ApplyPassive(RE::Actor* actor, bool passive, FollowerData& data)
{
    if (passive) {
        if (!data.isPassive) {
            data.originalAggression = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);
        }
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 0.0f);

        // Zero aggression does not end combat already in progress.
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
    // A summoned waiting follower must use the new position as its wait center.
    actor->extraList.RemoveByType(RE::ExtraDataType::kPackageStartLocation);
    actor->EvaluatePackage(true, false);

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
        Summon(f.actorFormID, true);  // silent - single group notification below
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
        // Reset the old wait center when switching back to follow.
        actor->extraList.RemoveByType(RE::ExtraDataType::kPackageStartLocation);
        actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kWaitingForPlayer, 0.0f);
        actor->EvaluatePackage(true, false);
    }

    if (!silent) {
        auto msg = data->name + " is following you.";
        Settings::Notify(msg.c_str());
    }

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
        // Anchor a fresh waiting package at this follower's current position.
        actor->extraList.RemoveByType(RE::ExtraDataType::kPackageStartLocation);
        actor->EvaluatePackage(true, false);
    }

    if (!silent) {
        // The engine selects safe-location wait sandbox only for opted-in actors;
        // other followers use the existing hold-position wait package.
        auto msg = data->name + " is now waiting.";
        Settings::Notify(msg.c_str());
    }

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
        // Preserve the preference for post-load recovery.
        data->isPassive = passive;
        logger::warn("Horde: SetPassive - actor {:08X} not loaded, state recorded only", formID);
        return;
    }

    ApplyPassive(actor, passive, *data);
    data->isPassive = passive;

    if (!silent) {
        auto msg = data->name + (passive ? " is now passive." : " is no longer passive.");
        Settings::Notify(msg.c_str());
    }

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
        SetPassive(id, passive, true);  // silent - single group notification below
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
        actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
    } else {
        // Restore original protection unless another follower needs the shared override.
        ReleaseEssential(actor, formID, data->originalProtection);
    }

    auto msg = data->name + (essential
        ? " is now essential and cannot be killed."
        : " is no longer essential and can be killed.");
    Settings::Notify(msg.c_str());

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

    PackageManager::GetSingleton().SyncSandboxSettings();

    auto msg = data->name + (enabled ? " will sandbox in safe locations." : " will no longer sandbox.");
    Settings::Notify(msg.c_str());

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
        SetFollow(id, true);  // silent - single group notification below
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
        SetWait(id, true);  // silent - single group notification below
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

    std::set<RE::FormID> detectedIDs;
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
        if (FindFollower(id)) continue;

        auto* actor = ResolveActor(id);
        if (!actor) continue;

        // Only adopt followers recruited through the vanilla DialogueFollower quest.
        if (!pkgMgr.HasDialogueFollowerAlias(actor)) {
            if (_rejectedCustomFollowers.insert(id).second) {
                logger::info("Horde: Skipping {} ({:08X}) - custom follower system",
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
        data.isAnimal = pkgMgr.HasAnimalAlias(actor);

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
        newFollowerDetectedThisScan = true;
        UpsertRegistry(id, data.name);
        CaptureOriginalEditorLoc(id, actor);
        RestoreHome(_followers.back());
        pkgMgr.ClearResidencePackage(actor);
        auto& home = _registry.at(id);
        if (home.homeRestorePending && RestoreOriginalEditorLocation(actor, home, id)) {
            home.homeRestorePending = false;
        }

        pkgMgr.ClearDismissedSandbox(actor);
        pkgMgr.FillSlot(slot, actor);

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

    // Vanilla recruitment dismisses the previous follower. Preserve that follower
    // only if this scan accepted a new recruit; otherwise honor external dismissal.
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

        // Rank 1 suppresses vanilla management dialogue through INFO conditions.
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

    UpdateDialogueGate();
    RefreshHomes();
    logger::trace("Horde: Scan complete, {} followers tracked", _followers.size());
}

void FollowerManager::OnDialogueClose()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // Adopt wait/follow changes made by vanilla dialogue before repairing follower state.
    for (auto& f : _followers) {
        auto* actor = ResolveActor(f.actorFormID);
        if (!actor) continue;

        float currentWait = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer);
        bool vanillaWaiting = currentWait >= 1.0f;

        if (vanillaWaiting != f.isWaiting) {
            f.isWaiting = vanillaWaiting;

            actor->extraList.RemoveByType(RE::ExtraDataType::kPackageStartLocation);
            actor->EvaluatePackage(true, false);

            auto msg = f.name + (vanillaWaiting ? " is now waiting." : " is following you.");
            Settings::Notify(msg.c_str());

            logger::info("Horde: Synced {} to {} (vanilla dialogue)",
                f.name, vanillaWaiting ? "waiting" : "following");
        }
    }

    ScanForFollowers();
}

void FollowerManager::OnPostLoadGame()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto* faction = GetCurrentFollowerFaction();
    auto& pkgMgr = PackageManager::GetSingleton();

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

        // Re-fill alias slot - ForceRefTo aliases are runtime-only, not persisted
        if (it->aliasSlot >= 0) {
            pkgMgr.FillSlot(it->aliasSlot, actor);
        } else {
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

        if (it->isPassive) {
            ApplyPassive(actor, true, *it);
        }

        // Re-apply essential override (user may have toggled it off pre-save)
        if (auto* actorBase = actor->GetActorBase()) {
            if (it->isEssential) {
                actorBase->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
            } else {
                ReleaseEssential(actor, it->actorFormID, it->originalProtection);
            }
        }

        actor->AsActorValueOwner()->SetActorValue(
            RE::ActorValue::kWaitingForPlayer, it->isWaiting ? 1.0f : 0.0f);

        it->isAnimal = it->isAnimal || pkgMgr.HasAnimalAlias(actor);

        // Ensure follower exists in registry (backfill for saves made before registry existed)
        UpsertRegistry(it->actorFormID, it->name);
        SyncRegistryHome(*it);
        CaptureOriginalEditorLoc(it->actorFormID, actor);

        actor->EvaluatePackage(true, false);

        recovered++;
        ++it;
    }

    RefreshHomes();

    // Apply saved follow distance preset to package data
    pkgMgr.ApplyFollowDistance(Settings::GetSingleton().GetFollowDistance());

    UpdateDialogueGate();

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

    // Store the destination now; apply it only after dismissal.
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

    if (auto* actor = ResolveActor(formID)) CaptureOriginalEditorLoc(formID, actor);
    SyncRegistryHome(*data);
    RefreshHomes();

    auto msg = data->name + "'s home has been set to " + data->homeName + ".";
    Settings::Notify(msg.c_str());

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

    RefreshHomes();

    auto msg = data->name + " no longer has an assigned home.";
    Settings::Notify(msg.c_str());

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

    // Never replace the original location with an assigned Horde home.
    if (it->second.originalEditorLocCaptured) return;

    auto& rt = actor->GetActorRuntimeData();
    it->second.originalEditorLocCaptured = true;
    it->second.originalEditorLocFormID = rt.editorLocForm ? rt.editorLocForm->GetFormID() : 0;
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
    if (!actor || homeFormID == 0 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;

    auto* homeForm = RE::TESForm::LookupByID(homeFormID);
    if (!homeForm || (!homeForm->As<RE::TESWorldSpace>() && !homeForm->As<RE::TESObjectCELL>())) {
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
    if (!actor || !entry.originalEditorLocCaptured) return false;

    auto* origForm = entry.originalEditorLocFormID ? RE::TESForm::LookupByID(entry.originalEditorLocFormID) : nullptr;
    if (entry.originalEditorLocFormID && !origForm) {
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
        logger::warn("Horde: SummonDismissed - {:08X} not in registry", formID);
        return;
    }

    if (FindFollower(formID)) {
        logger::warn("Horde: SummonDismissed - {:08X} is active, use Summon instead", formID);
        return;
    }

    auto* actor = ResolveActor(formID);
    if (!actor) {
        Settings::Notify("Cannot summon - follower is not loaded.");
        logger::warn("Horde: SummonDismissed - {:08X} not loaded", formID);
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
    if (FindFollower(formID)) { SetHome(formID); return; }

    auto it = _registry.find(formID);
    if (it == _registry.end()) {
        logger::warn("Horde: SetDismissedHome - {:08X} not in registry", formID);
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

    RefreshHomes();

    auto msg = it->second.name + "'s home has been set to " + it->second.homeName + ".";
    Settings::Notify(msg.c_str());

    logger::info("Horde: Dismissed {:08X} home set to {}", formID, it->second.homeName);
}

void FollowerManager::ClearDismissedHome(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (FindFollower(formID)) { ClearHome(formID); return; }

    auto it = _registry.find(formID);
    if (it == _registry.end()) {
        logger::warn("Horde: ClearDismissedHome - {:08X} not in registry", formID);
        return;
    }

    it->second.homeWorldspace = 0;
    it->second.homeX = it->second.homeY = it->second.homeZ = 0.0f;
    it->second.homeName.clear();
    RefreshHomes();

    auto msg = it->second.name + " no longer has an assigned home.";
    Settings::Notify(msg.c_str());

    logger::info("Horde: Dismissed {:08X} home cleared", formID);
}

void FollowerManager::ForgetFollower(RE::FormID formID)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    auto it = _registry.find(formID);
    if (it == _registry.end()) return;

    if (FindFollower(formID)) {
        logger::warn("Horde: ForgetFollower - {:08X} is active, cannot forget", formID);
        return;
    }

    std::string name = it->second.name;

    auto* actor = ResolveActor(formID);
    if (it->second.homeRestorePending &&
        (!actor || !RestoreOriginalEditorLocation(actor, it->second, formID))) {
        Settings::Notify("Horde: This follower's home cannot be restored yet. Try again when they are available.");
        return;
    }
    if (actor) {
        PackageManager::GetSingleton().ClearResidencePackage(actor);
        PackageManager::GetSingleton().ClearDismissedSandbox(actor);
        actor->EvaluatePackage(true, false);
    }

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
        // Skip active followers - they're managed from their detail cards
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

    constexpr float kLeashDistance = 3000.0f;   // Teleport threshold in game units.
    constexpr float kBehindDist   = 250.0f;     // same offset as Summon

    auto playerPos = player->GetPosition();
    float heading  = player->GetAngleZ();

    for (auto& f : _followers) {
        if (!f.isFollowClose || f.isWaiting) continue;

        auto* actor = ResolveActor(f.actorFormID);
        if (!actor || actor->IsDead()) continue;

        float dist = actor->GetPosition().GetDistance(playerPos);
        if (dist < kLeashDistance) continue;

        // Teleport behind the player - same logic as Summon
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

    // Sandboxing followers still need recovery when they miss the teammate
    // hand-off through a load door. Only waiting followers stay behind.
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

        // Different worldspace or interior/exterior mismatch - teleport to player
        actor->MoveTo(player);

        logger::info("Horde: Stranded follower {} - teleported to player's cell", f.name);
    }
}

void FollowerManager::RefreshHomes()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto& packages = PackageManager::GetSingleton();
    auto* faction = GetCurrentFollowerFaction();
    for (auto& [id, entry] : _registry) {
        auto* actor = ResolveActor(id);
        if (!actor) continue;  // Retry when the reference becomes available.
        packages.ClearDismissedSandbox(actor);
        const bool dismissed = !FindFollower(id) && !actor->IsPlayerTeammate() &&
            (!faction || !actor->IsInFaction(faction)) && !actor->IsDead();
        bool changed = false;
        if (entry.homeWorldspace && dismissed) {
            CaptureOriginalEditorLoc(id, actor);
            const auto& rt = actor->GetActorRuntimeData();
            const bool same = rt.editorLocForm && rt.editorLocForm->GetFormID() == entry.homeWorldspace &&
                rt.editorLocCoord.x == entry.homeX && rt.editorLocCoord.y == entry.homeY && rt.editorLocCoord.z == entry.homeZ;
            if (!same && !ApplyHomeEditorLocation(actor, entry.homeWorldspace,
                    entry.homeX, entry.homeY, entry.homeZ, entry.homeName)) continue;
            entry.homeRestorePending = true;
            changed = packages.EnsureResidencePackage(actor) || !same;
        } else {
            changed = packages.ClearResidencePackage(actor);
            if (entry.homeRestorePending && RestoreOriginalEditorLocation(actor, entry, id)) {
                entry.homeRestorePending = false;
                changed = true;
            }
        }
        if (changed && !actor->IsDead()) actor->EvaluatePackage(true, false);
    }
}
