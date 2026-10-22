# 057 - Match mode: MR13 with its overtime (Counter-Strike)

## Context
A Counter-Strike 2 competitive match is played in MR12 (first to 13), the
teams switch sides at half time, a 12-12 draw goes to overtime. On a
Counter-Strike 1.6 server this needs an admin or a plugin to swap the
teams, count the rounds and stop the match. The `match` mode of
[056](056-server-game-modes.md) should run a whole match by itself.

## Status (2026-10-22)
Done in ReGameDLL_CS (`dlls/game_modes.cpp`: `MatchThink`,
`MatchRoundRestart`, `MatchHoldsIntermission`), with the cvars of the
design and the settings of `modes/match.cfg` (056).

- Warmup: the game library sets respawn, buy anywhere, $16 000 and no
  freeze over the mode's config and restarts; "Warmup: waiting for players
  (7/10)" every 10 s; with `mp_match_players` in the teams (bots only with
  `mp_match_allow_bots 1`, otherwise kicked), "the match starts in N s",
  back to waiting if one leaves; then `modes/match.cfg` again and a
  complete restart (scores, money, weapons).
- Rounds counted from the teams' wins (a drawn round, rare, doesn't
  count). Half time after `mp_match_rounds / 2`: `SwapAllPlayers` (players,
  models and the teams' scores), everyone respawned with the default pistol
  and the start money, no bonus nor losing streak carried. Overtime at
  12-12 with `mp_match_ot_money` (no switch at its start, as in CS2),
  switch after its half; first to 13, then to 16; 15-15 a draw.
- End: "The Terrorists win the match 13-9" (or "Draw 15-15") in the centre,
  the chat and the log, then the intermission (scoreboard), held until a
  `sv_mode` while `mp_match_end_delay` is 0, else the next map after that
  many seconds.

Tested on 2026-10-22 on the dedicated server of the installed set with 6
YaPB bots and a test `cstrike/modes/match.cfg` (`mp_match_rounds 4`,
`mp_match_players 2`, `mp_match_warmup_time 10`, `mp_match_allow_bots 1`,
`mp_match_ot_rounds 2`): a match played by the bots to 3-1 with its half
time, the server held on the result; then with rounds forced by
`endround`: half time, 2-2 overtime, its switch and a 3-3 draw, and a 4-2
overtime win; with `mp_match_players 10` and 6 bots the warmup waited, and
started once lowered to 6. Soak test the same night with the installer's
set and only `mp_match_allow_bots 1`, `mp_match_players 2`,
`mp_match_warmup_time 10` changed: a whole MR12 match of 10 YaPB bots,
25 minutes, half time after 12 rounds, "The Terrorists win the match
13-8", the interludes of 058 (10 players at the start and half time, the 5
winners at the end), no error, the server held on the result. Not checked
by a player: the money after a
switch and the camera of the dead; a full MR12 match with ten players.

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
- Players only: no bots in a match (`yb_quota 0`, bots kicked when the
  mode starts); `mp_match_allow_bots 1` exists for tests only, off by
  default.
- Warmup: while fewer than `mp_match_players` (10) players are on the
  server (spectators don't count), the match waits in warmup:
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
  money (800), weapons and equipment removed, the round counter goes on
  (CS2's `mp_halftime 1`).
  The team scores on the board follow the teams.
- End of regulation: a team reaching 13 wins, the match ends.
- Overtime at 12-12: rounds 25 to 30 (CS2's `mp_overtime_maxrounds 6`,
  one overtime as `mp_overtime_limit 1`), money of each player set to
  $10 000 at its start and at its switch (`mp_overtime_startmoney`: the
  value given by CS2 server documentation, its compiled default couldn't
  be read), sides switched after round 27, a team reaching 16 wins, 15-15
  ends in a draw.
- Match end: the winner (or the draw) announced, the scores frozen, no
  more rounds; the server then waits for a `sv_mode` command or goes to the
  next map after `mp_match_end_delay` seconds (0: stays).
- Settings of the mode, those of CS2's `gamemode_competitive.cfg`: start
  money 800, maximum money 16 000, freeze time 15 s, buy time 20 s, round
  time 1.92 min (1:55); `mp_forcecamera 1` (a dead player only watches
  their teammates), `mp_freeforall 0`.
- Cvars: `mp_match_players 10`, `mp_match_warmup_time 60`,
  `mp_match_rounds 24` (MR12: first to 13), `mp_match_overtime 1`,
  `mp_match_ot_rounds 6`, `mp_match_ot_money 10000`,
  `mp_match_end_delay 0`.
- Chat and centre messages: "Half time, switching sides", "Overtime",
  "<team> wins the match 13-9", "Draw 15-15".

## Tasks
- [x] Warmup until 10 players, then a minute, then the first round.
- [x] Match state and team scores kept across switches.
- [x] Half time switch with money reset; overtime with its money and
      switch; end at 13, 16 or 15-15.
- [x] Cvars and the `match` mode's config; messages.
- [x] No bots in the mode; `mp_match_allow_bots` for tests.
- [ ] Test with bots allowed for the test and short cvars
      (`mp_match_rounds 4`, `mp_match_players 2`) to reach half time,
      overtime and both ends quickly; then a full match of players on the
      test server.

## Acceptance criteria
- With 9 players the server stays in warmup; the tenth starts a minute of
  warmup, after which the match begins.
- Without an admin: the teams switch after round 12, the first to 13 wins,
  12-12 plays overtime, the first to 16 wins, 15-15 stops as a draw.
- After a switch each team's score is still its own, every player starts
  with $800 (overtime: its money).
- Dead players only watch their teammates.
- No bot joins a match.

## Dependencies
056 (the mode), 058 (the interludes, built on this state).

## Risks / notes
- Open question: what happens when a team is empty or players leave during
  the match (pause, forfeit).
- CS2's competitive warmup is 120 s, 15 s once everyone is connected; this
  match waits for 10 players, then one minute, as asked.
- ReGameDLL_CS's `mp_maxrounds` / `mp_winlimit` end a map, not a match
  with sides: the state above replaces them in `match`.
