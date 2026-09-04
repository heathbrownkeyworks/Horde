#pragma once
#include "pch.h"
#include "follower/FollowerData.h"
#include <unordered_set>

class FollowerManager
{
public:
    static FollowerManager& GetSingleton();

    void Initialize();
    void Save() const {}  // no-op — SKSE cosave handles persistence on game save

    // SKSE cosave — per-save-game follower persistence
    void OnCosaveRevert();
    void OnCosaveSave(SKSE::SerializationInterface* a_intfc);
    void OnCosaveLoad(SKSE::SerializationInterface* a_intfc);

    // Detection
    void ScanForFollowers();
    void OnDialogueClose();

    // Follower operations
    bool TrackFollower(RE::Actor* actor);
    bool UntrackFollower(RE::FormID formID);

    // Bulk operations
    void SummonAll();
    void FollowAll();
    void WaitAll();
    // Group stand-down toggle: drops every follower out of combat and stops them
    // re-engaging until switched back to aggressive.
    void SetPassiveAll(bool passive);

    // Per-follower operations
    void Summon(RE::FormID formID, bool silent = false);
    void SetFollow(RE::FormID formID, bool silent = false);
    void SetWait(RE::FormID formID, bool silent = false);
    void SetPassive(RE::FormID formID, bool passive, bool silent = false);
    void SetSandbox(RE::FormID formID, bool enabled);
    void SetFollowClose(RE::FormID formID, bool enabled);
    void SetEssential(RE::FormID formID, bool essential);
    void SetHome(RE::FormID formID);
    void ClearHome(RE::FormID formID);

    // Dismissed follower operations (operate on registry, not active followers)
    void SummonDismissed(RE::FormID formID);
    void SetDismissedHome(RE::FormID formID);
    void ClearDismissedHome(RE::FormID formID);
    void ForgetFollower(RE::FormID formID);

    // Registry queries
    struct DismissedInfo {
        RE::FormID  formID;
        std::string name;
        std::string homeName;
        bool        hasHome;
    };
    std::vector<DismissedInfo> GetDismissedFollowers() const;

    // Distance leash — teleports followers with FollowClose enabled
    void UpdateFollowCloseLeash();

    // Cell transition — teleports stranded followers to the player
    void TeleportStrandedFollowers();

    // State
    const std::vector<FollowerData>& GetFollowers() const { return _followers; }
    int GetCount() const { return static_cast<int>(_followers.size()); }
    bool IsAtCap() const;
    bool IsTracked(RE::FormID formID) const;
    bool IsWaiting(RE::FormID formID) const;
    bool IsPassive(RE::FormID formID) const;
    // True when there is at least one follower and every one of them is passive.
    // Drives the Passive/Aggressive toggle label in the UI.
    bool IsAllPassive() const;

    // Global gate
    void UpdateDialogueGate();

    // State recovery (called after cosave load)
    void OnPostLoadGame();

    // Cosave record types
    static constexpr std::uint32_t kFollowerRecord = 'FLWR';
    static constexpr std::uint32_t kRegistryRecord = 'RGST';
    static constexpr std::uint32_t kCosaveVersion = 1;

private:
    FollowerManager() = default;

    RE::TESFaction* GetCurrentFollowerFaction() const;
    RE::TESFaction* GetHordeFollowerFaction() const;
    RE::TESGlobal* GetPlayerFollowerCount() const;
    RE::Actor* ResolveActor(RE::FormID formID) const;

    FollowerData* FindFollower(RE::FormID formID);
    void SoftUntrack(RE::FormID formID);  // cleanup for externally-dismissed followers
    void ApplyPassive(RE::Actor* actor, bool passive, FollowerData& data);

    // Essential is a TESNPC (actor base) flag, not a per-reference one, so two
    // followers sharing a base share the flag. These helpers keep the shared
    // form consistent and make sure Horde's override is always unwound.
    static void ApplyProtection(RE::TESNPC* actorBase, int protection);
    void ForceEssential(RE::Actor* actor);
    // Reads the actor's true protection level. If another tracked follower
    // already shares this actor base, Horde has already forced essential on it,
    // so the live flags are not the original — inherit the recorded value.
    int  CaptureOriginalProtection(RE::Actor* actor, RE::TESNPC* actorBase) const;
    // Restores originalProtection unless another still-tracked follower shares
    // the same actor base and still wants Horde's essential override.
    void ReleaseEssential(RE::Actor* actor, RE::FormID formID, int originalProtection);
    // Unwind every live essential override. Called on cosave revert so the
    // previous save's followers do not stay essential in the shared form pool
    // when a different save is loaded.
    void ReleaseAllEssential();

    std::vector<FollowerData> _followers;
    std::unordered_set<RE::FormID> _rejectedCustomFollowers;  // skip list for custom follower systems
    mutable std::recursive_mutex _mutex;

    // Persistent registry — remembers every follower who has ever been in Horde.
    // Replaces the old _homeCache. Serialized as its own cosave record ('RGST').
    std::unordered_map<RE::FormID, RegistryEntry> _registry;

    void UpsertRegistry(RE::FormID formID, const std::string& name);
    void SyncRegistryHome(const FollowerData& data);
    void RestoreHome(FollowerData& data);
    bool ApplyHomeEditorLocation(
        RE::Actor* actor,
        RE::FormID homeFormID,
        float x,
        float y,
        float z,
        const std::string& homeName);
    bool RestoreOriginalEditorLocation(RE::Actor* actor, const RegistryEntry& entry, RE::FormID formID);

    // Capture the actor's current vanilla editor location into the registry
    // entry the first time we see them. No-op if already captured.
    void CaptureOriginalEditorLoc(RE::FormID formID, RE::Actor* actor);
};
