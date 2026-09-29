#pragma once
#include "pch.h"
#include "follower/FollowerData.h"
#include <unordered_set>

class FollowerManager
{
public:
    static FollowerManager& GetSingleton();

    void Initialize();

    // Follower state is persisted by SKSE on game save.
    void OnCosaveRevert();
    void OnCosaveSave(SKSE::SerializationInterface* a_intfc);
    void OnCosaveLoad(SKSE::SerializationInterface* a_intfc);

    // Detection
    void ScanForFollowers();
    void OnDialogueClose();

    // Follower operations
    // Recruitment happens only through ScanForFollowers, which adopts actors
    // vanilla DialogueFollowerScript.SetFollower has already promoted.
    bool UntrackFollower(RE::FormID formID);

    // Bulk operations
    void SummonAll();
    void FollowAll();
    void WaitAll();
    // Stop combat for the party until passive mode is disabled.
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

    // Teleport distant followers with Follow Close enabled.
    void UpdateFollowCloseLeash();

    // Recover followers stranded across a cell transition.
    void TeleportStrandedFollowers();

    // State
    const std::vector<FollowerData>& GetFollowers() const { return _followers; }
    int GetCount() const { return static_cast<int>(_followers.size()); }
    bool IsAtCap() const;
    bool IsTracked(RE::FormID formID) const;
    bool IsWaiting(RE::FormID formID) const;
    bool IsPassive(RE::FormID formID) const;
    // False for an empty party.
    bool IsAllPassive() const;

    // Global gate
    void UpdateDialogueGate();

    // State recovery (called after cosave load)
    void OnPostLoadGame();
    void RefreshHomes();

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

    // Essential flags belong to the shared actor base, not individual references.
    static void ApplyProtection(RE::TESNPC* actorBase, int protection);
    void ForceEssential(RE::Actor* actor);
    // Reuse the captured protection when another follower shares this base.
    int  CaptureOriginalProtection(RE::Actor* actor, RE::TESNPC* actorBase) const;
    // Restore protection only when no other follower needs the shared override.
    void ReleaseEssential(RE::Actor* actor, RE::FormID formID, int originalProtection);
    // Release shared form overrides before loading another save.
    void ReleaseAllEssential();

    std::vector<FollowerData> _followers;
    std::unordered_set<RE::FormID> _rejectedCustomFollowers;  // skip list for custom follower systems
    mutable std::recursive_mutex _mutex;

    // Active and dismissed followers, serialized in the RGST cosave record.
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

    // Capture once, before a home assignment can override the editor location.
    void CaptureOriginalEditorLoc(RE::FormID formID, RE::Actor* actor);
};
