# Contributing

Bug reports, fixes and testing are all welcome.

**Reporting a bug**

- Include your firmware version, how you loaded etaHEN and kstuff, and the game
  (its name and the four-letter code from the cartridge header).
- Attach `/data/GBAR5/logs/gbar5.log`. After a crash it contains a `crash:`
  section.

**Changing the code**

- Keep the SPDX header (`GPL-3.0-or-later`) on new source files.
- Never commit ROMs, BIOS files, keys, `.env`, `build/`, `dist/` or save files.
  `.gitignore` already covers the usual accidents.
- Build with `make` before opening a pull request. If you touch the platform
  layer, also run `make desktop`.
- Release tags are `vMAJOR.MINOR.PATCH`.
