# 052 - Windows installer for Half-Life and Counter-Strike

## Context
Playing this fork on Windows takes a hand-made set today: the 64-bit
engine (`win64` install), `cs16-client` built for x64 (client, menu,
ReGameDLL_CS, YaPB), `SDL2.dll`, the game data of a Steam install passed
with `-rodir`, `XASH3D_BASEDIR` set, and a few settings typed in the
console. A player shouldn't have to know any of it.

The installer is built by the CI, once the CI is turned on for this fork.
Until then it is built and tested by hand: `scripts/installer/windows`.

## Status (2026-10-15)
Made and tested locally (Inno Setup 6, `scripts/installer/windows`): the
wizard, Steam detection, copy without Valve's libraries, first-run
`video.cfg`, app-local MSVC runtime, shortcuts with `-dll @yapb` for the
bots, uninstaller. A silent install found the Steam folder on its own,
installed 806 MB in 9 seconds, Counter-Strike started with Vulkan, ray
tracing and bots, Half-Life started c1a0; the uninstall removed everything
but the player's `video.cfg`. 
2026-10-21: de_dust2 with story 065's crates written at install time
(`patch_dust2.exe`); the player's Steam `config.cfg` on a first install
only, with `hud_utf8 1` and F12 for a screenshot, kept on a reinstall and
by the uninstaller unless asked; the progress bar checked and a cancel in
the middle of the copy leaving nothing; the players' page,
`Documentation/windows-installer.md`. Left: the CI job (not for now), the
fresh Windows 10 and 11 machines of the acceptance criteria.

## Goal
One `Setup.exe` for Windows 10 and 11, 64-bit: the player picks the Steam
folder of Half-Life (and Counter-Strike), picks where the game goes, and
after a minute launches Half-Life or Counter-Strike from a shortcut, with
nothing else to install or type. It works out of the box.

## Scope
In:
- A wizard: welcome, license (GPL-3.0 of the engine; the game data stays
  Valve's), Steam folder, destination folder, games, shortcuts, install,
  finish with "Launch Counter-Strike".
- The 64-bit engine with both renderers, `hlsdk-portable` (Half-Life) and
  `cs16-client` (Counter-Strike: client, menu, ReGameDLL_CS, YaPB with its
  graphs) built for x64, `SDL2.dll`, the MSVC runtime.
- Copying the game data from the Steam install.
- An uninstaller.
- The CI job that builds it all and publishes `Setup.exe`.

Out:
- Valve's 32-bit libraries and the original Counter-Strike client with
  VGUI2 (story 033): they need the 32-bit engine and Steam running.
- Shipping any Valve file: the data always comes from the player's own
  Steam install.
- Other platforms; an auto-updater; code signing (see risks).

## Tasks
- [x] Choose the tool: Inno Setup (free, a single script, `iscc` runs on
      GitHub's Windows runners, Pascal script for the checks below). NSIS
      or WiX only if Inno Setup can't do something listed here.
- [x] Steam folder page:
  - [x] Find it on its own: `HKCU\Software\Valve\Steam\SteamPath`, then
        every library of `steamapps/libraryfolders.vdf`, and the Half-Life
        folder from `appmanifest_70.acf` (Counter-Strike: app 10, same
        folder).
  - [x] Let the player browse to another folder (a non-Steam copy of the
        game too).
  - [x] Check it before going on: `valve\liblist.gam` and `valve\halflife.wad`
        must be there; Counter-Strike can only be ticked when
        `cstrike\liblist.gam` is. Clear messages when something lacks.
- [x] Destination page: per-user, no administrator rights, default
      `%LOCALAPPDATA%\Programs\Xash3D FWGS`: the engine writes its
      configuration, saves, screenshots and demos next to itself, which
      `Program Files` doesn't allow. Show the space needed (game data
      about 1 GB with Counter-Strike).
- [x] Games page: Half-Life, Counter-Strike (ticked when found), each with
      its shortcuts.
- [x] Install:
  - [x] Unpack the engine and the game libraries into the destination.
  - [x] Copy `valve` and `cstrike` from the Steam folder, skipping the
        Valve libraries this set replaces (`cl_dlls\client.dll`,
        `dlls\*.dll`, `hl.dll`, `vgui*.dll`, Steam's `*.exe`). Copied, not
        read in place with `-rodir`: the set must keep working when Steam
        updates, moves or uninstalls the game.
  - [x] A progress bar for the copy; cancelling leaves nothing behind.
- [x] Out of the box settings, written as the default configuration of a
      first run (never over an existing one, so a reinstall keeps the
      player's settings):
  - [x] renderer Vulkan when a Vulkan device is there, else OpenGL (the
        engine already falls back on its own when Vulkan fails);
  - [x] preset `classic`, `cl_charset utf-8`, `hud_utf8 1`, screen
        resolution of the desktop, fullscreen;
  - [x] `bind F12 snapshot` as well as F5.
- [x] Runtime dependencies: link the MSVC runtime statically (`/MT`) or
      ship the VC++ redistributable and run it silently when missing;
      check a fresh Windows 10 has every DLL the set imports (`dumpbin
      /dependents` in the CI).
- [x] Shortcuts: Start menu and, when asked, desktop: "Half-Life"
      (`xash3d.exe -game valve`), "Counter-Strike" (`xash3d.exe -game
      cstrike`), with the games' icons; "Uninstall".
- [x] Finish page: "Launch Counter-Strike" (or Half-Life) ticked.
- [x] Uninstaller: removes what it installed; asks before removing what the
      player made (configuration, saves, screenshots, demos).
- [ ] CI job (Windows runner): build the engine x64 (waf, Vulkan SDK for
      `glslc`), `hlsdk-portable` and `cs16-client` x64 (CMake), stage the
      files, run `iscc`, publish `Xash3D-FWGS-Setup-<version>.exe` as an
      artifact and on releases.
- [x] A page in `Documentation/` for players: what the installer does, what
      it needs (Steam copies of the games), where things go.

## Acceptance criteria
- On fresh Windows 10 and Windows 11 virtual machines with Steam, Half-Life
  and Counter-Strike installed and nothing else (no Visual Studio, no
  Vulkan SDK): the installer finds the Steam folder on its own, installs
  without administrator rights, and "Launch Counter-Strike" starts the
  game to its menu with no error box and no missing DLL.
- From that install: a local game with bots, joining an Xash3D server, the
  Vulkan renderer with a ray tracing preset on a GPU that has it, OpenGL on
  one without Vulkan.
- Half-Life starts a new game from its shortcut.
- With the game in a second Steam library, and with Steam closed or
  uninstalled after the install, everything above still works.
- Uninstalling leaves no file of the set, and the player's files only when
  asked to.

## Dependencies
CI turned on for the fork. 020 (ref_vk selectable), 027 (presets in the
menu), the 64-bit `cs16-client` and `hlsdk-portable` builds.

## Risks / notes
- Not signed: SmartScreen warns about an unknown publisher on first run
  ("More info", "Run anyway"). A code signing certificate is a separate
  decision.
- The game data is Valve's: the installer only copies the player's own
  files on their machine and never ships or uploads them.
- Servers that check Steam tickets refuse this client (no Steam); it joins
  Xash3D servers and GoldSrc servers that accept non-Steam clients.
