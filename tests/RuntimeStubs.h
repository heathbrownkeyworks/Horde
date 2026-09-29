#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>
#include "events/ScanWindow.h"
#include "follower/CosaveReader.h"

namespace logger {
    template<class... T> void info(T&&...) {}
    template<class... T> void warn(T&&...) {}
    template<class... T> void error(T&&...) {}
    template<class... T> void trace(T&&...) {}
}
namespace RE {
    using FormID = std::uint32_t;
    struct NiPoint3 { float x=0, y=0, z=0; };
    struct TESForm {
        virtual ~TESForm() = default;
        FormID id = 0;
        FormID GetFormID() const { return id; }
        static inline std::unordered_map<FormID, TESForm*> forms;
        static inline std::unordered_map<std::string, TESForm*> editors;
        static TESForm* LookupByID(FormID id) {
            auto it = forms.find(id); return it == forms.end() ? nullptr : it->second;
        }
        template<class T> static T* LookupByID(FormID id) { return dynamic_cast<T*>(LookupByID(id)); }
        template<class T> static T* LookupByEditorID(const std::string& name) {
            auto it = editors.find(name); return it == editors.end() ? nullptr : dynamic_cast<T*>(it->second);
        }
        template<class T> T* As() { return dynamic_cast<T*>(this); }
    };
    struct TESFaction : TESForm {};
    struct TESGlobal : TESForm { float value = 0; };
    struct TESWorldSpace : TESForm {};
    struct TESObjectCELL : TESForm {};
    struct Location { const char* GetFullName() { return "Test home"; } };
    struct ACTOR_BASE_DATA { enum class Flag { kEssential=1, kProtected=2 }; };
    struct Flags {
        unsigned value=0;
        template<class T> void set(T flag) { value |= static_cast<unsigned>(flag); }
        template<class T> void reset(T flag) { value &= ~static_cast<unsigned>(flag); }
    };
    struct NPCClass { const char* GetFullName() { return "Warrior"; } };
    struct TESNPC : TESForm {
        struct Data { Flags actorBaseFlags; } actorData;
        NPCClass* npcClass = nullptr;
        bool IsEssential() const { return actorData.actorBaseFlags.value & 1; }
        bool IsProtected() const { return actorData.actorBaseFlags.value & 2; }
        const char* GetFormEditorID() { return "TestActor"; }
    };
    enum class ActorValue { kHealth, kWaitingForPlayer, kAggression, kVariable04 };
    struct TESQuest;
    struct TESPackage : TESForm {};
    template<class T> using BSTArray = std::vector<T>;
    struct BGSRefAliasInstanceData {
        TESQuest* quest;
        const void* alias;
        const BSTArray<TESPackage*>* instancedPackages;
    };
    inline void* malloc(std::size_t size) { return std::malloc(size); }
    inline void free(void* pointer) { std::free(pointer); }
    struct ExtraAliasInstanceArray {
        BSTArray<BGSRefAliasInstanceData*> aliases;
        int lock=0;
        ~ExtraAliasInstanceArray() {
            for (auto* instance : aliases) { delete instance->instancedPackages; RE::free(instance); }
        }
    };
    struct ExtraList {
        std::unique_ptr<ExtraAliasInstanceArray> aliases;
        template<class T> T* GetByType() { return aliases.get(); }
        void Add(ExtraAliasInstanceArray* extra) { aliases.reset(extra); }
    };
    struct Actor;
    struct ActorHandle {
        Actor* actor = nullptr;
        std::shared_ptr<Actor> get() const { return std::shared_ptr<Actor>(actor, [](Actor*){}); }
    };
    struct Actor : TESForm {
        enum class BOOL_BITS { kPlayerTeammate=1 };
        struct Runtime {
            TESForm* editorLocForm = nullptr;
            NiPoint3 editorLocCoord;
            float editorLocRot = 0;
            Flags boolBits;
            ActorHandle currentCombatTarget;
        } runtime;
        TESNPC base;
        TESNPC* sharedBase = nullptr;
        std::unordered_set<TESFaction*> factions;
        std::map<ActorValue,float> values{{ActorValue::kHealth,100.0f}, {ActorValue::kAggression,1.0f}};
        bool combat=false, dead=false, vanillaAlias=true;
        ExtraList extraList;
        int stopCalls=0, evaluations=0;
        Runtime& GetActorRuntimeData() { return runtime; }
        TESNPC* GetActorBase() { return sharedBase ? sharedBase : &base; }
        const char* GetDisplayFullName() { return "Test follower"; }
        int GetLevel() { return 10; }
        bool IsInFaction(TESFaction* f) { return factions.contains(f); }
        void AddToFaction(TESFaction* f, int rank) { if (rank < 0) factions.erase(f); else factions.insert(f); }
        void RemoveFromFaction(TESFaction* f) { factions.erase(f); }
        bool IsPlayerTeammate() { return runtime.boolBits.value & 1; }
        bool IsDead() { return dead; }
        bool IsInCombat() { return combat; }
        Actor* AsActorValueOwner() { return this; }
        float GetActorValue(ActorValue av) { return values[av]; }
        void SetActorValue(ActorValue av, float value) { values[av] = value; }
        void EvaluatePackage(bool,bool) { ++evaluations; }
        void StopCombat() { combat=false; ++stopCalls; }
        void StopAlarmOnActor() {}
    };
    struct PlayerCharacter : Actor {
        static PlayerCharacter* GetSingleton() { static PlayerCharacter p; return &p; }
        TESWorldSpace* world = nullptr;
        TESObjectCELL* cell = nullptr;
        NiPoint3 position{10,20,30};
        TESWorldSpace* GetWorldspace() { return world; }
        TESObjectCELL* GetParentCell() { return cell; }
        NiPoint3 GetPosition() { return position; }
        Location* GetCurrentLocation() { static Location l; return &l; }
    };
    struct ProcessLists {
        std::vector<ActorHandle> highActorHandles;
        static ProcessLists* GetSingleton() { static ProcessLists p; return &p; }
    };
    struct TESQuest : TESForm { int aliasAccessLock=0; std::map<unsigned,ActorHandle> refAliasMap; };
    struct BSReadLockGuard { explicit BSReadLockGuard(int&) {} };
    struct BSWriteLockGuard { explicit BSWriteLockGuard(int&) {} };
}

#include "FollowerData.generated.h"

namespace SKSE {
    struct SerializationInterface {
        struct Record { std::uint32_t type, version, length; std::string bytes; };
        std::vector<Record> records;
        std::size_t index=0, offset=0;
        int reads=0;
        bool failWrite=false;
        std::unordered_set<RE::FormID> unresolved;
        bool OpenRecord(std::uint32_t type, std::uint32_t version) {
            records.push_back({type,version,0,{}}); return true;
        }
        bool WriteRecordData(const void* data, std::uint32_t size) {
            if(failWrite) return false;
            records.back().bytes.append(static_cast<const char*>(data),size);
            records.back().length+=size; return true;
        }
        template<class T> bool WriteRecordData(const T& value) { return WriteRecordData(&value,sizeof(value)); }
        bool GetNextRecordInfo(std::uint32_t& type, std::uint32_t& version, std::uint32_t& length) {
            if (index == records.size()) return false;
            auto& record=records[index++]; offset=0;
            type=record.type; version=record.version; length=record.length; return true;
        }
        std::uint32_t ReadRecordData(void* data, std::uint32_t size) {
            ++reads;
            auto& bytes = records[index-1].bytes;
            auto count = std::min<std::size_t>(size,bytes.size()-offset);
            std::memcpy(data,bytes.data()+offset,count); offset+=count;
            return static_cast<std::uint32_t>(count);
        }
        template<class T> std::uint32_t ReadRecordData(T& value) { return ReadRecordData(&value,sizeof(value)); }
        bool ResolveFormID(RE::FormID id, RE::FormID& resolved) {
            if (!id || unresolved.contains(id)) return false;
            resolved=id; return true;
        }
    };
}

struct Settings {
    static Settings& GetSingleton() { static Settings s; return s; }
    static void Notify(const char*) {}
    std::string GetFollowDistance() { return "normal"; }
    bool GetDefaultSandboxEnabled() { return false; }
    bool GetDefaultFollowClose() { return false; }
};

struct PackageManager {
    static PackageManager& GetSingleton() { static PackageManager p; return p; }
    std::map<int,RE::Actor*> slots;
    bool slotsAvailable=true;
    bool _questCached=false;
    RE::TESQuest* _residenceQuest=nullptr;
    RE::TESPackage* _residencePackage=nullptr;
    void InitQuestCache() {
        _residenceQuest=RE::TESForm::LookupByEditorID<RE::TESQuest>("Horde_ResidenceQuest");
        _residencePackage=RE::TESForm::LookupByEditorID<RE::TESPackage>("Horde_ResidencePkg");
        _questCached=true;
    }
    void ClearDismissedSandbox(RE::Actor*) {}
    void FillSlot(int slot, RE::Actor* actor) { slots[slot]=actor; }
    int FindEmptySlot() { if (!slotsAvailable) return -1; for(int i=0;i<20;++i) if(!slots.contains(i)) return i; return -1; }
    void ApplyFollowDistance(const std::string&) {}
    void ClearSandboxState(RE::Actor*) {}
    void ClearSlot(int slot) { slots.erase(slot); }
    bool HasDialogueFollowerAlias(RE::Actor* actor) { return actor->vanillaAlias; }
    bool HasAnimalAlias(RE::Actor* actor) const;
    void ReleaseAnimal(RE::Actor* actor);
    void ClearDialogueFollowerAlias(RE::Actor* actor) {
        auto* q=RE::TESForm::LookupByEditorID<RE::TESQuest>("DialogueFollower");
        if (q) std::erase_if(q->refAliasMap,[&](auto& p){return p.second.actor==actor;});
    }
    bool IsSlotFilled(int slot) { return slots.contains(slot); }
    bool EnsureResidencePackage(RE::Actor* actor);
    bool ClearResidencePackage(RE::Actor* actor);
    void Reset() { slots.clear(); _questCached=false; }
};

struct FollowerManager {
    static FollowerManager& GetSingleton() { static FollowerManager m; return m; }
    static constexpr std::uint32_t kFollowerRecord='FLWR', kRegistryRecord='RGST', kCosaveVersion=1;
    mutable std::recursive_mutex _mutex;
    std::unordered_map<RE::FormID,RegistryEntry> _registry;
    std::vector<FollowerData> _followers;
    std::unordered_set<RE::FormID> _rejectedCustomFollowers;
    int cap=20;
    const auto& GetFollowers() const { return _followers; }
    FollowerData* FindFollower(RE::FormID);
    RE::Actor* ResolveActor(RE::FormID) const;
    void UpdateDialogueGate() {}
    RE::TESFaction* GetCurrentFollowerFaction() const;
    RE::TESFaction* GetHordeFollowerFaction() const;
    void ApplyPassive(RE::Actor*,bool,FollowerData&) {}
    void ApplyProtection(RE::TESNPC*,int);
    bool ApplyHomeEditorLocation(RE::Actor*,RE::FormID,float,float,float,const std::string&);
    bool RestoreOriginalEditorLocation(RE::Actor*,const RegistryEntry&,RE::FormID);
    void CaptureOriginalEditorLoc(RE::FormID,RE::Actor*);
    void SetHome(RE::FormID);
    void ClearHome(RE::FormID);
    void SetDismissedHome(RE::FormID);
    void ClearDismissedHome(RE::FormID);
    void ForgetFollower(RE::FormID);
    void SyncRegistryHome(const FollowerData&);
    void UpsertRegistry(RE::FormID,const std::string&);
    void OnPostLoadGame();
    void OnCosaveLoad(SKSE::SerializationInterface*);
    void OnCosaveSave(SKSE::SerializationInterface*);
    void OnCosaveRevert();
    void ScanForFollowers();
    void SoftUntrack(RE::FormID);
    bool UntrackFollower(RE::FormID);
    bool IsTracked(RE::FormID) const;
    bool IsAtCap() { return _followers.size() >= static_cast<std::size_t>(cap); }
    int CaptureOriginalProtection(RE::Actor*,RE::TESNPC*) const;
    void ReleaseEssential(RE::Actor*,RE::FormID,int);
    void ReleaseAllEssential();
    void RestoreHome(FollowerData&);
    void RefreshHomes();
};

static std::int64_t now=0;
static std::int64_t NowMs() { return now; }
struct EventHandler {
    std::atomic<bool> _standDownPending{false}, _sessionReady{true};
    std::atomic<std::uint64_t> _session{0};
    std::atomic<std::int64_t> _lastCellLoadScan{0};
    Horde::ScanWindow _scanWindow;
    int queued=0;
    void RequestTeamStandDown() { ++queued; }
    bool ShouldBlockTeamDamage(RE::Actor*,RE::Actor*,float);
    void RunTeamStandDown();
    void RequestFastScan(int,bool);
    void SuspendSession();
    void ResumeSession();
};
