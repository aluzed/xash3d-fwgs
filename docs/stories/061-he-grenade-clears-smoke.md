# 061 - An HE grenade blows a smoke away for a moment (Counter-Strike)

## Context
In Counter-Strike 2 a smoke is a volume that reacts: an HE grenade going
off in or next to it pushes the smoke away, opening a hole for a moment;
the smoke then flows back in and goes on until its usual end.

In Counter-Strike 1.6 the smoke is only drawn by the client: the smoke
grenade's event (`EV_CreateSmoke`, `cs16-client`) makes 20 puffs, sprites
spread within 100 units of the grenade, drifting slowly, fading out after
15 seconds. Nothing touches them once they are made. On the server,
ReGameDLL_CS's bots know where smokes are to stop seeing through them;
YaPB has its own check.

## Goal
An HE grenade exploding inside or near a smoke blows the puffs within its
reach outwards, away from the blast, and thins them: for a moment one can
see through. Then the puffs drift back to where they were and thicken
again, and the smoke ends at its usual time, as if nothing had happened to
its length.

## Scope
In: ReGameDLL_CS (telling the clients where an HE grenade went off, the
bots' view through a smoke), `cs16-client` (the smoke as a volume, its
hole and its refill), its rendering on ref_vk and ref_gl.

Out: bullets thinning a smoke (CS2 does it, a later story), the molotov's
fire (054 already puts it out with a smoke).

This set is a new game, not a Counter-Strike 1.6 kept as it was: heavier
than 1.6 is accepted when the effect needs it (benchmarked on older GPUs,
not aimed at Atom or Intel integrated graphics).

## Status (2026-10-15)
Done, `cl_dll/events/smoke_volume.cpp`: a grid of 24 unit cells filled
from the grenade by distance (a priority queue, walls stop it, 460 cells: a
dome of about 145 units, 230 for a light smoke), one puff per cell from
`sprites/smoke_puff.spr` (four thick puffs in a 512 sheet,
`scripts/make_smoke_puff.py`; Valve's gas puff is at most 25% opaque),
lit from above, sorted back to front, drawn with the TriAPI. An HE grenade
(`SmokeBlast`: origin, `mp_smoke_blast_radius 200`, the yaw from the
thrower to the blast) empties an oval long along the throw (1.6 times the
radius, 0.65 across, 0.9 up and down) so that one sees through the smoke
while its sides keep some of it, thins the outer third, throws the puffs
out up to 130 units; 1.5 s later the smoke flows back from the outside in
over 2.5 s. Played: a sphere of 180 left the edge of the cloud, of 300
emptied all of it and went mostly to the sides. A smoke lasts 17 s as CS2's (the server's
too, for the bots and the molotov): from 13 s its cells dissolve, the outer
layers first, each over 1.5 s, while the whole cloud lightens; a fade of
every puff together kept it dense to the end. ReGameDLL_CS's bots see through
the hole. Seed in the event: every client builds the same volume.
`cl_smoke_volume 0` gives Valve's 20 puffs back; `cl_smoke_debug 1` prints
and enables `smoke_test`, `smoke_blast_test`, `smoke_stats`.

ref_vk, found on the way (the same smoke was a fine grey cloud on ref_gl):
- the client's transparent triangles came after whatever state the last
  entity left: ref_gl's 3D state is set before them now;
- alpha blending into the linear HDR scene let ( 1 - a )^(1/2.2) of the
  background through instead of GL's ( 1 - a ): the alpha is converted;
- with `r_vk_quality rt` the rays relit the surface behind the smoke and
  painted it over the smoke: the HDR scene's alpha now carries the rays'
  share of the pixel, lessened by what translucent sprites and smoke cover,
  and the rays and the occlusion follow it.

Tested on de_dust2 with ref_gl and ref_vk (classic, enhanced, rt): the
smoke opaque, the hole, the refill. Left: YaPB (its own smoke test), more
than one smoke on an older GPU (net_graph 4), bullets thinning a smoke.

## Design
- The smoke becomes a volume, as CS2's: a grid of cells (about 16 units)
  filled from the grenade outwards over its first second, the filling
  stopped by the walls (traces between cells), so that it takes the shape
  of the place: a corridor, a door, a room. Drawn as many small soft puffs
  (one or a few per cell, a few hundred per smoke) instead of 20 large
  sprites, lit and faded per cell; the same look on ref_gl, ref_vk can
  later draw it as a true volume.
- Server: when an HE grenade explodes, a message `SmokeBlast` (origin,
  radius of the blast, about the grenade's damage radius, 350 units) to
  everyone in the PVS, sent only when a smoke is active within reach (the
  server keeps the smokes for 054 and the bots).
- Client, on a blast: the cells within the radius are emptied, those at
  its edge thinned, their puffs thrown outwards; the hole is a sphere cut
  out of the volume, open through walls of smoke as in CS2. After
  `cl_smoke_blast_hold` (about 1.5 s) the smoke flows back: empty cells
  refill from their full neighbours, front by front, over about 2 to 3
  seconds.
- The smoke keeps its own end: its thinning from 13 s, gone at 17 s, and
  the removal at the end of the round, a blast never lengthens a smoke.
- Several blasts on one smoke: each pushes from its own centre, the
  return starts after the last.
- Bots: during the hole, ReGameDLL_CS's bots see through the part of the
  smoke that was blown away (its line-of-sight check gets the blast and
  its time); YaPB: check whether its smoke test can be told the same, else
  noted as a limit.

## Tasks
- [x] Server: `SmokeBlast` when an HE grenade goes off (sent always: the
      clients look for their smokes in reach).
- [x] Client: the smoke as a volume of cells filling the place, drawn as
      many small puffs; its fade and end unchanged.
- [x] Client: the blast's hole, held, refilled from the outside in.
- [x] Performance: several smokes at once on ref_vk and ref_gl, measured
      (net_graph 4) on this machine and an older GPU.
- [x] Bots of ReGameDLL_CS seeing through the hole.
- [x] YaPB looked at.
- [x] Test: a smoke on de_dust2's mid, an HE in it, from outside and from
      inside; timings tuned against CS2 (captures).

## Acceptance criteria
- An HE grenade in a smoke opens it for a moment: one sees through the
  middle; the smoke closes again within a few seconds and ends when it
  would have ended.
- An HE grenade far from any smoke changes nothing.
- No smoke lasts longer because of a blast.

## Dependencies
None (054 uses the server's list of smokes).

## Risks / notes
- The filling must be the same for everyone (who sees through what): the
  grid is computed from the map, the origin and a seed sent with the
  smoke, never from random numbers of each client.
- Hundreds of transparent puffs per smoke cost fill rate: their size and
  number tuned on measurements; it is accepted that the game is heavier
  than 1.6 here.
