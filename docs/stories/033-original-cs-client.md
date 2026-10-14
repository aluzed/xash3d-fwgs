# 033 - Valve's current Counter-Strike client library on Xash3D

## Context
Ground rule 2 of the roadmap: Valve's original game libraries must keep
working. Half-Life's `client.so` / `hl.so` from Steam run on the i386 build
(`scripts/dev/run-i386.sh`), but the current Steam `cstrike/cl_dlls/client.so`
crashes right away, with any renderer:

```
_ZN9KeyValuesnwEj (KeyValues.cpp:1342)
BuyPresetManager::Reset (buy_presets.cpp:149)
CHud::Init (hud.cpp:604)
HUD_Init (cdll_int.cpp:404)
CL_LoadProgs (cl_game.c)
```

`KeyValues::operator new` asks the `KeyValuesSystem` of Valve's `vstdlib`
for memory; on Xash it isn't there or not set up the way the 2023+ client
expects. CS on i386 runs cs16-client meanwhile.

## Goal
Know what the current Steam CS client needs from the engine, and make it
start, or document why it can't.

## Tasks
- [ ] Find how the client gets `KeyValuesSystem` (interface factory,
      `CreateInterface` of vstdlib / tier0 shipped with Steam's HL) and what
      Xash passes or doesn't.
- [ ] Check upstream Xash3D FWGS issues for the 2023 "25th anniversary"
      client libraries.
- [ ] Fix in the engine if it's an interface Xash can provide, without
      breaking older clients; otherwise document the limitation in
      `Documentation/` and keep cs16-client as the CS client.

## Acceptance criteria
- The current Steam CS client reaches the main menu and joins a listen
  server on the i386 build, or the limitation is documented with its cause.

## Dependencies
001 (i386 build).
