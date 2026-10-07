# Horde native UI validation

Horde 3.0.1 uses Dear ImGui/DX11. Follower package and persistence checks are
documented in [IMPROVEMENTS-VALIDATION.md](IMPROVEMENTS-VALIDATION.md) and
[AUDIT-FIXES-VALIDATION.md](AUDIT-FIXES-VALIDATION.md).
The UI design is maintained in `src/ui/imgui/HordeScreen.cpp` and `Theme.h`;
editable icon sources and their fixed glyph order live in `assets/icons/`.

## Automated and desktop checks

- Build the release DLL, native screen/input suites, and shared-screen DX11
  preview with MSVC/xmake, using the pinned dependencies.
- Run the screen suite: 1,363 assertions cover action payloads, selection/focus
  identity through reorder, missing actors, empty roster/registry, long names,
  confirmations with Cancel selected, disabled frames, controller sections and
  back actions, and bounds at 1280x720, 1920x1080, 2560x1440, and 3440x1440.
- Font checks use the production loader and installed Windows fonts to verify
  Cyrillic, Greek, Chinese, Japanese, Korean, and Latin glyphs in all four text
  faces. They rasterize actual characters at two sizes, render the multilingual
  fixture, and exercise context recreation and missing bundled fonts. Install
  the corresponding Windows fonts before running this coverage check.
- Run the input suite: 46 assertions cover keyboard/mouse/controller shortcut
  containment, held/released ownership across close, distinct trigger/device
  identities, native cursor motion, neutral sticks, disconnect reset, unchanged
  linked lists, and guard restoration after downstream exceptions.
- Run static follower/recruitment checks and verify the binary ESP's five
  package InterruptFlags and compiled sandbox condition scenarios.
- Regenerate the icon font and compare its SHA-256 with the existing font.
  Regeneration must preserve artwork and code points.
- Capture the shared-screen DX11 preview for the roster, dismissed registry,
  confirmation, empty roster, 720p, and ultrawide controller layouts. The fonts,
  copper/amber palette, equal columns, detail panels, and action bar must match.
- Use `tools/imgui-preview/fixture-multilingual.json` for localized follower,
  equipment, and home names. Check the roster, details, registry and confirmation
  at 1080p and a 720p controller layout. Compare the English fixture against its
  pre-fallback capture to confirm the bundled fonts and icons remain unchanged.
- Inspect DLL imports, then sign with a timestamp and stage the complete
  package using `scripts/package.ps1`. Verify its SHA-256 manifest and retain
  the exact source revision and dependency revision with the build evidence.

Desktop checks exercise synthetic fixture data and cannot certify Skyrim
behavior. A local build or deployment is not a public release. Deployment
receipts record actual check results, source revision, signatures, and hashes.

## Native input behavior

- Native focus and the optional right-stick cursor consume Skyrim events.
  Gamepad users open Horde by casting its lesser power in Favorites mode.
  Gameplay buttons have no Horde opening shortcut. Held buttons must be released
  before the menu accepts controller navigation.
- The controller footer uses bordered badges with Xbox, PlayStation, or generic
  labels. Mouse movement, sensitivity, and cursor drawing belong to Skyrim.
  The stick dead zone is 0.25; right-stick cursor speed is 900 pixels/second
  at 1080p, scaled by display height with a 50ms movement delta cap.
- A chained input dispatch hook filters owned buttons, text, and nonneutral
  sticks before other listeners, including while Horde is opening or closing.
  Owned releases remain filtered after close. Print Screen, native mouse motion,
  neutral sticks, connection state, and releases of pre-existing presses pass.
  Independent OS polling and script-opened menus are outside this boundary.
- Skyrim VR is unsupported pending a dedicated rendering/input implementation.

## Font fallback validation, 2026-10-07

- The coverage test failed on Cyrillic U+0416 with the original font loader.
  With Windows fallbacks, all four text faces rasterize the tested Cyrillic,
  Greek, Chinese, Japanese, Korean, and Latin characters at 14px and 24px.
- The release DLL and shared-screen DX11 preview build successfully. The screen
  suite passes 1,363 assertions and input routing passes 46. Static regression
  and the 1,024 compiled package-condition scenarios also pass.
- The English 1080p fixture capture is byte-identical before and after the font
  change. Multilingual roster, equipment, home, registry, confirmation, and 720p
  controller captures display the supplied characters.
- These checks used the local Windows installation with Segoe UI, Microsoft
  YaHei, Yu Gothic, and Malgun Gothic available. System fonts are shared from
  memory and remain available across ImGui context recreation.
- Localized Skyrim rendering and physical-controller checks for this build are
  **NOT RUN**. The font change does not translate interface labels.

## Controller test status, 2026-09-29

In-menu controller operation was confirmed in-game. Opening through the lesser
power and passing the former opening chord to other mods still need an in-game
retest of the updated build. Automated checks do not establish completion of
the remaining hardware and runtime tests below.

## Remaining Skyrim and controller gates: NOT RUN

1. Launch Horde with SKSE/Address Library and its complete package. Open through
   the lesser power in Favorites mode and Shift+H in keybind mode. With Horde
   closed, confirm LB+Y stays available to Skyrim/other mods and never opens Horde.
2. Check SE 1.5.97 and AE separately, including the intended AE runtime. Confirm
   fonts, scaling, dimmed game background, game pause, and the graphics stack.
3. Exercise each actor and party command, home changes, dismissal, registry
   summon/recruit, forget, and persisted settings. Check Cancel/Back never
   dispatches an action. Confirm crosshair quick-open selects the intended actor.
4. Test mouse, keyboard, controller focus, cursor mode, held power-button release,
   disconnect/reconnect, and input restoration after close. Confirm hotkeys do
   not leak, including other native mod openers, mouse side buttons, and gamepad
   chords. Check cursor appearance, sensitivity, hit alignment, held A while
   switching cursor mode, and Print Screen.
5. Test another menu opening, Alt+Tab, load/new game/main menu transitions, and
   repeated open/close. Confirm stale queued actions are discarded.
6. Open Horde, Tailor, and Romantasy in succession. Check rendering/input state
   and each opening action. Test another mod using LB+Y while Horde is closed.
