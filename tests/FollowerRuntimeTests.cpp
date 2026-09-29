#include "RuntimeStubs.h"
#include "RuntimeMethods.generated.h"
#include <cstdlib>
#include <iostream>

namespace {
    int checks=0;
    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    }

    struct Fixture {
        FollowerManager& manager=FollowerManager::GetSingleton();
        PackageManager& packages=PackageManager::GetSingleton();
        RE::Actor actor, other, custom, enemy;
        RE::TESObjectCELL original, home;
        RE::TESFaction followers, horde, dismissed;
        RE::TESGlobal animalCount;
        RE::TESQuest dialogue;
        RE::TESQuest residenceQuest;
        RE::TESPackage residencePackage;
        Fixture()
        {
            manager._followers.clear(); manager._registry.clear(); manager._rejectedCustomFollowers.clear(); manager.cap=20;
            packages.Reset(); packages.slotsAvailable=true;
            actor.id=1; other.id=2; custom.id=3; enemy.id=4; original.id=100; home.id=200;
            RE::TESForm::forms={{1,&actor},{2,&other},{3,&custom},{4,&enemy},{100,&original},{200,&home}};
            RE::TESForm::editors={{"CurrentFollowerFaction",&followers},{"Horde_FollowerFaction",&horde},
                {"DismissedFollowerFaction",&dismissed},{"PlayerAnimalCount",&animalCount},{"DialogueFollower",&dialogue}};
            RE::TESForm::editors["Horde_ResidenceQuest"]=&residenceQuest;
            RE::TESForm::editors["Horde_ResidencePkg"]=&residencePackage;
            actor.runtime.editorLocForm=&original;
            actor.runtime.editorLocCoord={1,2,3}; actor.runtime.editorLocRot=0.5f;
            custom.vanillaAlias=false;
            auto* player=RE::PlayerCharacter::GetSingleton();
            player->id=0x14; player->world=nullptr; player->cell=&home;
            RE::ProcessLists::GetSingleton()->highActorHandles.clear();
        }
        bool HasResidence(RE::Actor& subject)
        {
            auto* extra=subject.extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!extra) return false;
            return std::any_of(extra->aliases.begin(),extra->aliases.end(),[&](auto* instance){
                return instance->quest==&residenceQuest && !instance->alias;
            });
        }
        void Active(RE::Actor& who)
        {
            FollowerData f; f.actorFormID=who.id; f.name="Test follower"; f.aliasSlot=packages.FindEmptySlot();
            manager._followers.push_back(f); manager.UpsertRegistry(who.id,f.name);
            manager.CaptureOriginalEditorLoc(who.id,&who);
            who.AddToFaction(&followers,0); who.runtime.boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            packages.FillSlot(f.aliasSlot,&who);
        }
        void Detect(RE::Actor& who)
        {
            who.AddToFaction(&followers,0); who.runtime.boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            RE::ProcessLists::GetSingleton()->highActorHandles.push_back({&who});
        }
        void ExternalDismiss(RE::Actor& who)
        {
            who.RemoveFromFaction(&followers); who.runtime.boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
        }
    };

    void Homes()
    {
        Fixture f;
        f.Active(f.actor);
        f.manager.SetHome(1);
        Check(f.actor.runtime.editorLocForm==&f.original && !f.HasResidence(f.actor),"active home stores destination without overriding quest editor location");
        Check(f.manager.UntrackFollower(1),"explicit dismissal succeeds");
        Check(f.actor.runtime.editorLocForm==&f.home && f.HasResidence(f.actor),"dismissal attaches residence and applies home");
        Check(f.packages.slots.empty(),"dismissed home does not consume an active slot");
        f.Detect(f.other); f.manager.ScanForFollowers();
        Check(f.manager.IsTracked(2) && f.manager.FindFollower(2)->aliasSlot==0,"another follower can reuse the released slot");
        Check(f.actor.runtime.editorLocForm==&f.home && f.HasResidence(f.actor),"slot reuse preserves dismissed home");
        const int evaluations=f.actor.evaluations;
        f.manager.RefreshHomes();
        Check(f.actor.evaluations==evaluations,"unchanged home does not restart its package");
        nlohmann::json saved=f.manager._registry.at(1);
        f.manager._registry.at(1)=saved.get<RegistryEntry>();
        f.actor.runtime.editorLocForm=&f.original; f.packages.ClearResidencePackage(&f.actor);
        f.manager.OnPostLoadGame();
        Check(f.actor.runtime.editorLocForm==&f.home && f.HasResidence(f.actor),"registry-only dismissed home recovers after load");
        f.Detect(f.actor); f.manager.ScanForFollowers();
        Check(f.manager.IsTracked(1) && !f.HasResidence(f.actor) && f.actor.runtime.editorLocForm==&f.original,"recruitment restores original location and removes residence package");
        Check(f.manager.FindFollower(1)->homeWorldspace==200,"recruitment preserves saved home preference");
        f.manager.ClearHome(1);
        Check(f.manager._registry.at(1).homeWorldspace==0 && f.actor.runtime.editorLocForm==&f.original,"clear active home preserves original location");
        f.manager.SetHome(1); f.manager.UntrackFollower(1); f.manager.ClearDismissedHome(1);
        Check(!f.HasResidence(f.actor) && f.actor.runtime.editorLocForm==&f.original,"clear dismissed home releases package and restores location");
        Check(f.actor.runtime.editorLocCoord.x==1 && f.actor.runtime.editorLocRot==0.5f,"restore preserves original position and rotation");
        f.manager.SetDismissedHome(1); f.manager.ForgetFollower(1);
        Check(!f.manager._registry.contains(1) && !f.HasResidence(f.actor) && f.actor.runtime.editorLocForm==&f.original,"forget restores home before deleting registry");
    }

    void NullAndUnavailableHomes()
    {
        Fixture f; f.actor.runtime.editorLocForm=nullptr; f.Active(f.actor);
        Check(f.manager._registry.at(1).originalEditorLocCaptured && f.manager._registry.at(1).originalEditorLocFormID==0,"original null location is captured explicitly");
        f.manager.SetHome(1); f.manager.UntrackFollower(1); f.manager.CaptureOriginalEditorLoc(1,&f.actor);
        Check(f.manager._registry.at(1).originalEditorLocFormID==0,"repeat capture never records Horde home as original");
        RE::TESForm::forms.erase(1); f.manager.ClearDismissedHome(1);
        Check(f.manager._registry.at(1).homeRestorePending,"unavailable actor retains pending restoration");
        RE::TESForm::forms[1]=&f.actor; f.manager.RefreshHomes();
        Check(!f.actor.runtime.editorLocForm && !f.HasResidence(f.actor) && !f.manager._registry.at(1).homeRestorePending,"deferred clearing restores original null");
        f.manager.SetDismissedHome(1); RE::TESForm::forms.erase(1); f.manager.ForgetFollower(1);
        Check(f.manager._registry.contains(1),"unavailable forget retains restoration data");
        RE::TESForm::forms[1]=&f.actor; f.manager.ForgetFollower(1);
        Check(!f.manager._registry.contains(1) && !f.actor.runtime.editorLocForm,"forget succeeds when actor can be restored");
        auto legacy=nlohmann::json{{"homeWorldspace",200},{"originalEditorLocFormID",0}}.get<RegistryEntry>();
        Check(legacy.originalEditorLocCaptured && legacy.homeRestorePending,"legacy assigned home with zero original migrates as captured null");
        auto fresh=nlohmann::json::object().get<RegistryEntry>();
        Check(!fresh.originalEditorLocCaptured,"empty legacy entry remains uncaptured");
        f.manager.UpsertRegistry(1,"Test");
        RE::TESForm::forms.erase(1); f.manager.SetDismissedHome(1);
        RE::TESForm::forms[1]=&f.actor; f.manager.RefreshHomes();
        Check(f.HasResidence(f.actor) && f.actor.runtime.editorLocForm==&f.home,"unavailable home assignment retries when actor resolves");
        f.manager.OnCosaveRevert();
        Check(!f.HasResidence(f.actor) && !f.actor.runtime.editorLocForm && f.manager._registry.empty(),"revert unwinds residence and original null before discarding registry");
        Check(!f.manager.ApplyHomeEditorLocation(&f.actor,3,0,0,0,"invalid"),"actor form cannot be a home destination");
        Check(!f.manager.ApplyHomeEditorLocation(&f.actor,200,std::numeric_limits<float>::infinity(),0,0,"invalid"),"nonfinite home position is rejected");
        f.packages.InitQuestCache();
        auto* extra=f.actor.extraList.GetByType<RE::ExtraAliasInstanceArray>();
        auto* unrelated=static_cast<RE::BGSRefAliasInstanceData*>(RE::malloc(sizeof(RE::BGSRefAliasInstanceData)));
        *unrelated={&f.dialogue,&f.dialogue,new RE::BSTArray<RE::TESPackage*>()};
        extra->aliases.push_back(unrelated);
        Check(f.packages.EnsureResidencePackage(&f.actor),"residence package attaches once");
        Check(!f.packages.EnsureResidencePackage(&f.actor) && extra->aliases.size()==2,"repeated home refresh does not duplicate package instances");
        f.packages.ClearResidencePackage(&f.actor);
        Check(extra->aliases.size()==1 && extra->aliases[0]==unrelated,"home removal preserves another quest's alias instance");
    }

    void Recruitment()
    {
        for(int scenario=0;scenario<4;++scenario) {
            Fixture f; f.Active(f.actor); f.ExternalDismiss(f.actor);
            if(scenario==0) f.Detect(f.custom);
            else f.Detect(f.other);
            if(scenario==1) f.manager.cap=1;
            if(scenario==2) f.packages.slotsAvailable=false;
            f.manager.ScanForFollowers();
            if(scenario==3) Check(f.manager.IsTracked(1) && f.manager.IsTracked(2),"accepted recruit preserves the preceding vanilla follower");
            else Check(!f.manager.IsTracked(1) && !f.actor.IsPlayerTeammate(),"excluded, capped or slot-rejected candidate cannot suppress dismissal");
        }
    }

    void AnimalsAndProtection()
    {
        Fixture f; f.Active(f.actor); f.dialogue.refAliasMap[1]={&f.actor}; f.animalCount.value=1;
        f.actor.values[RE::ActorValue::kVariable04]=1;
        Check(f.packages.HasAnimalAlias(&f.actor),"animal alias is recognized");
        f.manager.UntrackFollower(1);
        Check(f.animalCount.value==0 && f.actor.values[RE::ActorValue::kVariable04]==0,"animal dismissal clears count and command state");
        Check(f.dialogue.refAliasMap.empty(),"animal dismissal releases vanilla alias");
        f.Active(f.actor); f.manager.FindFollower(1)->isAnimal=true;
        f.dialogue.refAliasMap[1]={&f.other}; f.animalCount.value=1;
        f.manager.UntrackFollower(1);
        Check(f.animalCount.value==1 && f.dialogue.refAliasMap.at(1).actor==&f.other,"dismissing old animal preserves replacement animal gate");
        for(bool reverse : {false,true}) {
            Fixture shared; RE::TESNPC base;
            shared.actor.sharedBase=&base; shared.other.sharedBase=&base;
            shared.Active(shared.actor); shared.Active(shared.other);
            shared.manager.FindFollower(2)->isEssential=false;
            if(reverse) std::reverse(shared.manager._followers.begin(),shared.manager._followers.end());
            shared.manager.OnPostLoadGame();
            Check(base.IsEssential(),"shared-base essential survives either load order");
            shared.manager.UntrackFollower(1);
            Check(!base.IsEssential(),"last essential owner releases shared override");
        }
    }

    void Combat()
    {
        Fixture f; f.Active(f.actor); f.Active(f.other); EventHandler events;
        auto* player=RE::PlayerCharacter::GetSingleton();
        Check(events.ShouldBlockTeamDamage(&f.actor,&f.other,-5),"follower-to-follower damage blocked");
        Check(events.ShouldBlockTeamDamage(&f.actor,player,-5),"player-to-follower damage blocked");
        Check(events.ShouldBlockTeamDamage(player,&f.actor,-5),"follower-to-player damage blocked");
        Check(!events.ShouldBlockTeamDamage(&f.actor,&f.enemy,-5),"hostile damage passes");
        Check(!events.ShouldBlockTeamDamage(&f.actor,nullptr,-30),"environmental damage passes");
        Check(!events.ShouldBlockTeamDamage(&f.actor,&f.actor,-5),"self damage passes");
        Check(!events.ShouldBlockTeamDamage(&f.actor,player,5),"healing passes");
        Check(!events.ShouldBlockTeamDamage(&f.actor,player,0),"zero damage passes");
        Check(!events.ShouldBlockTeamDamage(&f.actor,player,std::numeric_limits<float>::quiet_NaN()),"nonfinite damage is not reinterpreted");
        f.actor.values[RE::ActorValue::kHealth]=70;
        if (!events.ShouldBlockTeamDamage(&f.actor,player,-5)) f.actor.values[RE::ActorValue::kHealth]-=5;
        Check(f.actor.values[RE::ActorValue::kHealth]==70,"friendly graze cannot refund earlier environmental damage");
        f.actor.combat=true; f.actor.runtime.currentCombatTarget={player}; events.RunTeamStandDown();
        Check(!f.actor.combat && f.actor.stopCalls==1,"aggressive follower targeting player stands down");
        f.actor.combat=true; f.actor.runtime.currentCombatTarget={&f.other}; events.RunTeamStandDown();
        Check(!f.actor.combat,"aggressive follower targeting another follower stands down");
        f.actor.combat=true; f.actor.runtime.currentCombatTarget={&f.enemy}; events.RunTeamStandDown();
        Check(f.actor.combat,"aggressive follower continues fighting hostile target");
        f.manager.FindFollower(1)->isPassive=true; events.RunTeamStandDown();
        Check(!f.actor.combat,"passive follower stands down against hostile target");
        events.SuspendSession();
        Check(!events.ShouldBlockTeamDamage(&f.actor,player,-5),"damage filter is inactive during save transition");
        Check(events._session==1 && !events._sessionReady,"save transition advances session and suspends work");
    }

    SKSE::SerializationInterface::Record Record(std::uint32_t type, const std::string& payload)
    {
        auto size=static_cast<std::uint32_t>(payload.size());
        std::string bytes(sizeof(size),'\0'); std::memcpy(bytes.data(),&size,sizeof(size)); bytes+=payload;
        return {type,1,static_cast<std::uint32_t>(bytes.size()),bytes};
    }

    void Cosaves()
    {
        Fixture f; using Reader=SKSE::SerializationInterface;
        auto good=Record(FollowerManager::kFollowerRecord,R"([{"formID":1,"name":"Valid"}])");
        auto registry=Record(FollowerManager::kRegistryRecord,R"({"1":{"name":"Valid"},"bad":{},"2":{"homeX":"wrong"}})");
        std::vector<Reader::Record> bad={
            {FollowerManager::kFollowerRecord,1,0,{}},
            {FollowerManager::kFollowerRecord,1,3,std::string(3,'\0')},
            {FollowerManager::kFollowerRecord,1,0xffffffffu,{}},
            {FollowerManager::kFollowerRecord,1,4,std::string(4,'\xff')},
            {FollowerManager::kFollowerRecord,1,10,std::string(4,'\0')},
            Record(FollowerManager::kFollowerRecord,"not json"),
            Record(FollowerManager::kFollowerRecord,R"([{"formID":"bad"},{"formID":2}])")
        };
        auto truncated=good; truncated.bytes.pop_back(); bad.push_back(truncated);
        auto shortPrefix=good; shortPrefix.bytes.resize(2); bad.push_back(shortPrefix);
        for(const auto& invalid : bad) {
            f.manager._followers.clear(); f.manager._registry.clear();
            Reader reader; reader.records={invalid,good,registry}; f.manager.OnCosaveLoad(&reader);
            Check(f.manager._followers.size()==1 && f.manager._followers[0].actorFormID==1,"valid follower record recovers after malformed or truncated record");
            Check(f.manager._registry.size()==1 && f.manager._registry.contains(1),"malformed registry entries preserve valid cold-load entry");
        }
        Reader unknown; unknown.records={{'WHAT',1,0xffffffffu,{}},{FollowerManager::kFollowerRecord,99,0xffffffffu,{}}};
        f.manager.OnCosaveLoad(&unknown);
        Check(unknown.reads==0,"unknown types and versions are skipped before payload reads");
        Reader mixed; mixed.records={Record(FollowerManager::kFollowerRecord,R"([{"formID":1},{"formID":"bad"},{"formID":2}])")};
        f.manager.OnCosaveLoad(&mixed);
        Check(f.manager._followers.size()==2,"one malformed follower does not discard valid siblings");
        Reader unresolved; unresolved.unresolved={2}; unresolved.records=mixed.records; f.manager.OnCosaveLoad(&unresolved);
        Check(f.manager._followers.size()==1 && f.manager._followers[0].actorFormID==1,"unresolvable follower is pruned individually");
        Reader laterBad; laterBad.records={good,registry,Record(FollowerManager::kFollowerRecord,"bad"),Record(FollowerManager::kRegistryRecord,"bad")};
        f.manager.OnCosaveLoad(&laterBad);
        Check(f.manager._followers.size()==1 && f.manager._registry.size()==1,"later corrupt records preserve earlier successfully decoded data");
        Reader badKeys; badKeys.records={Record(FollowerManager::kRegistryRecord,R"({"1":{"name":"Valid"},"-1":{},"4294967296":{},"2suffix":{},"0":{}})")};
        f.manager.OnCosaveLoad(&badKeys);
        Check(f.manager._registry.size()==1 && f.manager._registry.contains(1),"registry rejects negative, overflowing and partially numeric FormIDs");
        Reader duplicates; duplicates.records={Record(FollowerManager::kFollowerRecord,R"([{"formID":1,"aliasSlot":0},{"formID":1},{"formID":2,"aliasSlot":0},{"formID":3,"aliasSlot":99}])")};
        f.manager.OnCosaveLoad(&duplicates);
        Check(f.manager._followers.size()==3,"duplicate actor records are collapsed");
        Check(f.manager.FindFollower(2)->aliasSlot==1 && f.manager.FindFollower(3)->aliasSlot==2,"duplicate and invalid alias slots get unused replacements");
        Reader reserved; reserved.records={Record(FollowerManager::kFollowerRecord,R"([{"formID":1,"aliasSlot":-1},{"formID":2,"aliasSlot":0}])")};
        f.manager.OnCosaveLoad(&reserved); f.manager.OnPostLoadGame();
        Check(f.packages.slots.at(0)==&f.other && f.packages.slots.at(1)==&f.actor,"repaired earlier entry cannot steal a later saved alias slot");
        f.manager.FindFollower(1)->isAnimal=true;
        f.manager.SetHome(1);
        Reader saved; f.manager.OnCosaveSave(&saved);
        Check(saved.records.size()==2,"save writes roster and registry records");
        f.manager._followers.clear(); f.manager._registry.clear(); f.manager.OnCosaveLoad(&saved);
        Check(f.manager.FindFollower(1)->isAnimal && f.manager._registry.at(1).originalEditorLocCaptured,"animal and original-location capture state survive a real serializer round trip");
        Reader failed; failed.failWrite=true; f.manager.OnCosaveSave(&failed);
        Check(failed.records.size()==1 && failed.records[0].bytes.empty(),"failed prefix write stops serialization");
        f.manager._followers.clear(); f.manager._registry.clear();
        Reader empty; f.manager.OnCosaveSave(&empty);
        Check(empty.records[1].bytes.substr(4)=="{}","empty registry is saved as an object");
    }

    void ScanWindows()
    {
        EventHandler events;
        now=1000; events.RequestFastScan(6000,true);
        now=2000; events.RequestFastScan(1000,false);
        Check(events._scanWindow.Read(6500).dialogue,"shorter overlapping request preserves dialogue window");
        Check(!events._scanWindow.Read(7000).active,"window expires at deadline");
        now=8000; events.RequestFastScan(3000,false);
        Check(events._scanWindow.Read(now).active && !events._scanWindow.Read(now).dialogue,"new non-dialogue window discards expired dialogue state");
        events.SuspendSession();
        Check(!events._scanWindow.Read(now).active,"save transition clears fast scan window");
        events.ResumeSession(); Check(events._sessionReady,"post-load resumes current session");
    }
}

int main()
{
    Homes(); NullAndUnavailableHomes(); Recruitment(); AnimalsAndProtection(); Combat(); Cosaves(); ScanWindows();
    std::cout << checks << " follower runtime regression checks passed. Engine collaborators are fixtures; Skyrim: NOT RUN.\n";
}
