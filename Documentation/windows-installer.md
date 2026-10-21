# Installing on Windows

`Xash3D-FWGS-Setup-<version>.exe` installs Half-Life and Counter-Strike on
Windows 10 and 11 (64-bit): the 64-bit engine, both renderers, the games'
libraries, the bots and the dedicated servers. Nothing else to install or
type.

## What you need

- Half-Life bought on Steam and installed; Counter-Strike too to play it
  (it lives in the same folder). The installer copies the game data from
  there: it ships no file of Valve's.
- About 1 GB of free space with Counter-Strike.
- A graphics card with a Vulkan driver; without one the game uses OpenGL.

## Installing

1. Run the installer. Windows may warn about an unknown publisher (the
   installer isn't signed): "More info", then "Run anyway".
2. **Half-Life folder**: found on its own in your Steam libraries, usually
   `C:\Program Files (x86)\Steam\steamapps\common\Half-Life`. Pick another
   one with Browse (a copy of the game outside Steam works too).
3. **Destination**: `%LOCALAPPDATA%\Programs\Xash3D FWGS` by default. No
   administrator rights: the game writes its settings, saves, screenshots
   and demos next to itself.
4. **Components**: Half-Life (always), Counter-Strike with bots (when your
   Steam folder has it), the dedicated servers.
5. The copy takes a minute; Cancel leaves nothing behind.

## What it sets up

- The game data of `valve` and `cstrike`, copied: the set keeps working
  when Steam updates, moves or uninstalls the games.
- Counter-Strike's `de_dust2` with small crates to climb alone, written
  from your own map.
- Your Steam settings (keys, mouse) as the starting point, with the
  HUD in UTF-8 (names in any language) and **F12** for a screenshot as well
  as F5. Reinstalling keeps the settings you have here.
- The Vulkan renderer, fullscreen at your desktop's resolution, in the
  `classic` quality (as the original game). The others, with HDR or ray
  tracing, are in Configuration, Video, Ray tracing.
- Shortcuts in the Start menu (and on the desktop when asked):
  - **Half-Life**, **Counter-Strike** (with bots: `-dll @yapb`);
  - **Counter-Strike server** (port 27015, de_dust2, bots) and
    **Half-Life server** (port 27016) with the dedicated servers. Windows
    asks to let them through its firewall the first time.

## Where things go

| | |
|---|---|
| The game | `%LOCALAPPDATA%\Programs\Xash3D FWGS` |
| Settings | `valve\config.cfg`, `cstrike\config.cfg`, `video.cfg` beside them |
| Screenshots, demos, saves | in `valve` and `cstrike` |
| Log | `engine.log`, with `-log` on the command line |

## Multiplayer

The game has no Steam: it joins Xash3D servers and the GoldSrc servers
that accept players without Steam. The server browser lists them.

## Uninstalling

"Uninstall Xash3D FWGS" in the Start menu, or Settings, Apps. It removes
the game and asks before removing your settings, saves, screenshots and
demos. Your Steam games are never touched.
