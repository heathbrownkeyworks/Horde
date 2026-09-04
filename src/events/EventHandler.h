#pragma once
#include "pch.h"
#include <mutex>
#include <unordered_map>

class EventHandler :
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
    public RE::BSTEventSink<RE::TESActivateEvent>,
    public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
    public RE::BSTEventSink<RE::TESHitEvent>,
    public RE::BSTEventSink<RE::TESCombatEvent>,
    public RE::BSTEventSink<RE::TESSpellCastEvent>
{
public:
    static EventHandler& GetSingleton();
    static void Register();

    void StartPolling();
    void StopPolling();

private:
    EventHandler() = default;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESActivateEvent* event,
        RE::BSTEventSource<RE::TESActivateEvent>*) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellFullyLoadedEvent* event,
        RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent* event,
        RE::BSTEventSource<RE::TESHitEvent>*) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCombatEvent* event,
        RE::BSTEventSource<RE::TESCombatEvent>*) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESSpellCastEvent* event,
        RE::BSTEventSource<RE::TESSpellCastEvent>*) override;

    // Open a short window during which the existing poll thread ticks quickly,
    // instead of spawning a detached thread per delay step. syncDialogue routes
    // the fast ticks through OnDialogueClose() so vanilla wait/follow commands
    // are picked up as well as new recruits.
    void RequestFastScan(int windowMs, bool syncDialogue);

    // Health baseline for intra-team friendly fire, so a blocked hit restores
    // only the damage actually taken rather than healing the target to full.
    float ConsumeHealthDeficit(RE::Actor* actor);
    void  RefreshHealthBaseline(RE::Actor* actor);

    // Combat/package state must never be mutated from inside combat event
    // dispatch — doing so re-enters the same event on the same stack. These
    // queue the work onto the game thread instead, coalescing event storms.
    void RequestTeamStandDown();

    std::atomic<bool> _polling{false};
    std::atomic<bool> _standDownPending{false};
    std::atomic<int64_t> _lastCellLoadScan{0};  // debounce cell load scans (ms since epoch)
    std::atomic<int64_t> _fastScanUntil{0};     // ms since epoch; 0 = normal cadence
    std::atomic<bool> _fastScanSyncDialogue{false};

    std::unordered_map<RE::FormID, float> _healthBaseline;
    std::mutex _healthBaselineMutex;

public:
    // Refresh the friendly-fire health baseline for the player and every tracked
    // follower. Called from the poll tick so damage from non-hit sources (falls,
    // traps, poison) cannot accumulate into an over-heal.
    void RefreshHealthBaselines();

    // Pull every passive follower out of combat. Runs on the game thread only —
    // queued from the combat event sink, and also called from the poll tick as a
    // backstop in case a combat event was missed.
    void RunTeamStandDown();
};
