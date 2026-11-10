# NOTICE

## Copyright

GBAR5 — the application, its front end, its art and its scripts:

Copyright (C) 2026 Ronin (Thereal47Ronin)
SPDX-License-Identifier: GPL-3.0-or-later

GBAR5 grew out of PSXS5, the same author's PlayStation emulator front end, and
from [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
by BlackBearReloaded. The build pipeline, runtime and deploy tooling in
`tooling/`, `tools/` and parts of `src/platform/` retain their original
copyright and GPL-3.0-or-later notices.

## Licence

The project as a whole is released under the GNU General Public License,
version 3 or later. The full text is in `LICENSE`.

In plain terms:

- You may run, study, share and modify GBAR5.
- If you distribute it, or a modified version, you must make the complete
  corresponding source available under the same licence, and keep the
  copyright and SPDX notices.
- There is no warranty. If it breaks, that is on you.

The GPL covers the code and the original art in this repository. It does not
grant rights to third-party trademarks. See the trademark note below.

## Third-party components

GBAR5 links or vendors work from other projects. Their licences and copyright
notices are listed in `THIRD_PARTY_NOTICES.md` and in the individual
`third_party/*/LICENSE` files. If you redistribute a build, keep those files
and notices with it.

The most important ones:

- **mGBA** (MPL-2.0) — the emulator core.
- **rcheevos** (MIT) — RetroAchievements.
- **libsmb2**, **libnfs** (LGPL-2.1) — network shares.
- **stb**, **miniz**, **qrcodegen**, **dlmalloc** (MIT / public domain) —
  support code.
- **SDL2** (zlib), **curl** (curl licence) — through PacBrew's PS5 ports.
- **RADV / Mesa** (MIT) and the PS5 Vulkan port by mihawk-99 — the Vulkan
  driver.
- **ps5-payload-dev** and **ps5-native-app-boilerplate** — the SDK, runtime
  and packaging tooling.

## No game data

This repository contains no ROMs, no BIOS dumps, no encryption keys and no
cheats with copyrighted content. The app's metadata and thumbnail indexes are
lists of names and URLs, not game data.

Use dumps of cartridges and consoles you own. Do not ask for ROMs here; they
will not be provided.

## Trademarks

GBAR5 is not affiliated with, endorsed by or sponsored by Nintendo or Sony
Interactive Entertainment.

"Game Boy", "Game Boy Advance", "Game Boy Color" and "Nintendo" are trademarks
of Nintendo. "PlayStation", "PS5" and "DualSense" are trademarks of Sony
Interactive Entertainment Inc. They are used in this project only to describe
what the software does and what it runs on.

The GBAR5 name and logo identify this project. Forks are welcome under the GPL,
but please give a fork its own name and icon so it is not mistaken for the
original.

## Donations

GBAR5 is free, and it stays free. If you want to say thanks:
[ko-fi.com/thereal47ronin](https://ko-fi.com/thereal47ronin)
