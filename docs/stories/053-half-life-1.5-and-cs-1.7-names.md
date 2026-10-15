# 053 - "Half-Life 1.5" and "Counter-Strike 1.7"

## Context
The installer of [052](052-windows-installer.md) and the games it installs
show the names of the original releases: "Half-Life", "Counter-Strike",
"Xash3D FWGS", `cs16-client build 4003`, `v49/0.21 (win32-amd64 build
4215)`. This set is no longer the original game: 64-bit engine, Vulkan
renderer with ray tracing, new menus and utilities. It gets its own names:
**Half-Life 1.5** and **Counter-Strike 1.7**.

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
- [ ] List every place a name or version is shown: installer (`xash3d.iss`,
      `license.txt`), engine (`XASH_ENGINE_NAME`, version strings of
      `host.c` / `console.c`, window title), menus (`mainui_cpp`
      BackgroundBitmap version line, banners), `cs16-client` (game title,
      HUD texts), `hlsdk-portable` if it shows any.
- [ ] Define the names once: a product name and a version per game,
      passed to the builds (waf / CMake options, defaulting to the current
      names so upstream builds are unchanged) and to `iscc` (`/D`).
- [ ] Installer: product "Half-Life 1.5 & Counter-Strike 1.7", components
      and shortcuts renamed, `Setup.exe` named after it, install folder
      `%LOCALAPPDATA%\Programs\Half-Life 1.5`; keep the same `AppId` so an
      installed 052 set is upgraded, not duplicated.
- [ ] Game titles: the installer writes a `gameinfo.txt` in `valve` and
      `cstrike` of the installed set with `title "Half-Life 1.5"` /
      `"Counter-Strike 1.7"` (it takes precedence over `liblist.gam`),
      keeping every other key of the original.
- [ ] Engine and menus: the version line reads "Counter-Strike 1.7
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
