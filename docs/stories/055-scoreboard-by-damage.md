# 055 - Scoreboard sorted by damage (Counter-Strike)

## Context
Counter-Strike 1.6's scoreboard sorts the players of a team by frags, then
by fewest deaths. Frags count more than kills: planting the bomb, defusing
it or rescuing hostages give some (3 for a defuse), and a kill of a
teammate takes one away. The top of the board goes to who kills, even with
little damage done.

Counter-Strike 2 ranks the players by what they did to the round: the
damage dealt weighs, and a player who wears the enemies down without the
last shot still climbs the board.

## Goal
In each team, the scoreboard sorted by the damage dealt to the enemies
during the match, highest first, shown in its own column. Frags keep their
meaning and their rewards: a defuse still gives 3, the kill feed and the
server browser still count frags.

## Scope
In: ReGameDLL_CS (counting the damage, sending it), `cs16-client` (the
scoreboard's sort and column, both teams and the spectators' view),
`mp_` cvar to turn it off.

Out: the server browser and the A2S score (still frags), Steam's CS 1.6
client or a stock `cs16-client` (they keep sorting by frags), the stats of
a whole season.

## Design
- Damage counted on the server, in `CBasePlayer::TakeDamage`: the health
  really lost by an enemy (capped at what it had left, so a 400 damage AWP
  shot on a 30 HP player counts 30), armour damage left out, as CS2's ADR.
  Grenades, the molotov's fire (054) and the knife count; the world, falls
  and the bomb's blast don't.
- Teammates: damage to a teammate doesn't count (open question: CS2
  subtracts nothing from the board, but a cvar could).
- Kept over the match as the frags are: reset with the scores
  (`sv_restart`, `mp_restartround`, map change), kept over a reconnect only
  if the frags are.
- Sent to the clients in a new user message (`ScoreDmg`: player, damage),
  when it changes and at connection, as `ScoreInfo` is; no change to
  `ScoreInfo` itself, so older clients see nothing new.
- Client: sort by damage first (the player's choice), then by score
  (frags) when the damage is the same, then fewest deaths; a "DMG" column
  next to kills and deaths; the team's line sums its players' damage.
  Without the message (another server) the sort stays by frags.
- `mp_scoreboard_damage 1` (default 1): 0 sends nothing, the clients sort
  by frags.

## Tasks
- [ ] Server: count the damage dealt per player (cap, teams, sources).
- [ ] Server: reset with the scores; `ScoreDmg` message at changes and at
      connection; `mp_scoreboard_damage`.
- [ ] Client: receive `ScoreDmg`, sort by it, "DMG" column, team totals;
      fall back to frags without it.
- [ ] Test on a local server with bots: damage without kills climbs the
      board; a defuse still gives 3 frags; restart clears it.
- [ ] Test server with players; documentation of the cvar.

## Acceptance criteria
- A player who dealt the most damage to the enemies is first of their team,
  whatever their frags; at the same damage the higher score (frags) goes
  first, then the fewest deaths.
- The damage shown is the health the enemies lost, as CS2 counts it.
- Frags unchanged: defuse 3, team kill -1, kill feed and server browser as
  before.
- A client of this set on another server, or another client on this
  server, works as before.

## Dependencies
054 (the fire's damage is counted with its thrower); the test server of 034.

## Risks / notes
- One more user message per hit: sent to everyone (the board shows both
  teams) only when a player's total changes, at most once a frame per
  player.
- Bots (YaPB, ReGameDLL's) read frags for their own logic: unchanged.
- Order decided by the player: damage first, score (frags) second, rather
  than CS2's own "score" mixing kills, assists, objectives and damage.
