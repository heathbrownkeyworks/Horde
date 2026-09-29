#pragma once
#include "pch.h"
#include <mutex>
#include "events/ScanWindow.h"

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
    void SuspendSession();
    void ResumeSession();
    std::uint64_t Session() const { return _session.load(); }
    bool IsCurrentSession(std::uint64_t session) const { return _session.load() == session; }
    bool ShouldBlockTeamDamage(RE::Actor* target, RE::Actor* attacker, float damage);

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

    // Temporarily poll faster; syncDialogue also adopts vanilla wait/follow changes.
    void RequestFastScan(int windowMs, bool syncDialogue);

    // Defer combat changes to avoid re-entering TESCombatEvent dispatch.
    void RequestTeamStandDown();

    std::atomic<bool> _polling{false};
    std::atomic<bool> _standDownPending{false};
    std::atomic<int64_t> _lastCellLoadScan{0};  // debounce cell load scans (ms since epoch)
    Horde::ScanWindow _scanWindow;
    std::atomic<std::uint64_t> _session{0};
    std::atomic<bool> _sessionReady{false};

public:
    // Game thread only; the poll tick also calls this to recover missed combat events.
    void RunTeamStandDown();
};
