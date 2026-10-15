# 033 - Valve's current Counter-Strike client library on Xash3D

## Context
Ground rule 2 of the roadmap: Valve's original game libraries must keep
working. Half-Life's `client.so` / `hl.so` from Steam run on the i386 build
(`scripts/dev/run-i386.sh`), but the current Steam `cstrike/cl_dlls/client.so`
crashed right away, with any renderer:

```
_ZN9KeyValuesnwEj (KeyValues.cpp:1342)
BuyPresetManager::Reset (buy_presets.cpp:149)
CHud::Init (hud.cpp:604)
HUD_Init (cdll_int.cpp:404)
CL_LoadProgs (cl_game.c)
```

## Goal
Know what the current Steam CS client needs from the engine, and make it
start, or document why it can't.

## Findings
- The 2013+ client draws its menus, scoreboard and spectator bars with
  VGUI2 and exports `VClientVGUI001`. GoldSrc's BaseUI starts Valve's
  `vgui2.so` with an engine-side surface (`VGUI_Surface026`, in `hw.so`)
  and gives the client its factories before `HUD_Init`; `KeyValues`
  allocate through vgui2's `KeyValues003`, which nothing had set up.
- Upstream Xash3D FWGS doesn't support VGUI2 (not-supported mod list,
  issues #1408, #1410); the old `vinterface` branch was abandoned in 2016.
  CSMoE's VGUI2 is rebuilt from Valve's Source SDK code, not reusable here.
- `hw.so`, `vgui2.so` and the client kept their symbols: every interface's
  method order comes from their vtables (`Documentation/vgui2.md`).
- `CounterStrikeViewport::Start` calls `SteamAPI_Init` and `exit(-1)`s when
  it fails: the Steam client has to run, as on GoldSrc. Not worked around.
- The client loads `cl_dlls/gameui.so` again to reach the instance
  GoldSrc's engine runs; a fresh, uninitialized copy crashes
  (`CCareerGame::IsPlayingMatch`).
- The server (ReGameDLL or Valve's `cs.so`, with cs16-client as well) sends
  the team menu only after the joining player presses jump or attack; no
  MOTD message reaches the client in the development setup. Not specific
  to this story.

## Tasks
- [x] Find how the client gets `KeyValuesSystem` and what Xash passes.
- [x] Check upstream Xash3D FWGS issues for the 2023 client libraries.
- [x] `vgui2_support`: loads `vgui2.so` from the install, implements the
      surface (drawing with clipping, textures, stb_truetype fonts with
      GoldSrc's Linux font mapping and effects, popups, traversals), the
      root panels, `IGameUIFuncs`, input routing as GoldSrc does (the
      client's `HUD_Key_Event` first, VGUI2 keeps the keys of a text entry
      only, the mouse while a panel takes it).
- [x] Engine: start it between the client's `Initialize` and `HUD_Init`,
      paint after the HUD, `platform/` images, no path for Valve's GameUI.
- [x] `CLIENT=steam run-i386.sh cstrike`, `Documentation/vgui2.md`, the
      not-supported mod list.
- [ ] User checks: playing with the mouse and keyboard (team, class and
      buy menus, scoreboard, spectator), a round online.

## Results
On the i386 build, with Steam running, the current Steam client starts,
loads de_dust2, and draws its VGUI2 team menu, buy menu and scoreboard
with the original layouts and fonts. Half-Life's Steam client (no VGUI2)
is unchanged. HTML panels (MOTD) stay empty: no browser.

## Acceptance criteria
- The current Steam CS client reaches the main menu and joins a listen
  server on the i386 build, or the limitation is documented with its cause.

## Dependencies
001 (i386 build).
