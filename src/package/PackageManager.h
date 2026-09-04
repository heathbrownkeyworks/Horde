#pragma once
#include "pch.h"
#include <chrono>
#include <unordered_set>

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
    bool HasDialogueFollowerAlias(RE::Actor* actor) const;
    void ClearDialogueFollowerAlias(RE::Actor* actor);

    // Clear only the vanilla DialogueFollower alias when Horde needs package control.
    // Other quest aliases can carry unrelated packages and must be left alone.
    void ClearCompetingAliasPackages(RE::Actor* actor);

    // --- Sandbox (Idle System) ---
    // Injects/removes sandbox package at position 0 of the actor's Horde alias
    void ApplySandbox(RE::Actor* actor, bool silent = false);
    void RemoveSandbox(RE::Actor* actor, bool silent = false);
    bool HasSandbox(RE::Actor* actor) const;

    // Drop every active sandbox immediately and reset the idle timer.
    // Used on cell transitions so stranded-follower recovery sees accurate state.
    void SuspendSandbox();

    // Forget an actor's sandbox bookkeeping without needing a live Actor*.
    // Must be called on every untrack path — otherwise a FormID whose actor
    // failed to resolve leaks into _sandboxActors and permanently pins the
    // shared idle marker (the marker only repositions when the set is empty).
    void ForgetSandboxActor(RE::FormID formID);

    // Serana-style idle sandbox system (called from the polling thread)
    void UpdateIdleSandbox();

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

    // Tear down and recreate the actor's Horde alias instance with a fresh package array.
    // The engine caches instanced packages when the alias is first added to
    // ExtraAliasInstanceArray, so in-place array edits are invisible to the
    // package evaluator.  Rebuilding the instance forces a cache refresh.
    void RebuildHordeAliasInstance(RE::Actor* actor, bool includeSandboxActive);

    RE::TESQuest* _hordeQuest = nullptr;
    bool _questCached = false;

    // Per-actor sandbox tracking
    std::unordered_set<RE::FormID> _sandboxActors;

    // Player idle tracking
    RE::NiPoint3 _lastPlayerPos{0, 0, 0};
    float _idleTimer = 0.0f;
    bool _followersAreSandboxing = false;

    // Wall-clock stamp of the previous UpdateIdleSandbox() call, so _idleTimer
    // accumulates real seconds instead of assuming a fixed poll interval.
    std::chrono::steady_clock::time_point _lastIdleTick{};
    bool _idleTickValid = false;

    static constexpr float kIdleThreshold = 5.0f;
    static constexpr float kMovementThreshold = 50.0f;
    static constexpr int kMaxSlots = 20;
};
