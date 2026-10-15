# 034 - Dedicated server baseline: Metamod, bots and CSDM

## Context
The multithreading work of the ReHLDS fork ([ReHLDS MT-001 to MT-702](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/README.md))
was measured and validated on a public Counter-Strike server with 19 bots, a
deathmatch mode with grenades and a plugin stack. Stories 035 to 050 bring the
same improvements to the Xash3D FWGS dedicated server; they need the same kind
of server to be measured on.

The amd64 dedicated server already builds (`scripts/dev/build-amd64.sh`,
`waf configure -d`) and runs ReGameDLL_CS from cs16-client (`cs_amd64.so`,
YaPB built alongside). It only accepts Xash3D clients (protocol 49,
`engine/server/sv_client.c`: `SV_ConnectClient`).

## Goal
A reproducible Counter-Strike dedicated server on this branch's engine, with
Metamod, bots and a deathmatch mode, started with one command, that every
following story is benchmarked and field tested on.

## Scope
In:
- Metamod: [Metamod-FWGS](https://github.com/FWGS/metamod-fwgs), the Xash3D
  fork of Metamod-R, which has linux x64 builds.
- Bots: ReGameDLL_CS's own bots (`bot_quota 19`, `bot_quota_mode normal`,
  `bot_join_after_player 0`), as on the ReHLDS field server; `.nav` files
  generated beforehand for every map of the cycle.
- Deathmatch: ReGameDLL_CS cvars (`mp_forcerespawn`, `mp_respawn_immunitytime`,
  `mp_round_infinite`, `mp_freeforall`, `mp_infinite_grenades`, `mp_buy_anywhere`,
  `mp_buytime`, `mp_startmoney`, `mp_item_staytime`).

Out: AMX Mod X, ReAPI and CSDM_ReAPI. AMX Mod X has no amd64 build and ReAPI
needs the ReHLDS engine API; the deathmatch mode comes from ReGameDLL_CS itself.
An i386 server could load them (story 001's container build) if the plugin
stack turns out to matter for the measurements.

## Tasks
- [x] `scripts/server/setup.sh`: server tree (engine, `cs_amd64.so`,
      Metamod-FWGS, configs) next to the read-only Steam data (`-rodir`).
- [x] `scripts/server/run.sh`: build, install, start on its own port (27016),
      with every option of stories 035-049 on (counterpart of the ReHLDS
      `run.sh`).
- [x] `server.cfg` and `csdm.cfg`: 19 bots, free for all deathmatch with
      grenades, map cycle with `.nav` files.
- [x] Check that Metamod-FWGS loads `cs_amd64.so` (`meta list`; no plugin
      tried yet).

## Progress (2026-10-15)
- Bots: on a dedicated server ReGameDLL_CS registers its bot cvars only with
  `bot_enable 1`, read from `game_init.cfg` before the game initialises; the
  bot profiles (`BotProfile.db`, `BotChatter.db`) are not in the Steam data
  and come from cs16-client's `extras.pk3`.
- Engine fix: `pfnGetFileSize` (`COM_FileSize`) did not convert DOS paths
  as `pfnLoadFileForMe` does; ReGameDLL_CS asks for `maps\<map>.bsp`, got -1
  and took every `.nav` file for an outdated one ("The AI navigation data is
  from a different version of this map").
- 19 bots play the free for all deathmatch on `de_dust2` (`.nav` files from
  the ReHLDS benchmark, same BSP). A Xash3D client joining is still to check.

## Acceptance criteria
- A clean checkout gives a running server with one command.
- 19 bots play a free for all deathmatch with grenades; a Xash3D client joins.
- `meta list` shows the plugins.

## Dependencies
001 (amd64 build).

## Risks / notes
- No player with a Steam client can join a Xash3D server: field tests need
  Xash3D clients.
