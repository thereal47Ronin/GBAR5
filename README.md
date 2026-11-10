# GBAR5

**Game Boy Advance emulation, running natively on a jailbroken PS5.**

GBAR5 is a homebrew app that installs on the PS5 home screen as a real title
(`PPSA97511`). The emulator is [mGBA](https://mgba.io/) — its libretro core,
built for the PS5's CPU — with a front end written for the console: a shelf of
your games, save states, cheats, RetroAchievements, a phone-friendly settings
page and a picture drawn through Vulkan.

Nothing is streamed, and no PC is needed while you play.

- [What you get](#what-you-get)
- [Requirements](#requirements)
- [Installing](#installing)
- [Your games](#your-games)
- [Controls](#controls)
- [Settings](#settings)
- [RetroAchievements](#retroachievements)
- [Cheats](#cheats)
- [Settings from your phone](#settings-from-your-phone)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Legal](#legal)

---

## What you get

- **A real home-screen app.** Title `PPSA97511`, its own icon and artwork,
  installed from `/data/homebrew/PPSA97511/`. Not a payload, not a stream.
- **The mGBA core.** Accurate Game Boy Advance and Game Boy emulation through
  libretro, statically linked into the app.
- **A shelf, not a file list.** A cover flow and four other layouts, your own
  cover art, and a screenshot from inside the game on the shelf card.
- **Save states.** Ten slots per game with thumbnails, plus quick resume: leave
  a game and the shelf offers *Continue* where you were, or *Start over*.
- **Rewind and fast forward.** Hold the touchpad and press L2 or R2.
- **Cheats.** The libretro-database Game Boy Advance cheat library is matched to
  the cartridge you started (region and revision), downloaded on first use, and
  toggled from the in-game menu. Your own `.cht` files work too.
- **RetroAchievements.** Sign in once from a PC; only a token reaches the
  console. Unlocks and progress pop up while you play.
- **A settings page on your phone.** Off by default. Turn it on and scan the QR
  on the console: that phone gets the settings page on the same network.
- **Ten themes and five shelf layouts**, including a light "Pocket" theme and a
  custom theme built from two colours you pick.
- **Upscaling and CRT.** Sharp, Scale2x/3x or xBR prescale, integer scaling,
  scanlines, a soft glow or a 90s TV frame, and colour correction.
- **Nine languages.** English, French, German, Spanish, Italian, Portuguese,
  Simplified Chinese, Korean and Japanese.
- **Profiles.** Each person keeps their own saves, states, settings, play time
  and RetroAchievements sign-in.
- **Games from a PC or NAS.** SMB and NFS shares are read directly, no copying.
- **Optional real BIOS.** Drop in your own `gba_bios.bin` to use the hardware
  boot sequence; otherwise mGBA's built-in HLE start is used.
- **An honest update checker.** Settings → About asks GitHub for a newer
  release and can install it.

## Requirements

- **A jailbroken PS5** with [etaHEN](https://github.com/etaHEN/etaHEN) and
  kstuff loaded. Development and testing happen on firmware 9.40.
- **[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)**, loaded at
  every boot. It puts GBAR5 on the home screen from
  `/data/homebrew/PPSA97511/`.
- **An FTP client** — etaHEN's FTP server on port 2121 (FileZilla works), or a
  command-line client.
- **Your own cartridge dumps.** GBAR5 ships no ROMs, BIOS files or keys, and
  never will.

## Installing

There is no store, no package and no installer: the app is a folder.

1. Get a release (or build it yourself, below). The release zip contains a
   `PPSA97511` folder.
2. Copy that folder to `/data/homebrew/` on the console over FTP.
3. Set its permissions to `777`, recursively. In FileZilla: right-click the
   folder → *File permissions* → `777`, and tick *Recurse into subdirectories*.
   Without this the PS5 says *"Can't start the game or app"* (CE-107750-0).
4. Restart ShadowMountPlus (or reboot). **Game Boy Advance** appears on the home
   screen.
5. Start it once. It creates `/data/GBAR5/` and an empty shelf that tells you
   where games go.

The app's data lives in `/data/GBAR5/`:

```text
/data/homebrew/PPSA97511/     the app (eboot.bin, assets/, sce_sys/)
/data/GBAR5/
├── games/<Game name>/        your games, one folder per game
├── bios/                     optional: your own gba_bios.bin
├── saves/                    battery saves, one per game
├── states/                   save states, ten slots per game
├── cheats/                   the .cht library (downloaded, and your own)
├── covers/                   downloaded and custom cover art
├── logs/gbar5.log            the session log (send this with bug reports)
├── gbar5.ini                 settings
├── library.txt               game list used when /data can't be listed
├── network.cfg               optional SMB/NFS shares
└── retroachievements.ini     RetroAchievements token (main profile)
```

If etaHEN does not unlock `/data`, GBAR5 can still run: it reads
`library.txt` instead of listing the games folder. The app writes that list when
you use the PC tool, or you can create it yourself.

## Your games

One folder per game, with the ROM inside:

```text
/data/GBAR5/games/Pokemon Emerald (USA, Europe)/Pokemon Emerald (USA, Europe).gba
/data/GBAR5/games/Golden Sun (USA, Europe)/Golden Sun (USA, Europe).gba
```

- **Formats:** `.gba` and `.agb`. Game Boy and Game Boy Color ROMs also run in
  the mGBA core.
- **Names matter a little.** GBAR5 reads the title and game code from the
  cartridge header, and uses the file name as the stable key for metadata and
  art. No-Intro names work best; renamed dumps still run.
- **Metadata is filled in for you.** `assets/gba-gamedb.txt` holds release
  dates, genres, developers, publishers and player counts for 2,374 Game Boy
  Advance games. Add your own lines to `/data/GBAR5/gba-metadata.txt`; it wins.
- **Cover art** is matched by the ROM's file name against
  [libretro-thumbnails](https://github.com/libretro-thumbnails)' Game Boy
  Advance set, and cached in `/data/GBAR5/covers/`. Your own art wins: put
  `cover.png` or `cover.jpg` in the game's folder.
- **Screenshots on the shelf** come from the same thumbnail set and are cached
  in `/data/GBAR5/art/snaps/`.

### PC helper

`tools/gbar5_sync.py` (Python 3, plus 7-Zip for archives) prepares a folder of
archives and uploads it over FTP:

```bash
python tools/gbar5_sync.py prepare --source "D:\GBA"          # unpack to GBAR5_ready/
python tools/gbar5_sync.py covers  --stage "D:\GBAR5_ready"   # fetch box art
python tools/gbar5_sync.py upload  --stage "D:\GBAR5_ready" --host <console IP>
```

`upload` mirrors the tree into `/data/GBAR5/games/` and writes `library.txt`.

## Controls

**On the shelf**

| Button | Does |
|---|---|
| D-pad / left stick | Move around the shelf |
| Cross | Play |
| Triangle | Details; Square picks a cover in Details |
| Square | Settings |
| R3 | Favorite / hide, in Details |
| L1 / R1 | Category |
| OPTIONS | Sort order |
| Touchpad | Surprise me — a random game |
| Circle | Back |

**In a game**

| Button | GBA |
|---|---|
| Circle | A |
| Cross | B |
| L1 / R1 | L / R |
| Triangle | Turbo A |
| Square | Turbo B |
| L2 / R2 | Turbo L / Turbo R |
| OPTIONS | Start |
| Touchpad click | Select |
| Touchpad (hold 0.4 s) | The GBAR5 menu |
| Hold touchpad + R2 | Fast forward |
| Hold touchpad + L2 | Rewind (turn it on in System) |
| Hold touchpad + Square | Screenshot |
| Hold touchpad + Triangle / Circle | Start or pause / reset the run timer |
| R3 / L3 | Brighten / darken the solar sensor |

The **in-game menu** has resume, save and load state (ten slots and the
auto-save), reset, cheats, achievements, the game's manual and text guides,
settings, and quit to the shelf.

**Text guides:** a `.txt` walkthrough in the game's folder, or in a `guides/`
folder inside it. **Manuals:** page images (`1.jpg`, `2.jpg`, …) in
`<game folder>/manual/`.

## Settings

Six sections, all saved only when you press **Save**:

| Section | Settings |
|---|---|
| Screen | Aspect ratio, upscale filter, prescale, integer scaling, smoothing, scanlines, border, brightness, colours, FPS counter |
| Controls | Button mapping, left stick as D-pad, rumble strength, light bar, trigger effects, stick dead zone and response |
| Sound | Master volume, music, effects, UI sounds, low-pass filter, audio range |
| Shelf | Theme, layout, backdrop, cover style, cover downloads, sort order, hidden games, rescan |
| System | Quick resume, rewind, auto-save, frameskip, colour correction, interframe blending, real BIOS, skip boot logo, solar sensor, Game Boy Player rumble, opposing directions, profiles, language, data location, network shares, phone settings page |
| About | Version, games, data folder, update check, GitHub QR, Ko-fi QR |

## RetroAchievements

GBAR5 never asks for your password on the console. Sign in once from a PC:

```bash
python tools/gbar5_ra_login.py --host <console IP>
```

The tool swaps your password for a login token and writes only the token to
`/data/GBAR5/retroachievements.ini`. Restart GBAR5 and it signs in by itself.

With several profiles (Settings → System → Who's playing), sign each one in
separately:

```bash
python tools/gbar5_ra_login.py --host <console IP> --profile "Player 2"
```

Hardcore mode is a switch in Settings → System. It disables save states and
cheats, as RetroAchievements requires.

## Cheats

When a game starts, GBAR5 matches it against libretro-database's Game Boy
Advance cheat files: same region, same revision. The matching file is downloaded
on first use. Open the in-game menu → **Cheats** to switch codes on and off; your
choice is remembered per game.

To use your own codes, put a `cheats.cht` in the game's folder, or any `.cht` in
`/data/GBAR5/cheats/`. *Find a code* searches the game's memory and saves a
result as a CodeBreaker code.

## Settings from your phone

Off unless you turn it on (Settings → System → Settings from your phone). When
it is on, the About screen shows a QR code carrying the page's address and a
one-time code. Scan it with a phone on the same network and you get the settings
page, save states and covers.

- The code is generated per console and rotates when you turn the page off and
  on again.
- Only the QR (or the exact address) opens it.
- The HTTP server listens on port 8095; nothing is exposed outside your LAN.

## How it works

GBAR5 is a C application, not a wrapper around another emulator front end.

### Boot

1. `main()` (`src/main.c`) opens the boot log first, so a crash before the
   interface is still recorded.
2. The data root is chosen: `/data/GBAR5`, or a USB/extended drive if
   `location.txt` names one.
3. The platform layer (`src/platform/plat_sdl.c`) starts SDL, creates the
   window, and opens the Vulkan presenter (`src/platform/vk/`).
4. The library is scanned, the theme and settings are loaded, and the shelf is
   drawn. A game is loaded into mGBA only when you press Play.

### The emulator

`src/core/host.c` is a small libretro host:

- it links `third_party/mgba` (built as `mgba_libretro.a`) and calls the core's
  `retro_init`, `retro_load_game`, `retro_run`;
- it translates the PS5 pad into RetroPad buttons, and the DualSense triggers
  and solar sensor into mGBA's extra inputs;
- it receives video frames and audio samples, and hands them to the platform
  layer;
- it persists battery saves to `/data/GBAR5/saves/` and exposes the memory map
  that `rcheevos` needs for achievements.

mGBA's own core options (colour correction, interframe blending, idle
optimisation, frameskip, BIOS use, solar level, Game Boy Player rumble, low-pass
filter) are set from `Settings` before each load.

### The picture

The game's 240×160 frame is prescaled on the CPU (sharp, Scale2x/3x or xBR),
then drawn by a Vulkan swapchain through the PS5's RADV driver. The UI uses the
same presenter: rounded cards, gradients, text and textures are drawn into one
frame. A capture path reads the swapchain back for screenshots and shelf
thumbnails.

### Storage and profiles

`src/config.c` defines the data layout. Profiles are folders under
`/data/GBAR5/profiles/`; the main profile is the root itself. Saves, states,
game-specific settings and achievement logs are resolved through the active
profile, so two people never overwrite each other.

Games are scanned from `games/`, an attached USB drive, or SMB/NFS shares
(`src/netfs.c`, backed by libsmb2 and libnfs). When `/data` can't be listed,
`library.txt` is used instead.

### The phone page

`src/remote.c` runs a small HTTP server on port 8095 in its own thread. The
token in the QR is stored in `/data/GBAR5/remote.token`; the page sets it as a
cookie, and every request is checked against it. It serves the settings form,
cover uploads, save-state thumbnails and the RetroAchievements sign-in.

### Updates

`src/update.c` asks the GitHub releases API for
`thereal47Ronin/GBAR5`, compares versions, downloads the release zip, verifies
it with miniz, and writes the `PPSA97511` folder into place with the same
permission rules as a manual install.

### Source map

| Path | What |
|---|---|
| `src/main.c` | entry point, screens, game start/stop |
| `src/app.h` | shared application state |
| `src/library.c` | scanning games, reading cartridge headers |
| `src/gbadb.c` | metadata from `assets/gba-gamedb.txt` |
| `src/config.c` | settings and the data layout |
| `src/profiles.c` | per-person saves, states and settings |
| `src/core/host.c` | the libretro host and mGBA glue |
| `src/play.c` | save states, screenshots, quick resume |
| `src/cheats.c` | matching, downloading and applying cheats |
| `src/ra/` | RetroAchievements through rcheevos |
| `src/netfs.c`, `src/net.c` | SMB/NFS shares and HTTP |
| `src/remote.c` | the phone settings page |
| `src/update.c` | GitHub update check and install |
| `src/ui/` | shelf, menus, settings, themes, text, sound |
| `src/platform/` | PS5 video, input, audio, heap, elevation |

## Building from source

The app builds on Linux or WSL (Ubuntu 24.04 is what CI and development use).
You need Clang 18, lld, Ninja, CMake, Python 3 and the usual build tools:

```bash
sudo apt install clang-18 lld-18 llvm-18 make ninja-build cmake ccache \
  pkg-config python3 python3-venv tar unzip wget zstd
```

Then:

```bash
git clone https://github.com/thereal47Ronin/GBAR5.git
cd GBAR5
make            # -> dist/PPSA97511/
```

`make` builds mGBA, rcheevos, the network-share libraries and the RADV Vulkan
driver, compiles `src/`, links and signs `eboot.bin`, and assembles the app
folder. The first build downloads the PS5 payload SDK and PacBrew's SDL2 and
libcurl into `.deps/` (all ignored by Git).

A Docker image is the reproducible path used during development:

```bash
docker build -t gbar5-build -f .build/Dockerfile .   # or use an Ubuntu 24.04 image
docker run --rm -v "$PWD:/work" -w /work -e BUILD_JOBS=8 gbar5-build \
  bash -lc 'git config --global --add safe.directory "*" && bash /work/.build/docker-app.sh'
```

There is also a desktop build of the same front end for testing on a PC:

```bash
make desktop
./build/desktop/psxs5
```

To deploy over FTP, set `PS5_HOST` and run `make deploy`; the console's FTP
server must be running (default port 2121, user `anonymous`, password `codex`).

> **Note on the built-in boot intro.** The retail Game Boy Advance startup
> animation and chime are Nintendo's, so they are not in this repository. The
> source ships a silent, neutral placeholder; supply your own `gba_bios.bin` to
> see the real boot sequence, or generate an intro from your own footage.

## Repository layout

```text
src/                the app
  core/             libretro host and mGBA glue
  platform/         PS5 video, input, audio, heap, elevation, Vulkan
  ra/               RetroAchievements
  ui/               shelf, settings, menus, themes
assets/             fonts, metadata lists, thumbnail indexes, QR codes
sce_sys/            the home-screen icon and artwork, param.json
tools/              build scripts, PC helpers, art and data generation
third_party/        mGBA, rcheevos, libsmb2, libnfs, stb, miniz and friends
docs/               platform notes and build documentation
examples/           boilerplate examples for the PS5 app layer
tests/              host-side tests
```

## Legal

GBAR5 is free software under the **GNU General Public License, version 3 or
later** (`LICENSE`). You may use, study, share and modify it; if you distribute
a modified version, you must publish its source under the same licence and keep
the copyright notices. That is the deal the GPL makes, and it is why this repo
is public.

GBAR5 is built on the work of other people, including
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
mGBA, rcheevos, libsmb2, libnfs, SDL2, curl, stb, miniz and the RADV port. Their
licences are in `THIRD_PARTY_NOTICES.md`; keep those notices if you fork it.

**No ROMs, BIOS files, keys or cheats with copyrighted content are included
here, and none will be provided.** Use dumps of cartridges and hardware you own.

GBAR5 is not affiliated with, endorsed by or sponsored by Nintendo or Sony
Interactive Entertainment. "Game Boy", "Game Boy Advance" and "Nintendo" are
trademarks of Nintendo. "PlayStation" and "PS5" are trademarks of Sony
Interactive Entertainment Inc. They are used here only to describe what the
software does.

The GBAR5 name and logo identify this project. If you fork it, please give your
fork a different name and icon so people don't confuse it with the original.

**If GBAR5 is useful to you, you can buy me a coffee:**
[ko-fi.com/thereal47ronin](https://ko-fi.com/thereal47ronin)
