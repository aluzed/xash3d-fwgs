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
bots' view through a smoke), `cs16-client` (the puffs pushed, thinned,
coming back).

Out: bullets thinning a smoke (CS2 does it, a later story), smokes as a
real volume (voxels), the molotov's fire (054 already puts it out with a
smoke).

## Design
- Server: when an HE grenade explodes, a message `SmokeBlast` (origin,
  radius of the blast, about the grenade's damage radius, 350 units) to
  everyone in the PVS, sent only when a smoke is active within reach (the
  server keeps the smokes for 054 and the bots).
- Client: every smoke puff within the radius is
  - pushed away from the blast's centre, fast at first and slowing down,
    further when it was close (a few dozen to 150 units);
  - made transparent in proportion to its closeness (the closest almost
    invisible), in a fraction of a second;
  - after `cl_smoke_blast_hold` (about 1.5 s), drifts back to its own
    position and recovers its opacity over about 2 to 3 seconds, as CS2's
    smoke flows back in;
  - keeps its own end: the fade after 15 s and the removal at the end of
    the round are unchanged, a blast never lengthens a smoke.
- Several blasts on one smoke: each pushes from its own centre, the
  return starts after the last.
- Bots: during the hole, ReGameDLL_CS's bots see through the part of the
  smoke that was blown away (its line-of-sight check gets the blast and
  its time); YaPB: check whether its smoke test can be told the same, else
  noted as a limit.

## Tasks
- [ ] Server: `SmokeBlast` when an HE grenade goes off near an active
      smoke.
- [ ] Client: puffs pushed and thinned by distance, held, coming back;
      their fade and end unchanged.
- [ ] Bots of ReGameDLL_CS seeing through the hole; YaPB looked at.
- [ ] Test: a smoke on de_dust2's mid, an HE in it, from outside and from
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
- The smoke is 20 large sprites: a hole looks coarse; more, smaller puffs
  for a smoke may be needed for the effect to read well (to try, cost of
  the sprites checked on ref_vk).
- Every client draws its own puffs at random places: two players don't
  see exactly the same hole, as today they don't see exactly the same
  smoke.
