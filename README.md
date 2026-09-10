# Horde

Horde 2.1.0 is a lightweight, native follower manager for Skyrim Special Edition
and Anniversary Edition. It automatically tracks up to 20 followers using
Skyrim's vanilla `DialogueFollower` system and presents the whole party through
a Meridian UI panel, group lesser powers, or configurable hotkeys.

## Features

### Party management

- Automatically detects up to 20 vanilla-system followers.
- Follow, wait, summon, and dismiss commands for individual followers.
- Follow All, Wait All, Summon All, and party-wide Passive commands.
- Close, Normal, and Far follow-distance presets.
- Optional per-follower Follow Close leash for companions who fall behind.
- Friendly-fire protection refunds only the damage dealt and stops combat
  between teammates.
- Custom-framework detection leaves independently managed followers alone.
- Recruitment does not grant the vanilla hidden follower bow or iron arrows;
  followers keep their existing equipment.

### Sandboxing and homes

- Per-follower idle sandboxing lets companions wander, sit, eat, and use nearby
  furniture while the player is stationary, then resume following automatically.
- Waiting followers can sandbox around their wait location.
- Assign, change, or clear a home for any follower.
- Dismissed followers can live and sandbox at their assigned homes.
- Home assignments and follower state persist in the SKSE cosave.

### Dismissed follower registry

- Remembers dismissed followers so they are not lost.
- Summons and re-recruits a dismissed follower from anywhere.
- Sets or clears dismissed followers' homes remotely.
- Permanently forgets registry entries when they are no longer wanted.

### Per-follower controls

- Passive mode prevents a follower from entering combat.
- Essential status can be enabled or disabled for each follower.
- Follow Close and sandbox behavior can be configured independently.
- The detail screen shows race, level, class, armor, combat skills, equipped
  items, resource values, distance, status, and assigned home.

### Interface and integration

- Live roster with health, magicka, stamina, distance, class, and status.
- Crosshair quick-open jumps directly to the targeted follower's detail screen.
- Five lesser powers: Horde, Follow, Wait, Summon, and Passive.
- Optional configurable keyboard controls, including group commands.
- Optional controller navigation through Meridian.Input/1, with follower and
  command focus, section switching, safe confirmations, contextual button hints,
  and Meridian's shared stick cursor.
- Keyboard input is contained while the Horde panel is open.
- Notifications can be disabled globally.
- Vanilla Follow, Wait, and Dismiss dialogue is hidden for tracked followers;
  Trade and Command dialogue remains available.
- The included `Horde.esp` is ESL-flagged and does not consume a full plugin slot.

## Compatibility

Horde manages followers based on Skyrim's vanilla `DialogueFollower` system.
Followers with independent frameworks are detected and excluded so their own
systems can continue operating.

Horde is a standalone replacement for multi-follower frameworks and is not
compatible with NFF, EFF, or AFT. Independently managed followers such as
Inigo, Vilja, Lucien, and Kaidan are intentionally not managed. Serana is also
excluded because she uses the Dawnguard follower quest system.

`Horde.esp` overrides the `DialogueFollower` quest to remove the starter bow
and arrow grants from its Follower alias. If another plugin overrides that
quest after Horde, its winning Follower alias must also omit those two items.
This prevents future recruitment grants; it does not remove items already
stored in a follower's inventory.

The unified native plugin supports Skyrim SE 1.5.97 and AE runtimes through
1.7.104. Skyrim VR is not supported because Meridian UI does not currently
provide Horde's required VR compositor and input backend.

## Runtime requirements

- [SKSE64](https://skse.silverlock.org/) matching the installed Skyrim runtime
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  matching the installed runtime
- [Meridian UI](https://github.com/heathbrownkeyworks/MeridianUI)
- The release package's ESL-flagged `Horde.esp`

Horde should not be installed alongside another multi-follower framework.
Testing on a new game is strongly recommended when replacing an existing
framework.

## Controls and settings

The default mode grants the five Horde lesser powers on save load. Keybind mode
can be selected from the roster screen. Its defaults are Shift+H for the panel
and Alt+F/W/S/P for Follow All, Wait All, Summon All, and Passive.

Settings are stored in `Data/SKSE/Plugins/Horde/settings.json`. They include the
roster limit, control mode and scan codes, default sandbox and Follow Close
states, notification state, and party follow distance.

### Controllers

Controller support uses `Meridian.Input/1`, available in Meridian UI 1.5.0.
Horde detects that optional interface directly; older or custom Meridian builds
without it keep working with mouse, keyboard, and the existing Horde lesser powers.

Default Xbox-style controls (the footer adapts to Meridian's current bindings
and Xbox, PlayStation, or generic prompt family):

| Control | Action |
|---|---|
| Hold LB, then press Y | Open Horde, in either favorites or keybind mode |
| D-pad / left stick | Move focus within the current section |
| A | Select a follower and enter their commands, or activate the focused control |
| LB / RB | Previous / next section: Followers, Commands or Dismissed, Group Orders, Settings |
| B | Cancel a confirmation, return from a section, or close from Followers |
| X | Group Orders from Followers; return to Followers from Commands |
| Y | Open the dismissed registry from Followers, when entries exist |
| Right-stick click | Toggle Meridian's shared cursor |
| Right stick | Scroll the current area |

Open Horde from normal gameplay with other game menus closed: hold LB first,
then tap Y. Once Horde is open, Y retains its dismissed-registry action.

Dismiss, Forget, and active-follower Clear Home retain their confirmation dialogs,
with Cancel initially focused. Focus is restored by follower identity after
state updates, rather than by row number. Mouse use remains available at any time.
Horde keeps its existing paused-menu behavior and sends commands through the same
game-thread callbacks used by mouse controls.

The opener is registered through Meridian's conflict checks. If another consumer
already owns an overlapping shortcut, Horde logs that result without taking it
over; the Horde lesser power and keyboard opener remain available. Meridian's
global controller settings control dead zones, repeat timing, cursor speed and
prompt family. Horde adds no independent controller polling or text-entry keyboard.

## Building

Horde uses C++23 and xmake 3.0.1 or newer. CommonLibSSE-NG is pinned as a Git
submodule at commit `8b032fa992750d654d6d38a33731714d8b86be1f`.

After cloning, initialize the dependency and build the release target:

```powershell
git submodule update --init --recursive
xmake f -m release --skyrim_vr=n -y
xmake -y Horde
```

The release DLL is written to `build/windows/x64/release/Horde.dll`.

Run the source and plugin regression checks with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/horde_static_regression.ps1
```

Run the controller consumer tests with Node 20 or newer and a controller-enabled
Meridian source checkout. The tests use its real injected helper, not a copied
implementation shipped with Horde:

```powershell
npm --prefix scripts/qa ci
node scripts/qa/node_modules/playwright/cli.js install chromium --only-shell
$env:MERIDIAN_INPUT_HELPER = 'C:\path\to\MeridianUI\src\UIPlatform\Web\meridian-input.js'
npm --prefix scripts/qa test
```

These are browser/contract checks with simulated native transport. In-game
controller operation, paused input capture, shortcut conflicts and hardware
disconnect/reconnect must also be checked in Skyrim; browser tests do not certify
those runtime behaviors.

The binary `Horde.esp` is intentionally not stored in source control. Its
Spriggit YAML source is under `plugin/Horde/` and targets Spriggit 0.40.0. To
rebuild it with Spriggit CLI and restore the package interrupt-flag bits that
are not represented by the YAML schema:

```powershell
Spriggit.CLI.exe deserialize --InputPath plugin\Horde --OutputPath plugin\Horde.esp
node plugin\patch_interrupt_flags.mjs
```

`plugin/generate_dialogue_overrides.mjs` regenerates the vanilla dialogue
overrides from a supplied load-order-winner `DialogTopics` directory. Use the
winning records rather than raw `Skyrim.esm` records so Horde does not revert
another mod's dialogue changes.

## Source layout

- `src/` — SKSE plugin, follower state, behavior, serialization, and Meridian UI integration
- `view/` — in-game HTML interface and bundled fonts
- `plugin/Horde/` — Spriggit YAML source for the ESL-flagged plugin
- `plugin/*.mjs` — deterministic plugin-source maintenance tools
- `scripts/qa/` — static regression checks

## License

Horde's source code and native DLL are licensed as GPL-3.0-or-later with the
Modding Exception and GPL-3.0 Linking Exception in
[EXCEPTIONS.md](EXCEPTIONS.md). Horde statically links CommonLibSSE-NG and is
not distributed as MIT-only software.

The bundled Poppins font files in `view/fonts/` are distributed under the
[SIL Open Font License 1.1](view/fonts/OFL.txt). Copied Meridian UI integration
headers remain MIT-licensed. See [LICENSING.md](LICENSING.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact boundaries and
corresponding-source information.
