#pragma once
#include "pch.h"

class PackageManager
{
public:
    static PackageManager& GetSingleton();

    // --- Horde Quest / Alias Management ---
    void InitQuestCache();

    // Alias slot operations
    int  FindEmptySlot();
    void FillSlot(int slot, RE::Actor* actor);
    void ClearSlot(int slot);
    bool IsSlotFilled(int slot);

    // --- DialogueFollower Cleanup ---
    // Preserve other quests' aliases and packages.
    bool HasDialogueFollowerAlias(RE::Actor* actor) const;
    bool HasAnimalAlias(RE::Actor* actor) const;
    void ReleaseAnimal(RE::Actor* actor);
    void ClearDialogueFollowerAlias(RE::Actor* actor);

    // Residence quest priority 1; independent of the twenty active follower slots.
    bool EnsureResidencePackage(RE::Actor* actor);
    bool ClearResidencePackage(RE::Actor* actor);

    // Engine conditions choose sandbox/wait/follow from a fixed alias stack.
    // Mirror saved per-follower preferences into an opt-in faction and global.
    // Call on the game thread after recruitment, load, settings, and dismissal.
    void SyncSandboxSettings();
    void ClearSandboxState(RE::Actor* actor);

    // Remove any leftover dismissed sandbox alias instances (called on re-recruit).
    // Retained to clean stale instances written by pre-1.8 saves.
    void ClearDismissedSandbox(RE::Actor* actor);

    // Apply follow distance preset to Horde_FollowPkg data keys
    void ApplyFollowDistance(const std::string& preset);

    // Reset on cosave revert
    void Reset();

private:
    PackageManager() = default;

    RE::TESQuest* GetHordeQuest();
    RE::BGSBaseAlias* GetAlias(int slot);

    RE::TESQuest* _hordeQuest = nullptr;
    RE::TESQuest* _residenceQuest = nullptr;
    RE::TESPackage* _residencePackage = nullptr;
    RE::TESGlobal* _sandboxEnabled = nullptr;
    RE::TESFaction* _sandboxFaction = nullptr;
    bool _questCached = false;
    static constexpr int kMaxSlots = 20;
};
