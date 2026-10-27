# Horde

Horde 3.1.0 is a lightweight follower manager for Skyrim Special Edition
and Anniversary Edition. It automatically tracks up to 20 followers using
Skyrim's vanilla `DialogueFollower` system and presents the whole party through
an animated web interface hosted by
[Magelight UI](https://github.com/Severause/MagelightUI), group lesser
powers, or configurable hotkeys.

Build and test coverage is documented in
[IMPROVEMENTS-VALIDATION.md](IMPROVEMENTS-VALIDATION.md) and
[AUDIT-FIXES-VALIDATION.md](AUDIT-FIXES-VALIDATION.md). The current source includes
changes awaiting in-game validation.

## Features

### Party management

- Automatically detects up to 20 vanilla-system followers.
- Follow, wait, summon, and dismiss commands for individual followers.
- Follow All, Wait All, Summon All, and party-wide Passive commands.
- Close, Normal, and Far follow-distance presets.
- Optional per-follower Follow Close leash for companions who fall behind.
- Friendly-fire protection filters attributed teammate damage and stops combat
  between teammates.
- Custom-framework detection leaves independently managed followers alone.
- Recruitment does not grant the vanilla hidden follower bow or iron arrows;
  followers keep their existing equipment.

### Sandboxing and homes

- Per-follower sandboxing lets companions use nearby furniture in safe locations
  while keeping within an 800-unit area around the player. The engine switches
  behavior when the player sneaks or either actor enters combat.
- Waiting followers with sandboxing enabled use an 800-unit area around their
  own wait location in safe locations. Other waiting followers use the existing
  hold-position package. Waiting companions do not pursue the player.
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
- Full-screen roster and follower detail screens with a copper and amber
  palette, detailed icons, and animated transitions.
- An interface scale setting from 70% to 170%, the first option in Settings.
- Group hotkeys stand down while Horde is open, so typing in it issues no orders.
- Notifications can be disabled globally.
- Vanilla Follow, Wait, and Dismiss dialogue is hidden for tracked followers;
  Trade and Command dialogue remains available.
- The included `Horde.esp` is ESL-flagged and does not consume a full plugin slot.

## Magelight UI

Horde's menu is a web page rendered by Magelight UI 1.1 or newer. It ships in
the package under `Magelight/Horde/`.

- The Horde lesser power, keybinds, and crosshair quick-open open it.
- Mouse, Tab/arrows/Enter, and Escape work inside it; it pauses the game while
  open.
- Magelight UI has no controller input on flat Skyrim, so the menu is used with
  a mouse and keyboard.
- Without Magelight UI the menu cannot open. Opening it shows "Horde's menu
  needs Magelight UI 1.1 or newer." Follower tracking, the group powers, and
  group hotkeys keep working. `Horde.log` records whether Magelight UI was found.

Horde does not use PrismaUI.

In-game testing of the interface with Magelight UI is **NOT RUN** in these notes.

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

The DLL declares compatibility with SE 1.5.97 and AE through 1.7.104. The menu
also needs Magelight UI, whose supported runtimes are listed on its page. Skyrim
VR is not supported.

## Runtime requirements

- [SKSE64](https://skse.silverlock.org/) matching the installed Skyrim runtime
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  matching the installed runtime
- The release package's ESL-flagged `Horde.esp`
- [Magelight UI](https://github.com/Severause/MagelightUI) 1.1 or newer,
  and the package's `Data/Magelight/Horde/` files

Install the complete package into the Horde MO2 mod, including its license
files. Preserve `SKSE/Plugins/Horde/settings.json` when upgrading. Install the
matching DLL and ESP together. Existing per-follower sandbox preferences are
restored from the SKSE cosave; new fields have defaults for older saves. Horde
3.1.0 no longer uses `SKSE/Plugins/Horde/fonts/`; a manual upgrade from 3.0.x
can delete it.

Horde should not be installed alongside another multi-follower framework.
Testing on a new game is strongly recommended when replacing an existing
framework.

### Language

Horde's interface text is English. The page follows Skyrim's `sLanguage`
setting to sort names and format distances, and its headings include Cyrillic
and Greek letters.

## Controls and settings

The default mode grants the five Horde lesser powers on save load. Keybind mode
can be selected in Settings. Its defaults are Shift+H for the menu and
Alt+F/W/S/P for Follow All, Wait All, Summon All, and Passive.

Settings are stored in `Data/SKSE/Plugins/Horde/settings.json`. They include the
interface scale, roster limit, control mode and scan codes, default sandbox and
Follow Close states, notification state, and party follow distance.

### Interface scale

Horde fits its layout to the screen. Settings > Interface scale, the first
option, makes it larger or smaller from 70% to 170% in 5% steps; Default
returns to 100%. The percentage updates while dragging and the new size applies
on release. Above 100% there is less room, so the layout condenses: follower
details stack into fewer columns, and at the largest sizes the distance rail
hides and the details scroll. The value is stored as `uiScale` in
`settings.json`, and hand-edited values are clamped to the same range.

### Controllers

Magelight UI has no controller input on flat Skyrim, so Horde's menu is used
with a mouse and keyboard. Dismiss, Forget, and active-follower Clear Home keep
their confirmation dialogs, with Cancel initially focused. Horde pauses the game
while open.

## Building

Horde uses C++23 and xmake 3.0.1 or newer. CommonLibSSE-NG is pinned as a Git
submodule at commit `8b032fa992750d654d6d38a33731714d8b86be1f`.

After cloning, initialize the dependency and build the release target:

```powershell
git submodule update --init --recursive
# Stage development installs away from the active game/profile.
$env:XSE_TES5_MODS_PATH = 'E:\tmp\Horde-build\stage'
Remove-Item Env:XSE_TES5_GAME_PATH -ErrorAction SilentlyContinue
xmake f -m release --skyrim_vr=n -y
xmake -y Horde
```

The release DLL is written to `build/windows/x64/release/Horde.dll`.

Rebuild the matching ESP from its checked-in Spriggit source before packaging:

```powershell
Spriggit.CLI.exe deserialize --InputPath plugin/Horde --OutputPath plugin/Horde.esp
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa/horde_static_regression.ps1
```

The package YAML includes explicit numeric interrupt flags so Spriggit 0.40.0
preserves the full values without a binary patch step.

Stage a complete package in a new directory with a SHA-256 manifest:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -OutputDirectory E:\tmp\Horde-3.1.0
```

Use `-DllPath` to package an independently signed copy of the same built DLL.
The script records its signature status and source revision. It refuses an
existing destination and does not install into MO2. Keep the matching source
checkout/archive alongside any binary you distribute.

Run the game text and follower runtime suites:

```powershell
xmake -y HordeGameTextTests
xmake run HordeGameTextTests
xmake -y HordeFollowerRuntimeTests
xmake run HordeFollowerRuntimeTests
```

Preview the interface in a browser with synthetic data by opening
`view/index.html?mock`. `view/dev/mock.js` stands in for the plugin; add `pad`
for controller prompts, `empty` for an empty roster, `lang=ru` for a game
language, or `intl` for names in other scripts. The fixture is not a
compatibility claim, and `view/dev/` is not packaged.

`tools/view/build-icons.mjs` regenerates `view/icons.js` from a lucide-svelte
package and the artwork in `assets/icons/`.

`plugin/generate_dialogue_overrides.mjs` regenerates the vanilla dialogue
overrides from a supplied load-order-winner `DialogTopics` directory. Use the
winning records rather than raw `Skyrim.esm` records so Horde does not revert
another mod's dialogue changes.

## Source layout

- `src/` - SKSE plugin, follower state, behavior, and serialization
- `src/ui/` - Magelight UI view host and page listeners
- `src/MagelightUIAPI/` - Magelight UI's public API header
- `view/` - the Magelight UI page; `view/dev/` is the browser preview stand-in
- `assets/icons/` - editable Game-icons.net SVG artwork
- `tools/view/` - icon builder for `view/icons.js`
- `tests/` - game text and follower runtime tests
- `plugin/Horde/` - Spriggit YAML source for the ESL-flagged plugin
- `plugin/generate_dialogue_overrides.mjs` - dialogue override generation
- `scripts/qa/` - static regression and plugin inventory checks
- `scripts/package.ps1` - complete package staging and SHA-256 manifest

## License

Horde's source code and native DLL are licensed as GPL-3.0-or-later with the
Modding Exception and GPL-3.0 Linking Exception in
[EXCEPTIONS.md](EXCEPTIONS.md). Horde statically links CommonLibSSE-NG and is
not distributed as MIT-only software.

Bundled web fonts, icons, and native dependencies retain their licenses in
`licenses/`.
See [LICENSING.md](LICENSING.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact boundaries and
corresponding-source information.
