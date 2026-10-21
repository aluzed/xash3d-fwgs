# 060 - CSDM mode: free for all or team deathmatch (Counter-Strike)

## Context
The deathmatch of [034](034-dedicated-server-baseline.md) is free for all:
ReGameDLL_CS cvars in `csdm.cfg` and a Metamod plugin
(`scripts/server/csdm`: CSDM spawn points, health and reload on a kill).
The `csdm` mode of [056](056-server-game-modes.md) offers it in two forms.

## Status (2026-10-22)
Done with 056: `modes/csdm_ffa.cfg` is the old `csdm.cfg` (removed from
`scripts/server`), `modes/csdm_team.cfg` the same with `mp_freeforall 0`,
`mp_autoteambalance 1`, `mp_limitteams 2` (friendly fire off in both). The
csdm plugin acts only in `csdm` and, in team deathmatch, only an enemy
within 450 units keeps a spawn point.

Tested on 2026-10-22 on the Windows dedicated server of the installed set
with 10 YaPB bots: `sv_mode csdm team` gave 23 kills in a minute, none
between teammates, the bots respawning and fighting without objectives;
`csdm ffa` as the deathmatch of 034 (bots killing teammates). Not tested:
the plugin (Linux only, its Metamod build isn't on this machine; its source
passes an MSVC syntax check with the template's Metamod headers); to check
on the Linux server with `MODE="csdm team" scripts/server/run.sh`. Since
067 the game library does what the plugin did, and the plugin steps aside.

## Goal
Deathmatch with respawn, chosen by the mode:
- `sv_mode csdm ffa`: everyone against everyone, as today;
- `sv_mode csdm team`: terrorists against counter-terrorists, with respawn.

## Scope
In: the two configs, team deathmatch's settings, the plugin's rules in
both forms (spawns, rewards), the spectator camera.

Out: moving the plugin into ReGameDLL_CS (worth it, its own story),
weapon menus of CSDM-ReAPI.

## Design
- `modes/csdm_ffa.cfg`: today's `csdm.cfg`.
- `modes/csdm_team.cfg`: the same with `mp_freeforall 0`, team balance on
  (`mp_autoteambalance 1`, `mp_limitteams 2`), friendly fire off; the
  score of a team is its frags.
- Spawns: the CSDM spawn files are shared by both teams; in team
  deathmatch a spawn is refused when an enemy is within 450 units (the
  plugin's rule, teams counted).
- Rewards on a kill (health, armour, reload) the same in both forms.
- No round end (`mp_round_infinite 1`), time limit per map
  (`mp_timelimit`); dead players respawn after 0.7 s, so no spectating to
  set.

## Tasks
- [x] `csdm_ffa.cfg` from `csdm.cfg`, `csdm_team.cfg`.
- [x] The plugin: spawns away from enemies only in team deathmatch.
- [ ] Test both with bots.

## Acceptance criteria
- `sv_mode csdm team`: teams that can't hurt each other's members, respawn,
  spawns away from enemies; `sv_mode csdm ffa` as the deathmatch of 034.

## Dependencies
056 (the mode), 034 (the plugin).

## Risks / notes
- YaPB in team deathmatch: check that bots keep fighting without
  objectives.
