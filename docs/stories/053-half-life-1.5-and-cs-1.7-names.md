# 053 - "Half-Life 1.5" and "Counter-Strike 1.7"

## Context
The installer of [052](052-windows-installer.md) and the games it installs
show the names of the original releases: "Half-Life", "Counter-Strike",
"Xash3D FWGS", `cs16-client build 4003`, `v49/0.21 (win32-amd64 build
4215)`. This set is no longer the original game: 64-bit engine, Vulkan
renderer with ray tracing, new menus and utilities. It gets its own names:
**Half-Life 1.5** and **Counter-Strike 1.7**.

## Status (2026-10-22)
Done and tested with the installer, except the GoldSrc server check (see
below). The names are defined once in `xash3d.iss` (`HLName`, `CSName`,
`/D` to change them) and reach the games through their `liblist.gam`:

- Installer: "Half-Life 1.5 and Counter-Strike 1.7" (product, programs
  list, Start menu folder, `Half-Life 1.5 and Counter-Strike 1.7
  Setup.exe`), components and shortcuts "Half-Life 1.5", "Counter-Strike
  1.7", "... server", folder `%LOCALAPPDATA%\Programs\Half-Life 1.5`. Same
  `AppId`: a 052 set installed as "Xash3D FWGS" is upgraded in its folder,
  its settings kept, its old Start menu folder and desktop shortcuts
  removed (`UsePreviousGroup=no`, `[InstallDelete]`). The notice that the
  set is unofficial, made by fans, not by Valve, and needs the player's
  Steam copy is on the Steam folder page and at the top of `license.txt`.
- Game titles: the installer rewrites `game` (and Counter-Strike's
  `version "1.7"`) in the installed copy of `liblist.gam` rather than
  writing a `gameinfo.txt`: a `gameinfo.txt` would need every key of
  `liblist.gam` translated to its own names, and the engine rewrites it
  from `liblist.gam` when that one is newer. The window, the taskbar and
  the menus (Half-Life's "Learn how to play Half-Life 1.5") read it.
- Engine: `waf configure --branded` (off by default) makes the corner line
  of the menus and screenshots "<game title> (unofficial build N)"; the
  console's banner, `version`, the server, master and HTTP strings keep
  Xash3D FWGS, its protocol and build number. Counter-Strike's menus:
  `-DMAINUI_CLIENT_BUILD_LINE=OFF` drops their "cs16-client build N" line.
  `stage.ps1` builds both so.

Tested on 2026-10-22: the 052 installer (0.22) installed silently, then
this one over it without `/DIR`: same folder, the old shortcuts gone, the
new ones in "Half-Life 1.5 and Counter-Strike 1.7" and on the desktop,
`config.cfg` kept; window titles "Counter-Strike 1.7" and "Half-Life 1.5",
menu corners "Counter-Strike 1.7 (unofficial build 4221)" and "Half-Life
1.5 (unofficial build 4221)", no cs16-client line; the set's client joined
the set's dedicated server (team menu); a fresh install of Half-Life alone
went to `Programs\Half-Life 1.5` (no empty `cstrike` or `tools` folder any
more); both uninstalls left nothing in the Start menu, desktop or programs
list.

Not checked: joining a GoldSrc (ReHLDS) server and an Xash3D server over
the network, both offline that night; nothing on the network path
changed. Demos: a de_dust2 demo recorded before 065 (`arte.dem`) refuses
the set's de_dust2 with the crates ("Your map [de_dust2] differs from the
server's"), as it would in the 065 play set: a consequence of 065, not of
the names. Since then (2026-10-22), a demo plays on a map changed
since it was recorded, with a warning: `arte.dem` plays to its end on the
de_dust2 with the crates.

## Goal
Everywhere a player reads a name or a version, the set says Half-Life 1.5
or Counter-Strike 1.7, while servers, demos and game folders keep working
with every other client.

## Scope
In, what players see:
- The installer: product name, wizard titles and texts, components,
  shortcuts ("Half-Life 1.5", "Counter-Strike 1.7"), the Start menu folder,
  the installed programs list, `Setup.exe` file name, default install
  folder.
- Window titles and the taskbar (the game title of `gameinfo.txt` /
  `liblist.gam` of the installed set, the engine's window title).
- Menus: main menu version line (`cs16-client build ...`), the engine's
  version string in the corner (`v49/0.21 ...`), the "about" texts.
- Documentation for players (052's page), the README section of the fork.

Out, kept as they are (compatibility):
- The game folders `valve` and `cstrike`: the server sends its game folder
  name, a client must have the same one to join; demos and maps refer to
  them.
- The protocol version (48/49), `liblist.gam`'s `gamedll` entries, the
  library names (`client_amd64.dll`, `mp_amd64.dll`...), the user agent
  and the build number servers and master servers may check.
- Valve's own files copied from Steam (titles, strings inside resources):
  only overridden in the installed set (see tasks), never edited in place.

## Tasks
- [x] List every place a name or version is shown: installer (`xash3d.iss`,
      `license.txt`), engine (`XASH_ENGINE_NAME`, version strings of
      `host.c` / `console.c`, window title), menus (`mainui_cpp`
      BackgroundBitmap version line, banners), `cs16-client` (game title,
      HUD texts), `hlsdk-portable` if it shows any.
- [x] Define the names once: a product name and a version per game,
      passed to the builds (waf / CMake options, defaulting to the current
      names so upstream builds are unchanged) and to `iscc` (`/D`).
- [x] Installer: product "Half-Life 1.5 & Counter-Strike 1.7", components
      and shortcuts renamed, `Setup.exe` named after it, install folder
      `%LOCALAPPDATA%\Programs\Half-Life 1.5`; keep the same `AppId` so an
      installed 052 set is upgraded, not duplicated.
- [x] Game titles: the installer writes a `gameinfo.txt` in `valve` and
      `cstrike` of the installed set with `title "Half-Life 1.5"` /
      `"Counter-Strike 1.7"` (it takes precedence over `liblist.gam`),
      keeping every other key of the original.
- [x] Engine and menus: the version line reads "Counter-Strike 1.7
      (build N)" / "Half-Life 1.5 (build N)" in the menus; the engine keeps
      its protocol and build number where servers read them.
- [ ] Check: a server browser, a GoldSrc and an Xash3D server, demos
      recorded before and after, all still work; nothing shows the old
      names in the installer, the menus or the window.

## Acceptance criteria
- Installed with the 052 installer, the player only reads "Half-Life 1.5"
  and "Counter-Strike 1.7" (installer, shortcuts, programs list, window,
  menus).
- The set still joins the servers it joined before, plays the demos it
  played before, and an earlier installed set is upgraded in place.
- Builds without the new options keep the current names.

## Dependencies
052.

## Risks / notes
- "Half-Life" and "Counter-Strike" are Valve's trademarks: names like
  "Half-Life 1.5" and "Counter-Strike 1.7" could pass for official Valve
  releases. The installer and the menus should say clearly that this is an
  unofficial, fan-made set that needs the player's own Steam copy; and the
  set must not be distributed publicly under these names without thinking
  this through.
- Servers that list their clients' versions (some admin plugins) may show
  the new version strings; harmless.
