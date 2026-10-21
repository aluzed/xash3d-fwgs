# Windows installer

`Setup.exe` for Windows 10 and 11, 64-bit, made with Inno Setup (story
[052](../../../docs/stories/052-windows-installer.md)). It installs the
64-bit engine and dedicated server with Half-Life (`halflife-15`) and
Counter-Strike (`cstrike-17`: client, menus, ReGameDLL_CS, YaPB) and copies the game data from the
player's Steam folder; the shortcuts then start the games with nothing else
to install.

| File | |
|------|-|
| `stage.ps1` | gathers the files into `stage\` |
| `xash3d.iss` | the installer: wizard, Steam folder detection, copy, shortcuts, uninstaller |
| `server-cstrike.cfg`, `server-valve.cfg` | the dedicated servers' settings |
| `license.txt` | the licenses of what it installs, shown by the wizard |
| `make_icon.py` | draws `installer.ico` and `wizard-small-*.bmp` (Pillow) |

## Building

Everything comes from this repository (`Documentation/monorepo.md`):
`stage.ps1` builds the 64-bit engine, the dedicated server, `halflife-15`
and `cstrike-17`, then gathers them with SDL2 and the MSVC runtime; Inno
Setup 6 (`winget install JRSoftware.InnoSetup`) packs them.

```
powershell -File stage.ps1            # -NoBuild: only gather the last builds
iscc /DVersion=0.21 xash3d.iss
```

It needs Visual Studio 2026 Build Tools (C++, CMake), Python, Rust, the
Vulkan SDK and `SDL2_VC` at the top of the tree, as the builds do. The
installer is `output\Xash3D-FWGS-Setup-<version>.exe`.

## What it does

- Finds Half-Life in Steam (`SteamPath` in the registry, then every library
  of `steamapps\libraryfolders.vdf`); the player can pick another folder.
  Counter-Strike can be installed when the folder has `cstrike`.
- Installs per user, without administrator rights, in
  `%LOCALAPPDATA%\Programs\Xash3D FWGS`: the engine writes its settings,
  saves and screenshots next to itself.
- Copies `valve` and `cstrike` without Valve's libraries (`*.dll`, `*.so`,
  `*.dylib`, `*.exe`): the 64-bit ones replace them. The set keeps working
  when Steam updates, moves or removes the game.
- Writes Counter-Strike 1.7's `de_dust2` (the small crates of story 065)
  from the copied map with `patch_dust2.exe` (`cstrike-17/tools`), run
  once and removed; a map that isn't Counter-Strike's own stays as it is.
- Writes a first-run `video.cfg` in each game folder: the Vulkan renderer
  (the engine falls back to OpenGL by itself), fullscreen at the desktop's
  resolution. Never over an existing one.
- Ships the MSVC runtime next to `xash3d.exe` (app-local); Windows 10 has
  the UCRT.
- Shortcuts: "Half-Life" (`-game valve`), "Counter-Strike"
  (`-game cstrike -dll @yapb`, which loads the bots before the game
  library).
- Dedicated servers (component "server"): `xash-dedicated.exe` next to the
  client, shortcuts "Counter-Strike server" (port 27015, YaPB bots, the
  options of the dedicated server stories, `server-cstrike.cfg` as
  `cstrike\server.cfg`) and "Half-Life server" (port 27016,
  `server-valve.cfg`). Windows asks to let them through its firewall on
  their first start.
- The uninstaller asks before removing what the player made.

Silent install, as used in tests:
`Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /DIR="C:\Games\Xash3D" /COMPONENTS="hl,cs"`.
