# Third-party notices

## Credits and acknowledgements

| Project | Role |
| --- | --- |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) | Public PS5 headers, libc++ headers, sysroot, and Clang target support |
| [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo) | Optional prebuilt PS5 ports and static libraries |
| [SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero) | Source of the ELF converter and FSELF writer in `tooling/native/` (GPL-3.0) |
| [SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) | Optional UFS2 `.ffpkg` generation |
| [PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) | Optional compressed `.ffpfsc` generation |
| [sinajet/PSFFPKG](https://github.com/sinajet/PSFFPKG) | Public `.ffpkg` procedure used as a format reference |
| [LLVM/Clang](https://github.com/llvm/llvm-project) | Native compiler |
| [GoogleTest](https://github.com/google/googletest) | Pinned host-only C++ unit-test framework |
| [zlib](https://zlib.net/) | Pinned source-built compression library used by the host FSELF tool |
| [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) | `texconv` presentation-image preparation |
| [FFmpeg](https://ffmpeg.org/) | Developer-supplied selection-audio preparation |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Directory-style deployment and hardware validation |
| [ArkSama/PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon) | Original Lapy project and owned-root design |
| [mpereiraesaa/PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon) | Exact-title one-shot helper and cooperative elevation protocol |

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application does not redistribute or dynamically load the
complete libc++ or libc++abi archives.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PS5-Lapy-JB-Daemon integration

The sandbox-elevation build fetches
[mpereiraesaa's PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon)
at commit `54a095c0f19161825e845daa760a03b446e654fa`, invokes its unmodified
`owned-helper` target for the selected application's exact title and packages
the generated helper ELF with Lapy's MIT license. The shared protocol header
published upstream is LGPL-2.1-or-later; this repository's application-side
wire implementation is GPL-3.0-or-later.

Lapy was created by
[ArkSama / Team PHU](https://github.com/ArkSama/PS5-Lapy-JB-Daemon). Credit
belongs to ArkSama, mpereiraesaa and the Lapy contributors. No Lapy kernel
source is copied or modified here.

The helper build also uses the pinned `ps5log/1` header from
[mpereiraesaa/ps5-agc-gears](https://github.com/mpereiraesaa/ps5-agc-gears/tree/1ae1f9182abd2770c131b97419034fb85173c2dc/native/ps5log),
GPL-3.0-or-later, and the official PS5 Payload SDK v0.40. Those build inputs
remain under ignored `.deps/lapy/`; the normal application toolchain remains
the separately pinned Payload SDK v0.42.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

The update-check example (`make update-check-example`) selects `libcurl`, which
statically links these PacBrew libraries into the example title. Its build
output stays under ignored `dist/` and is not distributed by this repository; an
application that ships the update check ships them and must carry their notices:

| Component | Version in PacBrew v0.40.2 | License |
| --- | --- | --- |
| [libcurl](https://curl.se/) | 8.18.0 | curl license (MIT/X derivative) |
| [OpenSSL](https://www.openssl.org/) | 3.5.2 | Apache License 2.0 |
| [zlib](https://zlib.net/) | 1.3.2 | zlib license |
| [zstd](https://github.com/facebook/zstd) | 1.5.6 | BSD-3-Clause (dual-licensed with GPL-2.0) |
| [libpsl](https://github.com/rockdaboot/libpsl) | 0.21.5 | MIT; built-in Public Suffix List data MPL-2.0 |

`examples/update-check/console_curl.c`, which makes these libraries run in a
native title, is original BlackBearReloaded code (GPL-3.0-or-later), taken from
the ProsperoRadio and ProsperoLichess projects. Its `gmtime_r` follows Howard
Hinnant's public-domain `civil_from_days` algorithm.

## Vendored miniz

`third_party/miniz/` holds [miniz](https://github.com/richgel999/miniz) 3.0.2
(commit `293d4db1b7d0ffee9756d035b9ac6f7431ef8492`), MIT, unmodified, with its
`LICENSE`; `SOURCE.json` records each file's SHA-256. Only the self-update
helper (`examples/self-update-helper`) and its host test link it; an
application that ships that helper ships miniz and must carry its notice. The
helper's archive validation and file helpers are original BlackBearReloaded
code (GPL-3.0-or-later), taken from the ProsperoStore project.

## Optional UFS2Tool dependency

When `.ffpkg` output is requested, the platform bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache and builds it with the host .NET SDK. UFS2Tool is
BSD-2-Clause software and is not distributed by this repository.

## Optional MkPFS dependency

When `.ffpfsc` output is requested, the platform bootstrapper fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies into an ignored virtual environment
there. MkPFS and its dependencies retain their own licenses and are not
distributed by this repository.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

No proprietary runtime module, encryption key, or game file is included.

## GBAR5 additions

GBAR5 is the Game Boy Advance front end built on the boilerplate above. Its
emulator and its extra libraries are these. The full licence text for each is in
its folder under `third_party/`.

| Component | Use | Licence |
| --- | --- | --- |
| [mGBA](https://github.com/libretro/mgba) (libretro fork, commit `7a12d6d`) | Game Boy Advance / Game Boy emulation core (`third_party/mgba`), with small PS5 portability fixes | MPL-2.0 (`third_party/mgba/LICENSE`) |
| [libretro API](https://www.libretro.com/) | the interface between the front end and the core | interface only; no libretro code is bundled |
| [rcheevos](https://github.com/RetroAchievements/rcheevos) | RetroAchievements client (`third_party/rcheevos`) | MIT |
| [libsmb2](https://github.com/sahlberg/libsmb2) | SMB network shares (`third_party/libsmb2`) | LGPL-2.1-or-later |
| [libnfs](https://github.com/sahlberg/libnfs) | NFS network shares (`third_party/libnfs`) | LGPL-2.1-or-later |
| Frame generation from [PS5SX2](https://github.com/Swordpdf/PS5SX2) by Swordpdf | Vulkan frame interpolation (`third_party/framegen`) | GPL-3.0-or-later |
| [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) 2.3.0 | FSR 3 optical flow and frame interpolation shaders (`third_party/framegen/framegen`) | MIT |
| [stb_image, stb_truetype](https://github.com/nothings/stb) | image decoding and text (`third_party/stb`) | Public domain / MIT |
| [miniz](https://github.com/richgel999/miniz) | reading release zips (`third_party/miniz`) | MIT |
| [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki | QR codes on Settings → About and the phone page (`third_party/qrcodegen`) | MIT |
| [dlmalloc](https://gee.cs.oswego.edu/dl/html/malloc.html) 2.8.6 by Doug Lea | allocator for the direct-memory heap (`third_party/dlmalloc`) | Public domain / CC0 |
| [SDL2](https://www.libsdl.org/) (PacBrew PS5 port) | events, controllers, audio, software renderer | zlib |
| [curl](https://curl.se/) (PacBrew PS5 port) | HTTP for updates, covers and achievements | curl licence |
| [Mesa RADV](https://gitlab.freedesktop.org/mesa/mesa) via mihawk-99's [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | the Vulkan driver linked into the app | MIT |
| [ps5-payload-dev](https://github.com/ps5-payload-dev/sdk) | public SDK headers and toolchain | see upstream |
| [Inter](https://github.com/rsms/inter) | interface font (`assets/fonts`) | SIL OFL 1.1 |
| [Noto Sans JP / SC / KR](https://github.com/notofonts/noto-cjk) (subsets by `tools/make_lang_fonts.py`) | Japanese, Chinese and Korean text (`assets/fonts`) | SIL OFL 1.1 |
| [Tabler Icons](https://tabler.io/icons) 3.31.0 (subset by `tools/make_icon_font.py`) | interface icons (`assets/fonts`) | MIT |
| xBR by Hyllian | upscale filter, reimplemented in `src/platform/xbr.c` | algorithm credit |
| Scale2x / Scale3x by Andrea Mazzoleni (AdvanceMAME) | upscale filter, reimplemented in `src/platform/plat_sdl.c` | algorithm credit |
| [libretro-database](https://github.com/libretro/libretro-database) | `.cht` cheat files, downloaded at runtime; not bundled | see that repository |
| [libretro-thumbnails](https://github.com/libretro-thumbnails) | box art and screenshots, downloaded at runtime; not bundled | see that repository |
| [LaunchBox Games Database](https://gamesdb.launchbox-app.com/) | source of `assets/gba-gamedb.txt` (release dates, genres, publishers) | data, not source; see that site |
| [RetroAchievements](https://retroachievements.org) | achievement sets and accounts, used online | service terms of retroachievements.org |

`assets/gba-gamedb.txt` is a data file built from the LaunchBox Games Database.
It is not covered by this project's GPL licence; keep its attribution if you
redistribute it.

## Boot assets

The built-in boot animation and chime in the public source tree are a silent,
neutral placeholder. The retail Game Boy Advance startup animation and sound are
Nintendo's and are not distributed here. Use a `gba_bios.bin` dumped from
hardware you own to see the real boot sequence.
