# 057 - Match mode: MR13 with its overtime (Counter-Strike)

## Context
A Counter-Strike 2 competitive match is played in MR12 (first to 13), the
teams switch sides at half time, a 12-12 draw goes to overtime. On a
Counter-Strike 1.6 server this needs an admin or a plugin to swap the
teams, count the rounds and stop the match. The `match` mode of
[056](056-server-game-modes.md) should run a whole match by itself.

## Goal
A match that runs alone from its first round to its end:
- warmup as long as the server has fewer than 10 players; with 10, one
  more minute of warmup, then the match starts;
- the first team to 13 rounds wins;
- after 12 rounds, the teams switch sides automatically;
- at 12-12, overtime: the first team to 16 wins, the sides switch after
  three rounds of overtime as in CS2;
- at 15-15, the match ends in a draw, nothing more is played.

## Scope
In: ReGameDLL_CS (the match's state, round counting, switches, end), the
`match` mode's settings, messages to the players.

Out: the interludes at the start, the switches and the end
([058](058-match-interludes.md)), a ready check (the player count starts
the match), several overtimes.

## Design
- Warmup: while fewer than `mp_match_players` (10) players are on the
  server (bots count, spectators don't), the match waits in warmup:
  respawn, buy anywhere with $16 000, no score kept, a message "Warmup:
  waiting for players (7/10)". When the count is reached, warmup goes on
  for `mp_match_warmup_time` (60 s) with its countdown on screen; if a
  player leaves and the count falls below 10 during it, back to waiting.
  Then the map restarts into the first round, with the match's rules.
- Match state in the game rules: warmup, first half, second half,
  overtime first half, overtime second half, ended; the rounds won by each
  side kept per team, not per side, so the score follows the players when
  they switch.
- Half time after round 12: the players of both teams swap sides (as
  `swapteams` does, models included), their money goes back to the start
  money (800), weapons and equipment removed, the round counter goes on.
  The team scores on the board follow the teams.
- End of regulation: a team reaching 13 wins, the match ends.
- Overtime at 12-12: rounds 25 to 30, money of each player set to the
  overtime money at its start and at its switch (CS2 gives 12 500; open
  question), sides switched after round 27, a team reaching 16 wins, 15-15
  ends in a draw.
- Match end: the winner (or the draw) announced, the scores frozen, no
  more rounds; the server then waits for a `mode` command or goes to the
  next map after `mp_match_end_delay` seconds (0: stays).
- Settings of the mode, the standard rules of a match: start money 800,
  `mp_forcecamera 1` (a dead player only watches their teammates),
  `mp_freeforall 0`, freeze time 20 s (CS2's competitive value as far as
  known, to check), the usual buy time and round time.
- Cvars: `mp_match_players 10`, `mp_match_warmup_time 60`,
  `mp_match_rounds 24` (MR12: first to 13), `mp_match_overtime 1`,
  `mp_match_ot_rounds 6`, `mp_match_ot_money 12500`,
  `mp_match_end_delay 0`.
- Chat and centre messages: "Half time, switching sides", "Overtime",
  "<team> wins the match 13-9", "Draw 15-15".
- Bots: switched with the players.

## Tasks
- [ ] Warmup until 10 players, then a minute, then the first round.
- [ ] Match state and team scores kept across switches.
- [ ] Half time switch with money reset; overtime with its money and
      switch; end at 13, 16 or 15-15.
- [ ] Cvars and the `match` mode's config; messages.
- [ ] Test with bots, with short cvars (`mp_match_rounds 4`) to reach half
      time, overtime and both ends quickly; then a full match on the test
      server.

## Acceptance criteria
- With 9 players the server stays in warmup; the tenth starts a minute of
  warmup, after which the match begins.
- Without an admin: the teams switch after round 12, the first to 13 wins,
  12-12 plays overtime, the first to 16 wins, 15-15 stops as a draw.
- After a switch each team's score is still its own, every player starts
  with $800 (overtime: its money).
- Dead players only watch their teammates.

## Dependencies
056 (the mode), 058 (the interludes, built on this state).

## Risks / notes
- Open questions: the money in overtime; what happens when a team is
  empty or players leave during the match (pause, forfeit); the freeze time
  of CS2 to confirm.
- ReGameDLL_CS's `mp_maxrounds` / `mp_winlimit` end a map, not a match
  with sides: the state above replaces them in `match`.
