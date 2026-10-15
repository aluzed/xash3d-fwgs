# Windows installer

`Setup.exe` for Windows 10 and 11, 64-bit, made with Inno Setup (story
[052](../../../docs/stories/052-windows-installer.md)). It installs the
64-bit engine with Half-Life (`hlsdk-portable`) and Counter-Strike
(`cs16-client`, ReGameDLL_CS, YaPB) and copies the game data from the
player's Steam folder; the shortcuts then start the games with nothing else
to install.

| File | |
|------|-|
| `stage.ps1` | gathers the files into `stage\` |
| `xash3d.iss` | the installer: wizard, Steam folder detection, copy, shortcuts, uninstaller |
| `license.txt` | the licenses of what it installs, shown by the wizard |
| `make_icon.py` | draws `installer.ico` and `wizard-small-*.bmp` (Pillow) |

## Building

1. Build and install the 64-bit engine (`docs/building-vulkan-renderer.md`,
   Windows), `hlsdk-portable` and `cs16-client` for x64:

   ```
   cmake -S hlsdk-portable -B build -G "Visual Studio 18 2026" -A x64 -D64BIT=ON
   cmake --build build --config Release
   cmake --install build --config Release --prefix C:\xash-dev\stage-hl

   cmake -S cs16-client -B build -G "Visual Studio 18 2026" -A x64 -DENABLE_YY_THUNKS=OFF
   cmake --build build --config Release
   cmake --install build --config Release --prefix C:\xash-dev\stage-cs
   ```

2. Stage, then compile with Inno Setup 6
   (`winget install JRSoftware.InnoSetup`):

   ```
   powershell -File stage.ps1 -Engine C:\xash-dev\win64 -Hlsdk C:\xash-dev\stage-hl -Cs16 C:\xash-dev\stage-cs
   iscc /DVersion=0.21 xash3d.iss
   ```

   The installer is `output\Xash3D-FWGS-Setup-<version>.exe`.

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
- Writes a first-run `video.cfg` in each game folder: the Vulkan renderer
  (the engine falls back to OpenGL by itself), fullscreen at the desktop's
  resolution. Never over an existing one.
- Ships the MSVC runtime next to `xash3d.exe` (app-local); Windows 10 has
  the UCRT.
- Shortcuts: "Half-Life" (`-game valve`), "Counter-Strike"
  (`-game cstrike -dll @yapb`, which loads the bots before the game
  library).
- The uninstaller asks before removing what the player made.

Silent install, as used in tests:
`Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /DIR="C:\Games\Xash3D" /COMPONENTS="hl,cs"`.
