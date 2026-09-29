# Horde

Horde 3.0 is a lightweight follower manager for Skyrim Special Edition
and Anniversary Edition. It automatically tracks up to 20 followers using
Skyrim's vanilla `DialogueFollower` system and presents the whole party through
a native Dear ImGui/DX11 panel, group lesser powers, or configurable hotkeys.

Build and test coverage is documented in [IMGUI-VALIDATION.md](IMGUI-VALIDATION.md),
[IMPROVEMENTS-VALIDATION.md](IMPROVEMENTS-VALIDATION.md), and
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
- Native controller navigation with follower and command focus, section
  switching, confirmation dialogs, button legends, and an optional stick cursor.
- Full-screen roster and follower detail panels with a copper and amber palette,
  Poppins/Montserrat fonts, and detailed icons.
- Skyrim's native mouse cursor and sensitivity.
- Keyboard, mouse-button, and gamepad shortcuts are filtered before other
  Skyrim input listeners while Horde owns input.
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

The DLL declares compatibility with SE 1.5.97 and AE through 1.7.104.
Runtime-specific in-game checks are listed in [IMGUI-VALIDATION.md](IMGUI-VALIDATION.md).
Skyrim VR is not supported.

## Runtime requirements

- [SKSE64](https://skse.silverlock.org/) matching the installed Skyrim runtime
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  matching the installed runtime
- The release package's ESL-flagged `Horde.esp`
- The package's five fonts in `Data/SKSE/Plugins/Horde/fonts/`

ImGui and FreeType are compiled into Horde. Install the complete package into
the Horde MO2 mod, including its five fonts and license files. Preserve
`SKSE/Plugins/Horde/settings.json` when upgrading. Install the matching DLL and
ESP together. Existing per-follower sandbox preferences are restored from the
SKSE cosave; new fields have defaults for older saves.

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

The bottom legend shows controller actions for the current section, confirmation,
or cursor mode. It appears after controller activity and hides when mouse/keyboard
input resumes. The optional `controllerGlyphs` setting accepts `xbox` (default),
`playstation`, or `generic`; changing the labels does not change the bindings.

| Control | Action |
|---|---|
| Cast the Horde lesser power from Favorites | Open Horde in Favorites mode |
| D-pad / left stick | Move focus within the current section |
| A | Select a follower and enter their commands, or activate the focused control |
| LB / RB | Previous / next section: Followers, Commands or Dismissed, Group Orders, Settings |
| B | Cancel a confirmation, return from a section, or close from Followers |
| X | Group Orders from Followers; return to Followers from Commands |
| Y | Open the dismissed registry from Followers, when entries exist |
| Right-stick click (R3) | Toggle cursor/navigation mode |
| Right stick | Scroll the current section, or move the cursor when enabled |

Use Favorites mode, select the Horde lesser power from Skyrim's Favorites menu,
then cast it from normal gameplay with other game menus closed. The controller
legend and navigation appear as usual. Once Horde is open, Y opens the dismissed
registry.

Dismiss, Forget, and active-follower Clear Home retain their confirmation dialogs,
with Cancel initially focused. Focus is restored by follower identity after
state updates, rather than by row number. Mouse use remains available at any time.
Horde pauses the game while open and captures menu input. Input coexistence and
controller validation details are in [IMGUI-VALIDATION.md](IMGUI-VALIDATION.md).

Horde reserves no gamepad opening shortcut, leaving those buttons available to
Skyrim and other mods during gameplay. Mouse, Tab/arrows/Enter, and Escape remain
available inside Horde; Shift+H remains available in keybind mode.

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
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -OutputDirectory E:\tmp\Horde-3.0
```

Use `-DllPath` to package an independently signed copy of the same built DLL.
The script records its signature status and source revision. It refuses an
existing destination and does not install into MO2. Keep the matching source
checkout/archive alongside any binary you distribute.

Run the native screen, input, and follower runtime suites:

```powershell
xmake -y HordeImGuiScreenTests
xmake run HordeImGuiScreenTests
xmake -y HordeImGuiInputTests
xmake run HordeImGuiInputTests
xmake -y HordeFollowerRuntimeTests
xmake run HordeFollowerRuntimeTests
```

Build the shared-screen DX11 preview:

```powershell
xmake -y HordeImGuiPreview
build\windows\x64\release\HordeImGuiPreview.exe --fixture tools/imgui-preview/fixture.json --size 1920x1080 --shot E:/tmp/Horde-imgui/roster.png --frames 6
```

The test suite exercises real ImGui items and action payloads. The preview uses
the same screen code and fonts as the DLL. Its fixture is synthetic; it includes
names that Horde deliberately excludes in-game and is not a compatibility claim.
Use `--screen dismissed` or `--screen modal`, `--controller`, `--cursor`,
`--glyphs playstation` (also `xbox`/`generic`), or
`--focus controller` for additional captures. Omitting `--shot` explicitly opens
an interactive fixture preview. `--shot` keeps the window hidden.

In-game validation is still required for SE/AE rendering, game pause/input
capture, controller reconnect, coexisting native menus, load transitions, and
follower actions. See [IMGUI-VALIDATION.md](IMGUI-VALIDATION.md).

The checked-in icon font can be regenerated from `assets/icons/` without
changing its artwork. `glyphs.json` fixes the glyph order and code points.
Node 24 or later and the pinned development packages are
used only for this optional step:

```powershell
npm --prefix tools/imgui-preview ci
npm --prefix tools/imgui-preview run icons
```

`plugin/generate_dialogue_overrides.mjs` regenerates the vanilla dialogue
overrides from a supplied load-order-winner `DialogTopics` directory. Use the
winning records rather than raw `Skyrim.esm` records so Horde does not revert
another mod's dialogue changes.

## Source layout

- `src/` - SKSE plugin, follower state, behavior, and serialization
- `src/ui/imgui/` - native menu, input host, custom screen, and theme
- `assets/fonts/` - runtime fonts and converted original icons
- `assets/icons/` - editable SVG artwork and ordered glyph manifest
- `tools/imgui-preview/` - standalone DX11 preview, fixtures, and font builder
- `tests/` - native interaction, layout, input-routing, and follower runtime tests
- `plugin/Horde/` - Spriggit YAML source for the ESL-flagged plugin
- `plugin/generate_dialogue_overrides.mjs` - dialogue override generation
- `scripts/qa/` - static regression and plugin inventory checks
- `scripts/package.ps1` - complete package staging and SHA-256 manifest

## License

Horde's source code and native DLL are licensed as GPL-3.0-or-later with the
Modding Exception and GPL-3.0 Linking Exception in
[EXCEPTIONS.md](EXCEPTIONS.md). Horde statically links CommonLibSSE-NG and is
not distributed as MIT-only software.

Bundled fonts and native dependencies retain their licenses in `licenses/`.
See [LICENSING.md](LICENSING.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact boundaries and
corresponding-source information.
