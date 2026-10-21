# 058 - Match interludes: the teams shown at the start, the switches and the end

## Context
Counter-Strike 2 opens a match by showing the two teams before the first
round, and closes it on the winners. The `match` mode of
[057](057-match-mr13.md) has the moments for it: the start, the half time
switch, the start of overtime, its switch, the end.

## Status (2026-10-22)
Done: server in ReGameDLL_CS (`dlls/game_modes.cpp`, `MatchRoundStarted`,
`MatchPlayerThink`), client in `cs16-client` (`cl_dll/interlude.cpp`).

- Server: at the match's start, half time, overtime's start and switch,
  the round's freeze gets `mp_match_interlude_time` (6 s) more at its
  start; the players' buttons are cleared and their angles held every frame,
  weapon, buy, drop and team commands refused, `CanPlayerBuy` false. The
  message `Interlude` (kind, duration, up to 20 players: index, team,
  origin, yaw) goes to everyone; the shown players get
  `EF_FORCEVISIBILITY` so that clients far from them still receive them.
  The match's end plays one more round start for the winners' interlude:
  the losers hidden (`EF_NODRAW`, not solid), then the intermission.
- Client: during the interlude the view is the camera, the local player is
  drawn among the others (`CL_IsThirdPerson`), the HUD (crosshair, money,
  timer, chat of the HUD) is replaced by the shown players' names under
  their feet in their team's colour. The camera: a group (a team, or both
  when their centres are within 1400 units) seen from one of 8 sides, at
  the usual height or from above, the one with the most players in view
  from where it ends (traced against the world), then the least pulled in,
  in front of the players when as good; it starts close and travels back,
  every position traced so that it never enters a wall. Teams farther
  apart are shown one after the other, half the time each.

Tested on 2026-10-22 with the installed set, a client joined to the
dedicated server with 6 YaPB bots and the short test match of 057: the
start on de_dust2 (terrorists, then counter-terrorists, the local player
among them, names in their colours), half time, the end showing only the
3 winning terrorists then the scoreboard; the start on de_nuke and
de_inferno. Seen: a few lines of VGUI2 text (chat, "joining the
terrorist force") stay at the bottom and top of the screen during an
interlude; the camera on de_nuke's open spawn stays far.

## Goal
A short interlude, a few seconds, at each of these moments:
- start of the match, half time, start of overtime, overtime switch: the
  two teams standing in their spawns, a camera that frames everyone and
  travels slowly back, each player's name under their model;
- end of the match: only the winning team is shown, the same way; the
  losers aren't. A draw shows both teams.

## Scope
In: ReGameDLL_CS (the interlude's timing, players held in place, a message
to the clients), `cs16-client` (the camera, the names under the models,
the HUD hidden during the interlude).

Out: poses or animations of the players (they stand still with their
weapon), music, a camera per map made by hand.

## Design
- Server: at each moment of 057, the round starts with a longer freeze
  (`mp_match_interlude_time`, 6 s by default) during which the players are
  held still in their spawns: no movement, no turning, no shooting, no
  buying, no weapon switch; their inputs are ignored, their view is the
  interlude's camera, nobody controls their player during it; the end of the
  match spawns the winners in their spawns once more, the losers left as
  spectators for the interlude.
- A new message `Interlude` (kind: start, half time, overtime, overtime
  switch, end; the team or teams shown; duration) sent to everyone.
- Client camera: computed from the shown players' positions: the centre of
  each team's group, a point of view above and in front of the group (or of
  the two groups, between the two spawns, looking at both when they can be
  framed together, else one after the other), starting close and
  travelling back slowly until everyone is in view; checked against the
  walls with a trace so that it never goes through the map.
- Names: each shown player's name drawn under their model, in their team's
  colour, projected from the feet each frame.
- HUD, crosshair, view model and chat hidden during the interlude; the
  interlude can't be skipped but ends with the freeze.
- Spectators and dead players see the same interlude.

## Tasks
- [x] Server: the interlude's freeze at the five moments, players held
      still and their inputs ignored,
      losers kept out at the end; `Interlude` message.
- [x] Client: the camera from the players' positions, its travel, the
      checks against the walls.
- [x] Client: names under the models; HUD hidden.
- [x] Test on de_dust2, de_inferno, de_nuke (spawns far apart, indoors) with
      bots and `mp_match_rounds 4`.

## Acceptance criteria
- During an interlude no player moves, turns or acts, whatever is pressed.
- At the start, the half time and the overtime's start and switch, both
  teams are seen in their spawns with their names, the camera travelling
  back, for the interlude's length, then the round starts normally.
- At the end, only the winners are shown (both teams on a draw).
- The camera never shows the inside of a wall or the void.

## Dependencies
057 (the match's moments).

## Risks / notes
- Spawns of the two teams are often too far apart to frame together: the
  camera then shows one team and the other, half the time each.
- Names under the models in 3D need the client to know each player's
  position, also for players outside its view (sent in the message).
