# Horde sandbox and package validation

Horde uses a fixed alias package stack and engine conditions to select follower
behavior. Per-follower preferences persist in the SKSE cosave. This document
covers the package records, migration constraints, and validation procedure.

## Package selection

All twenty follower aliases use this priority order:

| Package | Behavior |
|---|---|
| `Horde_SandboxWaitPkg` | Sandbox within 800 units of the wait position when enabled, out of combat, and in a safe location. |
| `Horde_SandboxActivePkg` | Sandbox within 800 units of the player when enabled and safe, while neither actor is in combat and the player is not sneaking. |
| `Horde_WaitPkg` | Hold position when waiting and the sandbox conditions are not met. |
| `Horde_FollowPkg` | Follow the player when not waiting. |

`Horde_SandboxEnabledFaction` records each follower's opt-in. The
`Horde_SandBoxEnabled` global is 1 while any tracked follower has sandboxing
enabled, including waiting followers. Both are synchronized after recruitment,
preference changes, scans, dismissal, and save loading. Revert clears the prior
save's memberships and global before discarding its follower list.

The engine evaluates movement, sneak, combat, and location conditions. These
transitions do not move a shared marker or rebuild the package stack. Follow,
wait, and summon commands reset obsolete package start positions. Waiting
followers keep their own wait position instead of pursuing the player.

Dismissed homes use the separate priority-1 `Horde_ResidenceQuest` and
`Horde_ResidencePkg`. They do not consume active follower slots. Home restoration
and registry checks are covered in [AUDIT-FIXES-VALIDATION.md](AUDIT-FIXES-VALIDATION.md).

## Record and migration constraints

- Sandbox additions use compact local IDs `000838` (global), `000839` (waiting
  package), and `00083A` (opt-in faction). Residence records use `00083B` and
  `00083C`. The plugin remains ESL-flagged with `Skyrim.esm` as its only master.
- Existing FormKeys, alias IDs, recruitment dialogue, powers, follow/wait
  packages, and holding-cell references are preserved. The retired sandbox
  marker remains for save/reference stability.
- All twenty aliases carry `StoresText` and the same package order. Runtime
  refill replaces only the matching Horde alias instance, including older saved
  instances with shorter package stacks. Other quests' aliases remain intact.
- Each safe-location OR group is terminated explicitly. Numeric interrupt flags
  preserve all package bits through Spriggit 0.40.0 deserialization.
- Vanilla recruitment, payment, and relationship handling remain in place.
  Dialogue suppression applies only to Horde-managed followers' dismiss, wait,
  and follow topics. Trade and favors remain available.
- Followers must use the vanilla `DialogueFollower` system. Race alone does not
  establish ownership of an animal or quest companion. Essential status remains
  controlled by the saved per-follower preference.
- Install the matching ESP and DLL together.

## Automated checks

Run from the repository after rebuilding `plugin/Horde.esp`:

```powershell
xmake build -y Horde
xmake run HordeImGuiScreenTests
xmake run HordeImGuiInputTests
xmake run HordeFollowerRuntimeTests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/horde_static_regression.ps1
node scripts/qa/horde_plugin_improvements.mjs plugin/Horde.esp
```

The plugin check validates twenty alias stacks, ESL bounds, the default global,
templates, weapon flags, locations, and interrupt flags. It evaluates **1,024
compiled-condition scenarios** covering preferences, waiting, sneak, combat,
every allowed location keyword, unsafe locations, and fallback packages.

An optional second path to `horde_plugin_improvements.mjs` compares against the
pre-integration ESP. It permits five new records and changes to the follower
quest, active sandbox package, and plugin header; all other records must match.
RacerESP strict/reference validation and a Spriggit roundtrip also passed on the
2026-09-29 candidate. These checks do not execute Skyrim's package evaluator or
establish saved-game migration behavior.

## Skyrim test gates: NOT RUN

Use a copied save and retain the previous matching DLL/ESP for rollback.

1. Load an existing save with mixed sandbox preferences, then another save and
   a new game. Verify preferences and the global do not leak between saves.
2. In an inn, enable sandboxing for one follower and disable it for another.
   Switch all preferences off and on while already waiting.
3. Walk away, sneak, and enter combat. Check follow transitions and equipment
   reequipping, including custom weapons.
4. Leave a waiting follower through a door, then summon them. Verify both the
   stationary wait and the new wait center. Repeat in a dungeon to check the
   hold-position fallback.
5. Test doors, fast travel, Follow Close, group commands, dismissal, recruitment,
   and assigned homes with several followers.
6. Recruit and dismiss a normal follower, a hireling, and a vanilla animal.
   Verify payment, dialogue visibility, and subsequent recruitment.
7. Check Essential-off behavior and restoration on dismissal. Confirm custom
   frameworks and quest-controlled companions remain outside Horde ownership.
8. Run the input and controller gates in [IMGUI-VALIDATION.md](IMGUI-VALIDATION.md).
