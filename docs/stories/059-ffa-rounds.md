# 059 - FFA mode: rounds, everyone against everyone (Counter-Strike)

## Context
ReGameDLL_CS can play free for all (`mp_freeforall 1`): the teams don't
count, everyone can hurt everyone. The deathmatch of 034 uses it with
respawn; the `ffa` mode of [056](056-server-game-modes.md) keeps the
rounds of Counter-Strike instead.

## Status (2026-10-22)
Done in ReGameDLL_CS (`CHalfLifeMultiplay::FreeForAllRoundCheck`,
`FreeForAllRoundEnd` in `dlls/game_modes.cpp`), the settings in
`modes/ffa.cfg` (056). With `mp_freeforall 1` and rounds, the win checks of
the teams and objectives (bomb, hostages, VIP, escape) are skipped: the
game starts with two players whatever their teams, the last player alive
wins the round ("<name> wins the round!", the sound of their team's win, a
`Round_Win` line in the log), a round whose time runs out on more than one
player is a draw. Money of a round, given to each player at its end:
`mp_ffa_win_money` (3250, a team's win in CS) to the winner,
`mp_ffa_loss_money` (1400, CS2's first loss bonus) to everyone else and to
all on a draw; $300 a kill as usual. Dead players watch anyone
(`mp_forcecamera 0`, `mp_forcechasecam 0`).

Tested on 2026-10-22 on the dedicated server of the installed set with 10
YaPB bots on de_dust2: the bots fight everyone, teammates included; each
round ended on its last player alive (`Round_Win`), then with
`mp_roundtime 0.25` every round ran out of time as a draw. Not checked by a
player on the server: the money and the spectator camera; the economy
needs playing to tune (as the notes said).

## Goal
Rounds where everyone plays against everyone: buy at the start, no
respawn, the round ends when one player is left (or the round time runs
out), a dead player can watch anyone until the round ends.

## Scope
In: the `ffa` mode's settings, what ReGameDLL_CS lacks for rounds in free
for all (the end of a round, its winner, its rewards), the spectator
camera.

Out: objectives (no bomb, no hostages), deathmatch with respawn
([060](060-csdm-modes.md)).

## Design
- Settings: `mp_freeforall 1`, rounds (`mp_round_infinite 0`, no
  respawn), objectives off (`mp_give_player_c4 0`, hostages and bomb
  sites ignored), start money 800, the usual buy time and round time,
  teams only for the spawns (`mp_limitteams 0`, `mp_autoteambalance 0`).
- End of a round: the last player alive wins it; when the time runs out,
  the round is a draw. Rewards: money per kill as in a classic round, a
  bonus to the round's winner (to set: CS's 3250 for a win is for a team).
- Dead players: `mp_forcecamera 0`, they watch any player, free look
  included, until the round ends.
- Score: frags; the board sorted as in 055 (damage, then frags).

## Tasks
- [x] The `ffa` mode's config.
- [x] Round end and winner in free for all in ReGameDLL_CS (check what
      `mp_freeforall` already does with rounds).
- [x] Spectating anyone while dead.
- [ ] Test with bots on de_dust2 and a small map.

## Acceptance criteria
- Every player can hurt every other; a round ends with one player alive
  or with the time.
- A dead player can watch any player until the round ends.

## Dependencies
056 (the mode), 055 (the board).

## Risks / notes
- The money's balance in free for all (rewards for a win, for a kill) needs
  playing to tune.
