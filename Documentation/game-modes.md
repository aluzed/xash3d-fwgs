# Counter-Strike 1.7 game modes

A Counter-Strike 1.7 server (`cstrike-17`, ReGameDLL_CS) plays one of its
modes, switched with one command from the server's console or rcon
(story [056](../docs/stories/056-server-game-modes.md)):

| Command | Mode |
|---|---|
| `sv_mode match` | competitive match, MR13 with its overtime, players only ([057](../docs/stories/057-match-mr13.md)) |
| `sv_mode ffa` | rounds, everyone against everyone ([059](../docs/stories/059-ffa-rounds.md)) |
| `sv_mode csdm ffa` | deathmatch with respawn, everyone against everyone ([060](../docs/stories/060-csdm-modes.md)) |
| `sv_mode csdm team` | deathmatch with respawn, terrorists against counter-terrorists |

`sv_mode` alone prints the current mode and this list; an unknown mode is
refused.

## What a switch does

`sv_mode` sets `mp_mode` (and `mp_csdm_teams` for `csdm`), executes the
mode's config and restarts the map: scores, money and the weapons on the
ground of the previous mode are gone. The players are told the new mode in
the chat and at the centre of the screen.

On every map the game library executes the mode's config again, after the
server's own configs: the mode stays over a `changelevel` and a map cycle.
With `mp_mode` empty (the default) the server plays as its configs say, as
before the modes.

On the command line, `+sv_mode csdm ffa` before `+map` starts the server in
a mode.

## Free for all rounds

In `ffa` the last player alive wins the round, a round that runs out of
time is a draw. Each player gets the money of the round at its end:
`mp_ffa_win_money` (3250) for the winner, `mp_ffa_loss_money` (1400) for
the others and for everyone on a draw.

## The configs

`modes/match.cfg`, `modes/ffa.cfg`, `modes/csdm_ffa.cfg` and
`modes/csdm_team.cfg` are in Counter-Strike 1.7's `extras.pk3`. Every one
sets the same cvars (free for all, rounds or respawn, money, buy and round
times, equipment, camera of the dead, team balance, limits), so that the
order of the switches never matters. A file of the same name in
`cstrike/modes/` comes before the one of `extras.pk3`: copy one there to
change a mode.

`modes/bots.cfg` holds the bots of the modes that have them (`ffa` and
`csdm`): 10 YaPB bots filling the server by default. `match` sets the
quotas (`yb_quota`, `bot_quota`) to 0 and the bots leave. A server sets its
own count in `cstrike/modes/bots.cfg` (`scripts/server` does, with
Counter-Strike's bots).

## Servers

- The installer's "Counter-Strike 1.7 server" starts in `match`.
- `scripts/server/run.sh` starts in `MODE` (default `csdm ffa`). Its csdm
  Metamod plugin (spawn points, rewards on a kill) only works in `csdm`; in
  `csdm team` an enemy within 450 units keeps a spawn point, a teammate
  doesn't.
