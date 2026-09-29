# Third-Party Notices

Horde is licensed as described in [LICENSING.md](LICENSING.md). The following third-party components retain their own licenses.

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) is included as a Git submodule and is licensed under GPL-3.0-or-later with its Modding Exception and GPL-3.0 Linking Exception. Horde statically links it. The exact revision and complete license texts are recorded by the submodule.
- [Dear ImGui](https://github.com/ocornut/imgui), pinned to v1.92.6, is statically linked under MIT. See `licenses/Dear-ImGui-MIT.txt`.
- [FreeType](https://freetype.org/), pinned in `xmake-requires.lock`, is statically linked under the FreeType License. Portions of this software are copyright (c) The FreeType Project. All rights reserved. See `licenses/FreeType-FTL.txt`.
- [Poppins](https://github.com/itfoundry/Poppins) and [Montserrat](https://github.com/JulietaUla/Montserrat) are distributed under the SIL Open Font License 1.1. The original Horde font files are retained in `assets/fonts/`; their complete licenses are `licenses/Poppins-OFL.txt` and `licenses/Montserrat-OFL.txt`.
- `assets/fonts/HordeIcons.ttf` converts the original Horde SVG artwork from [Game-icons.net](https://game-icons.net) to monochrome TrueType glyphs. Authors, individual source links, and the CC BY 3.0 license are recorded in `licenses/Game-Icons-Attribution.md` and `licenses/Game-Icons-CC-BY-3.0.txt`.
- [nlohmann/json](https://github.com/nlohmann/json), [spdlog](https://github.com/gabime/spdlog), and [zlib](https://zlib.net/) retain their MIT or zlib licenses, copied into `licenses/`. Versions are pinned in `xmake-requires.lock`.
- The CommonLib dependency graph also includes Microsoft's [DirectXMath](https://github.com/microsoft/DirectXMath) and [DirectXTK](https://github.com/microsoft/DirectXTK). Their MIT licenses are included in `licenses/`.
- The desktop preview uses [stb_image_write](https://github.com/nothings/stb) under its MIT option. It is a development tool and is not included in the installed plugin. Its license is `licenses/stb-LICENSE.txt`.

Editable icon artwork is retained in `assets/icons/`, with its glyph order in `glyphs.json`. Icon regeneration tools are development-only dependencies recorded in `tools/imgui-preview/package-lock.json`.
