# Third-Party Notices

Horde is licensed as described in [LICENSING.md](LICENSING.md). The following third-party components retain their own licenses.

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) is included as a Git submodule and is licensed under GPL-3.0-or-later with its Modding Exception and GPL-3.0 Linking Exception. Horde statically links it. The exact revision and complete license texts are recorded by the submodule.
- The web interface embeds Horde's [Game-icons.net](https://game-icons.net) artwork as SVG paths in `view/icons.js`. Authors, individual source links, and the CC BY 3.0 license are recorded in `licenses/Game-Icons-Attribution.md` and `licenses/Game-Icons-CC-BY-3.0.txt`.
- The web interface (`view/`) uses the [Saira Condensed](https://github.com/Omnibus-Type/Saira), [Fira Sans Extra Condensed](https://github.com/google/fonts/tree/main/ofl/firasansextracondensed) (Cyrillic and Greek subsets) and [IBM Plex Sans and Mono](https://github.com/IBM/plex) web fonts under the SIL Open Font License 1.1. Their complete licenses are `licenses/SairaCondensed-OFL.txt`, `licenses/FiraSansExtraCondensed-OFL.txt` and `licenses/IBM-Plex-OFL.txt`.
- `view/icons.js` includes [Lucide](https://lucide.dev) icons under the ISC License, with icons derived from Feather under MIT. See `licenses/Lucide-ISC.txt`.
- `src/MagelightUIAPI/MagelightUI_API.h` is [Magelight UI](https://github.com/Severause/MagelightUI)'s public API header (commit `9acebb2`, Magelight UI 1.5.1), compiled into `Horde.dll` under the MIT License. See `licenses/MagelightUI-MIT.txt`.
- [nlohmann/json](https://github.com/nlohmann/json) and [spdlog](https://github.com/gabime/spdlog) retain their MIT licenses, copied into `licenses/`. Versions are pinned in `xmake-requires.lock`.
- The CommonLib dependency graph also includes Microsoft's [DirectXMath](https://github.com/microsoft/DirectXMath) and [DirectXTK](https://github.com/microsoft/DirectXTK). Their MIT licenses are included in `licenses/`.

Editable icon artwork is retained in `assets/icons/`. `tools/view/build-icons.mjs` regenerates `view/icons.js` from a lucide-svelte package and `assets/icons/`. `view/dev/` is a development preview and is not installed.
