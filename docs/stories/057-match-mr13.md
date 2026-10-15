# 057 - Match mode: MR13 with its overtime (Counter-Strike)

## Context
A Counter-Strike 2 competitive match is played in MR12 (first to 13), the
teams switch sides at half time, a 12-12 draw goes to overtime. On a
Counter-Strike 1.6 server this needs an admin or a plugin to swap the
teams, count the rounds and stop the match. The `match` mode of
[056](056-server-game-modes.md) should run a whole match by itself.

## Goal
A match that runs alone from its first round to its end:
- the first team to 13 rounds wins;
- after 12 rounds, the teams switch sides automatically;
- at 12-12, overtime: the first team to 16 wins, the sides switch after
  three rounds of overtime as in CS2;
- at 15-15, the match ends in a draw, nothing more is played.

## Scope
In: ReGameDLL_CS (the match's state, round counting, switches, end), the
`match` mode's settings, messages to the players.

Out: the interludes at the start, the switches and the end
([058](058-match-interludes.md)), ready checks and warmup (an open
question below), several overtimes.

## Design
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
- Settings of the mode: start money 800 as in a classic round,
  `mp_forcecamera 1` (a dead player only watches their teammates),
  `mp_freeforall 0`, the usual buy time, freeze time and round time.
- Cvars: `mp_match_rounds 24` (MR12: first to 13), `mp_match_overtime 1`,
  `mp_match_ot_rounds 6`, `mp_match_ot_money 12500`,
  `mp_match_end_delay 0`.
- Chat and centre messages: "Half time, switching sides", "Overtime",
  "<team> wins the match 13-9", "Draw 15-15".
- Bots: switched with the players.

## Tasks
- [ ] Match state and team scores kept across switches.
- [ ] Half time switch with money reset; overtime with its money and
      switch; end at 13, 16 or 15-15.
- [ ] Cvars and the `match` mode's config; messages.
- [ ] Test with bots, with short cvars (`mp_match_rounds 4`) to reach half
      time, overtime and both ends quickly; then a full match on the test
      server.

## Acceptance criteria
- Without an admin: the teams switch after round 12, the first to 13 wins,
  12-12 plays overtime, the first to 16 wins, 15-15 stops as a draw.
- After a switch each team's score is still its own, every player starts
  with $800 (overtime: its money).
- Dead players only watch their teammates.

## Dependencies
056 (the mode), 058 (the interludes, built on this state).

## Risks / notes
- Open questions: warmup and a ready check before the first round (CS2
  has a warmup until the players are there); the money in overtime; what
  happens when a team is empty during the match (pause, forfeit).
- ReGameDLL_CS's `mp_maxrounds` / `mp_winlimit` end a map, not a match
  with sides: the state above replaces them in `match`.
