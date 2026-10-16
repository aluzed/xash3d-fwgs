# 062 - The knife thrown straight, and picked up from the ground (Counter-Strike)

## Context
Counter-Strike 1.6 has a knife for close range only: a slash (primary
attack, 15 or 20 damage) and a stab (secondary, 65, a back stab more).
It can be neither dropped nor thrown. CS2 adds the Zeus for a close
kill at a short distance; this set has no Zeus. A thrown knife does that
job instead: one throw at short range that can kill, with the knife lost
until it is picked up again.

In ReGameDLL_CS the knife is `CKnife` (`dlls/wpn_shared/wpn_knife.cpp`),
predicted on the client by `cs16-client`. The use key (`+use`, E) only
reaches the server as `IN_USE` in the buttons (doors, hostages, the
bomb's defuse in `CBasePlayer::PlayerUse`).

## Goal
With the knife in hand, holding E and pressing the primary attack throws
the knife straight where one aims: fast, a flat flight that drops after a
while like a ballistic projectile, a short range. It kills in the head and
almost kills in the body. The knife is then on the ground (or where it
fell) and anyone without a knife picks it up by walking over it. Without
E, the knife slashes and stabs as in 1.6.

## Scope
In: ReGameDLL_CS (the throw, the flying knife, its hits, the knife on the
ground and its pickup, a player without a knife), `cs16-client` (the
prediction of the knife with E held, the throw's animation and sounds),
the models (the knife in flight and on the ground), the kill icon.

Out: the lobbed throw (063), throwing from another weapon in hand, bots
throwing (they keep stabbing; ReGameDLL_CS's bots and YaPB only have to
survive a player without a knife), a knife stuck in walls.

## Status (2026-10-16)
Done, `dlls/throwing_knife.cpp` (ReGameDLL_CS): E + primary attack plays
the stab's swing and lets the knife go 0.2 s later; the player takes their
next weapon (or empty hands), the knife flies as `throwing_knife`, moved by
the engine without collisions and traced each frame against the world and
the hitboxes. 1000 units/s, flat for 0.25 s then 1.2 times the gravity:
about 600 units on flat ground, 0.6 s of flight at that range, so that a
knife thrown from afar can be seen and dodged (1400 units/s was a bullet).
Damage set by hit group, neither multiplied nor lessened by armour
(`CBasePlayer::TakeDamage` skips the armour for a `throwing_knife`); the
teammate reduction of `ff_damage_reduction_other` applies as to any weapon.
Off a wall it keeps a quarter of its speed, off a floor a tenth, then lies
(`SOLID_TRIGGER`, `MOVETYPE_TOSS`); anyone without a knife picks it up by
walking over it; a survivor without one gets one at the next round; the
round's restart removes them. Kill feed: the knife's icon.
`mp_knife_throw_debug 1` prints to the thrower where it hit and lies.

Found on the way: an entity named with `MAKE_STRING` is missing from
ReGameDLL_CS's classname hash, so `UTIL_RemoveOther` never found it: the
molotov's bottle, fire and flames (054) were never removed at a round's
restart either; all are named with `MAKE_STRING_CLASS` now.

Played: the knife's spin turned the model about the end of its blade and
drew circles: it flies point first now, along its curve. Valve's
`w_knife.mdl` is a knife held by a hand 50 units over its origin, upright:
the knife seemed thrown from over the head and floated upright at mid
height on the ground; `scripts/make_throwing_knife.py` makes
`w_throwknife.mdl` (cstrike-17 extras), a combat knife lying flat along its
flight, its balance point on the origin, 1024 texture. The straight throw
leaves from the hand (below the eyes, to the right), aimed at what the
crosshair is on. E held with the knife readies the throw: the arm goes down
out of sight (`V_CalcKnifeCharge`, the client) and comes back up with the
throw. A knife hitting a wall was put back in it as a box and stayed stuck:
it rests as a point off the surface, and is picked up wherever it lies.

Tested on de_dust2 against a frozen YaPB teammate (ff reduction 1): a body
hit takes 90 (100 to 10), a head hit kills, with the headshot in the kill
feed; a knife on the ground picked up; a throw at a wall, at the floor;
the restart removes the knife. Left: limbs (70) measured, a second human
player, bots coping
without a knife over a whole match, a kill icon of its own.

## Design
- Input: in `CKnife::PrimaryAttack`, `IN_USE` held (`pev->button`) means
  a throw instead of a slash. E pressed first still does its use (a door
  in front opens); while defusing or carrying a hostage nothing is
  thrown, the use wins. The client's predicted knife reads the same
  button so that the slash animation is not played and then corrected.
- The throw: a `CThrownKnife` entity from the eyes, along the aim, 1000
  units/s (`mp_knife_throw_speed`), no gravity for its first 0.25 s
  (`mp_knife_throw_flat`) then a gravity of 1.2
  (`mp_knife_throw_gravity`): flat up to about 250 units, on the ground
  around 600; slow enough to be dodged from afar, it is no bullet. It
  spins along its flight (the model rotated, not the hitbox).
- Hits: each frame a trace against the players' hitboxes from the last
  position to the new one (not a touch of its box: a touch has no hit
  group), ignoring the thrower for the first 0.2 s. Damage by hit group,
  set and not multiplied by CS's own multipliers:
  head 100, chest and stomach 90, arms and legs 70
  (`mp_knife_throw_dmg_head`, `_body`, `_limb`). Kevlar and helmet
  take nothing off and are not worn down: the thrown knife plays the
  Zeus's part, a sure kill for whoever hits. Friendly fire as the
  server's `mp_friendlyfire`. One hit only: the knife then drops at the
  victim's feet.
- A wall, a floor, a prop: the knife bounces once with most of its speed
  lost, falls and lies flat; the knife's wall sound, a spark. Breakables
  take the body damage.
- On the ground: a `CThrownKnife` at rest becomes a pickup, as a dropped
  weapon: touching it with no knife gives the knife back (deployed if the
  player had nothing else in hand); a player who has a knife walks over
  it. Removed at the round's restart; at most one knife per player at
  any time.
- Without a knife: slot 3 is empty, `lastinv` and the weapon slots skip
  it; a player with no weapon at all has empty hands (the engine accepts
  it, CS never had it: tested). The next round gives a knife back as
  always.
- The throw takes the knife's place at once: the view model plays a
  throw (the stab's start played fast if no animation can be made, a
  real throw made later), then the next weapon is deployed.
- Assets: a knife model for the flight and the ground (from `p_knife.mdl`
  if it reads well at that size, else made, 4K-ready textures as the
  other new assets), the throw's swish (`knife_slash`), the hit sounds of
  the knife, a kill icon "throwing knife" in the death notices.
- Cvars: `mp_knife_throw 1` (0: the throw is off, E + attack slashes).

## Tasks
- [ ] `CKnife`: E + primary attack throws; E alone still uses.
- [ ] `CThrownKnife`: the flight, its fall, the trace against hitboxes,
      the damage by hit group, the bounce and rest.
- [ ] The knife on the ground picked up; one knife per player; removed
      at the round's restart.
- [ ] A player without a knife: the HUD's slots, the weapon switch, empty
      hands; the bots coping with it.
- [ ] Client prediction with E held, the throw's animation and sounds.
- [ ] The flying and lying knife model, the kill icon.
- [ ] Test on de_dust2 with a second player or a bot: ranges, damage by
      hit group with and without armour, pickup by both teams, a throw
      at a door, while defusing.

## Acceptance criteria
- Only with the knife in hand: E + click with another weapon fires it as
  usual.
- E + click throws the knife where the crosshair is; at 300 units it hits
  where one aimed, past about 700 it has fallen to the ground.
- A head hit kills a player at 100 health; a body hit leaves 10; a leg or
  an arm leaves 30; the same with kevlar and helmet.
- The thrown knife can be picked up by anyone without a knife by walking
  over it; nobody ever holds two.
- Without E, the knife slashes and stabs exactly as before.
- E still opens doors, takes hostages and defuses the bomb.

## Dependencies
None.

## Risks / notes
- CS was never played without a knife: code that takes the knife for
  granted (the bots' "switch to knife", the weapon slots, the shield's
  knife) is found and tested.
- E + click is a chord, not an input of its own: a player who binds +use
  elsewhere throws with that key. A `+knifethrow` command could be added
  for those who want one key.
