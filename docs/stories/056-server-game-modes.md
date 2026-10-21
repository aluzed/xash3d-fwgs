# 056 - Server game modes: match, FFA, CSDM (Counter-Strike)

## Context
The server plays one way at a time, set by hand: `server.cfg` for rounds,
`csdm.cfg` and the Metamod plugin of 034 for deathmatch. Going from one to
the other means editing files and restarting. Counter-Strike 2 has its
modes chosen in a menu; the set should switch between its own modes with
one command.

## Status (2026-10-22)
Done (`dlls/game_modes.cpp` in ReGameDLL_CS, the configs in
`cs16client-extras/modes`, operators' page `Documentation/game-modes.md`).
The modes' own rules come with 057 to 060; until then each mode is its
settings. Differences with the design:

- `mp_mode` isn't saved by the server: the command line sets the starting
  mode (`+sv_mode csdm ffa`), the cvars keep it over map changes.
- Each mode has one config (`csdm` two: `csdm_ffa.cfg`, `csdm_team.cfg`);
  a copy in `cstrike/modes/` overrides the one of `extras.pk3`.
- Bots: `ffa` and `csdm` execute `modes/bots.cfg` (10 YaPB bots in
  `extras.pk3`, `scripts/server` has its own with 19 of Counter-Strike's),
  `match` sets the quotas to 0. Xash3D executes `server.cfg` once at
  startup, so the quota couldn't stay in it.
- The announcement waits for a player in the game (6 s at least after the
  map starts); the centre text is under the team menu right after a
  restart, the chat line stays.

Tested on 2026-10-22 with the installed set (Windows, YaPB): a dedicated
server started in `csdm ffa` (10 bots), then `sv_mode match` (rounds, $800,
dead watch their team, the bots gone), `sv_mode csdm team` (no free for
all, balance on, the bots back), `sv_mode ffa`, `zombie` and `ffa team`
refused with the list, `changelevel de_inferno` kept `ffa`; a client on
the server got "Mode: team deathmatch" after the switch; a listen server
with `+sv_mode csdm team` ran the mode with its bots. Not tested: the csdm
plugin's changes (Linux only: Metamod and its build aren't on this
machine), to check on the Linux server.

## Goal
A command that switches the server between three native modes, applied at
once (the map restarts), each mode a complete set of settings:
- `match`: a competitive match, MR13 ([057](057-match-mr13.md)), with its
  interludes ([058](058-match-interludes.md));
- `ffa`: rounds, everyone against everyone ([059](059-ffa-rounds.md));
- `csdm`: deathmatch with respawn, in free for all or in teams
  ([060](060-csdm-modes.md)).

## Scope
In: ReGameDLL_CS (the command, the mode cvar, applying a mode), the modes'
config files, the server scripts and the installer's server shortcuts.

Out: votes of the players (a later story), maps per mode, the rules of
each mode (their own stories).

## Design
- `mp_mode` (server cvar, saved in the server's config) names the mode:
  `match`, `ffa`, `csdm`; `mp_csdm_teams 0|1` picks free for all or team
  deathmatch in `csdm`.
- Server command `sv_mode <match|ffa|csdm> [ffa|team]` (server commands are `sv_`), from the server
  console or rcon: sets `mp_mode`, executes `modes/<mode>.cfg` (and
  `modes/csdm_ffa.cfg` or `modes/csdm_team.cfg`), restarts the map so that
  nothing of the previous mode stays (scores, money, weapons on the
  ground). `sv_mode` alone prints the current mode and the list.
- `modes/<mode>.cfg`: only the cvars of the mode, all of them set, so that
  the order of switches never matters (e.g. `mp_freeforall`,
  `mp_round_infinite`, `mp_forcerespawn`, `mp_startmoney`,
  `mp_forcecamera`, `mp_maxrounds`...). `server.cfg` keeps what is common
  (name, rates, bots).
- On a map change the mode stays: the game library executes the mode's
  config after `server.cfg`.
- The deathmatch plugin of 034 (spawns, rewards on a kill) is enabled only
  in `csdm` (it reads `mp_mode`), until 060 brings it into ReGameDLL_CS.
- Announced to the players: "Mode: match (MR13)" in the chat and at the
  centre of the screen after the restart.
- Installer (052): the "Counter-Strike server" shortcut starts in `match`;
  the README lists the command.

## Tasks
- [x] `mp_mode`, `mp_csdm_teams` and the `sv_mode` command in ReGameDLL_CS.
- [x] `modes/match.cfg`, `ffa.cfg`, `csdm_ffa.cfg`, `csdm_team.cfg`, every
      mode setting every cvar of the others.
- [x] Mode kept over a map change; announced after a switch.
- [x] The csdm plugin enabled only in `csdm`.
- [x] `scripts/server` and the installer: a default mode, the command in
      their README.
- [ ] Test: each switch from each mode, on a listen server and on the
      dedicated server, with bots.

## Acceptance criteria
- `sv_mode csdm team` on a server in `match` gives team deathmatch with
  respawn after the restart, with nothing of the match left; `sv_mode match`
  goes back with the match's rules and money.
- The mode stays after a `changelevel`.
- An unknown mode is refused with the list of modes.

## Dependencies
034 (the server and its csdm plugin), 057 to 060 for the modes' rules.

## Risks / notes
- Some cvars only take effect at a restart or a new map: the switch always
  restarts the map.
- Bots (YaPB) only in `ffa` and `csdm` (their configs set `yb_quota`);
  `match` is for players only, its config sets `yb_quota 0` and kicks the
  bots.
