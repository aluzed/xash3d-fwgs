# 067 - The deathmatch's spawns and rewards in the game library

## Context
The deathmatch of [034](034-dedicated-server-baseline.md) gets its spawn
points and its rewards on a kill from a Metamod plugin
(`scripts/server/csdm`), built for the Linux server only. The installer's
Counter-Strike 1.7 server ([052](052-windows-installer.md)) has neither
Metamod nor the plugin: its `csdm` mode ([056](056-server-game-modes.md),
[060](060-csdm-modes.md)) respawned players at their team's spawn and gave
nothing on a kill. Story 060 left moving the plugin into ReGameDLL_CS to a
story of its own.

## Goal
The `csdm` mode plays the same on every server: random spawn points away
from the others, the weapon reloaded and health given back on a kill.

## Scope
In: ReGameDLL_CS (spawns, rewards, cvars), the plugin stepping aside.
Out: CSDM-ReAPI's weapon menus, spawn files for the maps (the Linux server
downloads CSDM-ReAPI's; the installer has none).

## Design
- Only in the mode `csdm` (`mp_mode`); the cvars are the plugin's with an
  `mp_` prefix: `mp_csdm_spawns 1`, `mp_csdm_refill_clip 1`,
  `mp_csdm_heal_kill 15`, `mp_csdm_heal_headshot 30`,
  `mp_csdm_armor_headshot 30`.
- Spawns, in `GetPlayerSpawnSpot`: the map's CSDM spawn file
  (`addons/csdm/spawns/<map>.spawns.cfg`, read as the plugin read it), else
  in free for all every spawn point of the map, both teams'; a random
  point other than the player's last, with no living player within 450
  units (in teams, no enemy). A player not yet in a team, and team
  deathmatch without a spawn file, keep the map's points.
- On a kill, in `PlayerKilled`: the killer's active weapon reloaded, +15
  health, +30 health and +30 armor on a headshot, up to 100, the medkit's
  sound on a headshot (precached by the game library).
- The plugin finds `mp_csdm_heal_kill` and does nothing: no double
  rewards on the Linux server.

## Tasks
- [x] Spawns and rewards in ReGameDLL_CS (`dlls/game_modes.cpp`).
- [x] The plugin steps aside.
- [x] Test on the Windows dedicated server with bots.
- [ ] Check the Linux server: the plugin quiet, the spawn files read.

## Acceptance criteria
- On the installer's server in `csdm ffa`, players respawn anywhere on the
  map away from the others, and get health back on a kill.
- No reward is given twice on a server with the plugin.

## Status (2026-10-22)
Done but the Linux check. Tested on the Windows dedicated server of the
installed set, `sv_mode csdm ffa` with 10 YaPB bots on de_dust2 (no spawn
file): "no spawn file for de_dust2, its 40 spawn points", 29 respawns in a
minute on 15 different points of both teams' areas; 106 kills rewarded,
health given back (73 to 100, 40 to 70 on a headshot), armor up to 100;
after `sv_mode csdm team` the bots respawned at their team's points (no
spawn file). Seen with temporary log lines, removed before the commit. The
plugin's source passes an MSVC syntax check (Metamod template headers); it
isn't built nor run here.

## Dependencies
056, 060.

## Risks / notes
- Without a spawn file a map's spawn points are close together at each
  team's end: in free for all players meet at the bases. Spawn files for
  the set's maps would help (the installer could ship CSDM-ReAPI's).
