# Changelog

Horde changes from 3.1.0 on are recorded here. Earlier releases are described
on Horde's Nexus page.

## 3.1.0 - Candidate

### Changed

- Horde's menu is now a web interface hosted by
  [Magelight UI](https://github.com/Severause/MagelightUI) 1.1 or newer:
  the roster, follower details, group orders, dismissed registry, and settings,
  with animated transitions. Magelight UI is required to open the menu.
- The Dear ImGui panel is removed, with its fonts and native input hook. Without
  Magelight UI, opening the menu shows "Horde's menu needs Magelight UI 1.1 or
  newer."; follower tracking, the group powers, and group hotkeys keep working.
  `Horde.log` records whether Magelight UI was found.
- Magelight UI has no controller input on flat Skyrim, so the menu is used with
  a mouse and keyboard. The `controllerGlyphs` setting is gone.

### Added

- Settings > Interface scale, the first option: 70% to 170% in 5% steps,
  default 100%. It is stored as `uiScale` in `settings.json`.
- The package ships the interface under `Magelight/Horde/`.
- The interface follows the game's language (`sLanguage`): the dismissed
  registry sorts names and distances read the way that language expects.
- Cyrillic and Greek headings use a matching condensed font (Fira Sans Extra
  Condensed) instead of a wide fallback.

### Fixed

- A follower, item, perk or home name stored in a legacy code page instead of
  UTF-8 could crash Skyrim when Horde opened or saved. Such characters now show
  as a replacement mark, and the menu and the save carry on.
- Sandboxing works again. Followers with "Idle in safe places" on used to stand
  still: the waiting sandbox package had every location switched off, both
  sandbox packages had wandering disabled, and the vanilla follow package
  overrode Horde's packages for the follower recruited most recently through
  vanilla dialogue. That follower now also uses Horde's follow distance.

### Unchanged

- Follower management and the ESP are unchanged apart from the version and the
  fixes above.

### Compatibility

- Horde does not use PrismaUI.

In-game testing of the interface with Magelight UI is **NOT RUN**.
