# 056 - Server game modes: match, FFA, CSDM (Counter-Strike)

## Context
The server plays one way at a time, set by hand: `server.cfg` for rounds,
`csdm.cfg` and the Metamod plugin of 034 for deathmatch. Going from one to
the other means editing files and restarting. Counter-Strike 2 has its
modes chosen in a menu; the set should switch between its own modes with
one command.

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
- Server command `mode <match|ffa|csdm> [ffa|team]`, from the server
  console or rcon: sets `mp_mode`, executes `modes/<mode>.cfg` (and
  `modes/csdm_ffa.cfg` or `modes/csdm_team.cfg`), restarts the map so that
  nothing of the previous mode stays (scores, money, weapons on the
  ground). `mode` alone prints the current mode and the list.
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
- [ ] `mp_mode`, `mp_csdm_teams` and the `mode` command in ReGameDLL_CS.
- [ ] `modes/match.cfg`, `ffa.cfg`, `csdm_ffa.cfg`, `csdm_team.cfg`, every
      mode setting every cvar of the others.
- [ ] Mode kept over a map change; announced after a switch.
- [ ] The csdm plugin enabled only in `csdm`.
- [ ] `scripts/server` and the installer: a default mode, the command in
      their README.
- [ ] Test: each switch from each mode, on a listen server and on the
      dedicated server, with bots.

## Acceptance criteria
- `mode csdm team` on a server in `match` gives team deathmatch with
  respawn after the restart, with nothing of the match left; `mode match`
  goes back with the match's rules and money.
- The mode stays after a `changelevel`.
- An unknown mode is refused with the list of modes.

## Dependencies
034 (the server and its csdm plugin), 057 to 060 for the modes' rules.

## Risks / notes
- Some cvars only take effect at a restart or a new map: the switch always
  restarts the map.
- Bots (YaPB) need nothing per mode, they follow `mp_freeforall`.
