# 054 - Molotov and incendiary grenade (Counter-Strike)

## Context
Counter-Strike 2 has fire grenades: the terrorists' molotov and the
counter-terrorists' incendiary. Thrown, the bottle flies lower and shorter
than a grenade, bursts into a pool of fire where it lands on the ground, or
in the air without fire when its fuse runs out first; the fire burns for a
few seconds, hurts whoever stands in it, and a smoke grenade puts it out.

The set of [052](052-windows-installer.md) plays on its own Xash3D server
with ReGameDLL_CS and `cs16-client`, both built from source: the weapon can
be added on both sides without touching the engine or the protocol.

## Goal
A molotov (T) and an incendiary grenade (CT) bought in the equipment menu,
thrown like the other grenades, with CS2's behaviour, played on the test
server.

## Scope
In: ReGameDLL_CS (weapon, projectile, fire, purchase, cvars), `cs16-client`
(weapon prediction, HUD, buy menu, kill icon, effects), sounds and sprites
from Half-Life and Counter-Strike, placeholder models.

Out: original clients (Steam's CS 1.6 or a stock `cs16-client`) on a server
with the weapon: they don't know it, nothing is done for them. Bots using
it (YaPB) at first.

## Status (2026-10-15)
First version in ReGameDLL_CS and `cs16-client`, the smoke grenade's models
for both teams. Tested on a local server: bought from the equipment menu
(T $400, CT $600), thrown, bursts on the ground into fire spreading in a
ring, burns (100, 80, 60 HP, then the kill feed "killed self with
molotov"), the incendiary's fire at a distance. Left: the smoke putting it
out and the air burst seen in play, the test server (its Linux ReGameDLL
needs the change too), the bottle model, a kill icon.

## Design
- Weapon id 2 (`WEAPON_GLOCK`, never used by CS 1.6) for both: the team
  picks the model, the fire's length and the price, like the CS2 pair.
- Projectile: a grenade with more gravity and less throw speed (a heavy
  bottle); bounces off walls; bursts on touching a floor (surface normal
  pointing up enough); bursts in the air without fire when its fuse runs
  out (`mp_molotov_detonate_time`, CS:GO's 2 seconds by default).
- Fire: points of flame laid on the ground around the burst (traces down
  from a ring), spreading over a fraction of a second, burning
  `mp_molotov_duration` (7 s, incendiary 5.5 s), hurting about 40 HP a
  second whoever stands in a flame (burn damage, armour counts), put out
  by a smoke grenade's cloud. Fire sprites and sounds of Half-Life and CS.
- Purchase: equipment menu (8), item 8 for the terrorists, 9 for the
  counter-terrorists (the shield is 8); console `molotov`; $400 / $600 as in
  CS2; one at a time.
- Models: the smoke grenade's for both until a bottle with a clear licence
  is found (GameBanana, credited) or made.

## Tasks
- [x] Server: `weapon_molotov`, its throw and the projectile with its physics
      and fuse (cvars for gravity, speed, fuse).
- [x] Server: the fire entity: points on the ground, spread, damage,
      lifetime, smoke puts it out; the killer and weapon in death notices.
- [x] Server: purchase (menu entries, `molotov` command, team prices),
      round reset, drop on death, `mp_` cvars.
- [ ] Client: weapon prediction, HUD (weapon list, ammo, selection),
      buy menu entry, kill icon, burst and fire effects.
- [ ] Test on a local server, then on the test server with players.
- [ ] Bottle model for the terrorists (v_, p_, w_), credited.
- [ ] Documentation: `cs16-client` README, cvars.

## Acceptance criteria
- Bought in the equipment menu by both teams at their price; thrown, it
  arcs lower than a grenade; bursts on the ground into fire that burns for
  its length, or in the air without fire after its fuse.
- Standing in the fire hurts as in CS2; a smoke grenade puts it out; the
  kill feed names the thrower and the molotov.
- No change to the protocol or the engine; the other weapons unchanged.

## Dependencies
052 (the set), the test server of 034.

## Risks / notes
- Weapon id 2 is free in CS 1.6, but plugins (Metamod, AMX) may iterate
  weapon ids and meet an unknown one.
- Models of molotovs found online are often ripped from other games: only a
  model with a clear licence goes into the set.
