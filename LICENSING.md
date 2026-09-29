# Horde Licensing

Copyright (c) 2026 Heath Brown.

Horde is distributed as `GPL-3.0-or-later` with the additional permissions in [EXCEPTIONS.md](EXCEPTIONS.md). This license covers Horde's source code and the compiled `Horde.dll`.

Horde statically links CommonLibSSE-NG. A compiled native plugin containing CommonLibSSE-NG is a combined work and is not distributed as MIT-only software. The complete GNU GPL version 3 text is in [LICENSE](LICENSE). CommonLibSSE-NG retains its own license and exceptions in the `lib/commonlibsse-ng` submodule.

The native UI embeds Dear ImGui and FreeType and bundles fonts in `assets/fonts/`. Those components retain their own licenses in `licenses/`; the same applies to the other dependencies listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The preferred form for modifying Horde is this repository with the CommonLibSSE-NG submodule initialized at the revision recorded by Git.

Binary release packages must include [LICENSE](LICENSE), [EXCEPTIONS.md](EXCEPTIONS.md), this file, [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and the applicable API, font, and third-party license texts. Corresponding Source for the exact release, including its build files and pinned CommonLibSSE-NG revision, must accompany the binaries or be made available using a method permitted by GNU GPL version 3 section 6.
