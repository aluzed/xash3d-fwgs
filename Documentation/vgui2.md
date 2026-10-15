# VGUI2 for GoldSrc's 2013 client libraries

Since 2013 the Steam versions of Counter-Strike (and Condition Zero, Day of
Defeat...) draw part of their interface with VGUI2: the team, class and buy
menus, the scoreboard, the spectator bars, the MOTD. Their `client.so`
exports the `VClientVGUI001` interface and expects the engine to set up
Valve's vgui2 library, with a surface the engine implements, before
`HUD_Init`. Without it, `HUD_Init` crashes in `KeyValues::operator new`.

`vgui2_support` is that part of the engine. It is loaded only for a client
that exports `VClientVGUI001`; the other clients, Half-Life's included,
don't see it.

## Running Steam's Counter-Strike client

Valve's client exists for 32-bit x86 only, so this needs the i386 build:

```
scripts/dev/build-i386.sh
CLIENT=steam scripts/dev/run-i386.sh cstrike
```

- The Steam client must be running. `CounterStrikeViewport::Start` calls
  `SteamAPI_Init` and quits the game when it fails, as it does on GoldSrc.
  `run-i386.sh` sets `SteamAppId` to the game's application (10 for
  Counter-Strike), as Steam's launcher does.
- `vgui2.so` is Valve's, from the game's install: it is found through the
  library path, or next to `-rodir`. Nothing of Valve's is copied or
  redistributed.

## What the engine provides

| Interface | Where | Notes |
|---|---|---|
| `VGUI_Surface026` | `vgui2_support/surface.cpp` | drawing, textures, fonts, popups |
| `VENGINE_GAMEUIFUNCS_VERSION001` | `vgui2_support/surface.cpp` | key names and bindings, video mode |
| `VFileSystem009` | `filesystem/VFileSystem009.cpp` | Xash's filesystem, already there for GameUI |
| root panels | `vgui2_support/surface.cpp` | the embedded panel and the client's parent panel |

Everything else (`VGUI_ivgui006`, `VGUI_Panel007`, `VGUI_Scheme009`,
`VGUI_Localize003`, `VGUI_System009`, `VGUI_InputInternal001`,
`KeyValues003`) is Valve's `vgui2.so`.

The start follows GoldSrc's BaseUI: `IVGui::Init` with the factories (the
engine's, vgui2's, the filesystem's, the client's), the size of a KeyValues
registered (`RegisterSizeofKeyValues(0x18)`, which every module's
`VGuiControls_Init` does), `resource/TrackerScheme.res` as the default
scheme, the localized strings of `valve` and of the game, then the client's
`Initialize`, `Start` and `SetParent`.

### Layouts

The order of the virtual functions was read from the symbols of the Steam
libraries (2023 update): `hw.so` for `BaseUISurface` (119 entries, the
2013+ ones included), `CGameUIFuncs`, `CBaseUI`; `vgui2.so` for `CVGui`,
`VPanelWrapper`, `CInputWin32`, `CSchemeManager`, `CSystem`,
`CVGuiKeyValues`; the client for `IClientVGUI` (a secondary vtable of
`CounterStrikeViewport`) and `IClientPanel` (the start of
`vgui2::Panel`'s). The declarations are in `vgui2_support/interfaces.h`.
A few slots of the surface are empty functions folded together in
`hw.so`; they are kept as placeholders returning 0.

### Fonts

GoldSrc on Linux maps the Windows font names of the schemes to the fonts
Steam ships in `platform/resource/linux_fonts`; the same mapping is used:

| Scheme name | File |
|---|---|
| Verdana (Bold, Italic) | DejaVuSans (-Bold, -Oblique, -BoldOblique) |
| Tahoma, Arial | LiberationSans-Regular, -Bold |
| Trebuchet MS | FiraSans-Regular, -Medium |
| Courier, Courier New | LiberationMono-Regular |
| Marlett | `resource/marlett.ttf` of the game |

They are rasterized with stb_truetype, with the outline, drop shadow and
blur effects, into 512x512 pages. Without a Steam install the engine's own
`gfx/fonts` are used.

### Input

As on GoldSrc, keys go to the client's `HUD_Key_Event` first (its menus
read their hot keys there) and to VGUI2 as well; only a VGUI2 text entry
with the focus keeps them from the game. While a panel that takes the
mouse is up, the cursor is shown and mouse buttons and the wheel go to
VGUI2 only. The images of VGUI2's own controls (`resource/icon_*.tga`) are
read from the install's `platform` directory, like GoldSrc does.

## Limitations

- **HTML**: GoldSrc renders the MOTD and the info pages with Steam's
  Chromium. There is no browser here: the surface gives out a controller
  and browser objects that do nothing, so HTML panels stay empty instead
  of crashing.
- **GameUI**: the client loads Valve's `cl_dlls/gameui.so` again to reach
  the instance GoldSrc's engine runs. Xash runs its own menu, and a second,
  never initialized copy crashes, so `COM_ExpandFilename` doesn't find that
  library: the client goes on without GameUI, career mode and the music
  manager.
- Valve's client runs on i386 only; cs16-client remains the Counter-Strike
  client on the other platforms and the default one of the development
  tree.
