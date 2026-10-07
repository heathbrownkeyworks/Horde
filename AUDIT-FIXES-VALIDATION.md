# Horde follower lifecycle validation

Horde 3.0.1 includes the following follower lifecycle and persistence repairs.
Automated verification results are recorded below. Skyrim release validation is
**NOT RUN**; controller test status is tracked in
[IMGUI-VALIDATION.md](IMGUI-VALIDATION.md).

## Repairs

| Audit | Change | Automated coverage |
|---|---|---|
| H01 | Dismissed homes use a separate priority-1 quest package targeting the actor's editor location, independent of the twenty active follower slots. | Compiled package location, conditions and quest priority; dismissal, slot reuse, re-recruitment and ownership fixtures. |
| H02 | Registry homes recover on load and when a reference becomes available during later scans. | Registry-only load, delayed resolution and unchanged-package checks. |
| H03 | Deferred stand-down handles aggressive followers targeting teammates, while preserving hostile combat. | Player/follower targets, hostile targets and Passive mode. |
| H04 | Only an accepted recruit suppresses external-dismissal detection for that scan. | Excluded custom companion, full roster, unavailable slot and accepted recruit. |
| H05 | Explicit capture state distinguishes an original null location; pending restoration survives unavailable references. | Null capture, legacy migration, clear, forget, re-recruitment and revert. |
| H06 | Animal dismissal resets Variable04 and PlayerAnimalCount without clearing a replacement animal's gate. | Actual animal cleanup methods and native dismissal flow. |
| H07 | Attributed negative health deltas between teammates are filtered through Character/PlayerCharacter HandleHealthDamage hooks. Health-baseline refunds are removed. | Team directions, environmental/hostile/self damage, healing, zero/nonfinite deltas and prior environmental damage. |
| H08 | Load recovery respects other followers sharing an essential actor base. | Both follower orders and release of the final essential owner. |
| H09 | Equipment ratings use CommonLib's already-scaled armor rating. | Static regression check; native screen suite. |
| H10 | Cosave records validate type/version, record length, prefix length and exact reads before bounded allocation. Valid records survive malformed siblings/later records. | Cold loads with truncation, oversized lengths, invalid JSON/types/keys, duplicate actors/slots, unresolved forms, write failure and serializer round trips. |
| H11 | A new fast-scan window discards expired dialogue-sync state. | Overlap, deadline, new activation window and session reset. |

Polling, delayed cell work and combat tasks also carry a session generation so
work queued for an earlier save cannot act on the next one. Active followers
retain their original editor location; the home override applies after dismissal.
Higher-priority quests retain their packages, and home removal touches only
Horde's residence instances. Clearing an unavailable actor's home retains the
pending restoration. Forget refuses to erase that recovery data until it can
restore the actor.

## Verification results, 2026-09-29

- Release DLL verification build: passed with MSVC/xmake.
- Follower runtime suite: **92 checks passed**. The generator extracts production
  method bodies and JSON serializers, then compiles them against deterministic
  engine collaborators. This exercises Horde's control flow and data handling;
  it does not emulate Skyrim's package evaluator, vtable dispatch or pathfinding.
- Native screen suite: **1,184 assertions passed**.
- Native input suite: **46 assertions passed**.
- Static regression, recruitment inventory and compiled sandbox selection:
  passed, including **1,024 condition scenarios**.
- Residence ESP checks: passed. Exactly two records added relative to the
  pre-fix candidate; every existing non-header record is unchanged.
- Spriggit rebuild: passed. RacerESP reference and xEdit-strict validation:
  **zero errors and zero warnings**.
- `git diff --check`: passed.

Run the checks from the repository after building the matching ESP:

```powershell
xmake run HordeFollowerRuntimeTests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/horde_static_regression.ps1
node scripts/qa/horde_release_fixes.mjs plugin/Horde.esp
```

The residence-record check accepts an optional second path to the pre-repair ESP
for a full record-preservation comparison. That baseline must already contain
the sandbox integration records.

## Skyrim gates: NOT RUN

1. Assign Lydia an interior home before owning Breezehome. Dismiss her, travel
   away and back, save/load, and verify she travels to and sandboxes at the home.
   Repeat with an exterior destination and a different home.
2. Dismiss several followers with different homes; reuse their active slots;
   re-recruit, clear homes and forget entries. Exercise a follower with no
   original editor location and one whose reference is unavailable temporarily.
3. Confirm scenes and higher-priority quest behavior still take precedence over
   residence behavior. Check the save-load recovery of the new residence quest.
4. Test melee, arrows, spells, concentration and lingering effects between all
   team directions. Take fall/trap/hostile damage before a friendly hit and
   confirm none of that damage is refunded. Healing must still work.
5. Check friendly combat with Passive both on and off. Aggressive followers must
   continue fighting enemies; Passive followers must stand down.
6. Dismiss a follower while an excluded custom companion is nearby. Recruit and
   dismiss a vanilla animal, then recruit another animal.
7. Test shared-base essential preferences, equipment armor display, dialogue
   wait/follow synchronization, and rapid load/new-game/main-menu transitions.
8. Run the native input/controller gates in IMGUI-VALIDATION.md on hardware.

The damage filter deliberately requires an identifiable teammate attacker at
the engine's health-damage entry point. Direct actor-value writes that bypass
that entry point pass through unchanged; they are never estimated or refunded.
The pinned CommonLib declarations establish the hook signature and flat-runtime
slot. The negative-delta convention and the separate magic/value-modifier paths
were cross-checked against [FloatingDamageNG's implementation](https://github.com/alandtse/FloatingDamageNG/blob/main/src/Hooks.cpp).
In-game compatibility with other damage-hooking mods still requires the tests above.
