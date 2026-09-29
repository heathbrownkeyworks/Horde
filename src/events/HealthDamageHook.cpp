#include "pch.h"
#include "events/HealthDamageHook.h"
#include "events/EventHandler.h"

namespace
{
    template <class ActorType>
    struct HealthDamageHook
    {
        static void Thunk(RE::Actor* target, RE::Actor* attacker, float damage)
        {
            // HandleHealthDamage receives a negative health delta for damage.
            if (EventHandler::GetSingleton().ShouldBlockTeamDamage(target, attacker, damage)) {
                damage = 0.0f;
            }
            original(target, attacker, damage);
        }

        static void Install()
        {
            REL::Relocation<std::uintptr_t> table{ActorType::VTABLE[0]};
            // Actor::HandleHealthDamage, flat SE/AE. VR is excluded by the build.
            original = table.write_vfunc(0x104, Thunk);
        }

        static inline REL::Relocation<decltype(Thunk)> original;
    };
}

void Horde::InstallHealthDamageHook()
{
    static bool installed = false;
    if (installed) return;
    HealthDamageHook<RE::Character>::Install();
    HealthDamageHook<RE::PlayerCharacter>::Install();
    installed = true;
    logger::info("Horde: Health damage filters installed");
}
