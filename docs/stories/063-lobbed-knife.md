# 063 - The knife lobbed, deadly as it comes down (Counter-Strike)

## Context
062 throws the knife straight at short range. A second throw reaches
over a cover or further: the knife lobbed high, aimed upwards like a
grenade, that hits hard only on its way down.

## Goal
With the knife in hand, holding E and pressing the secondary attack lobs
the knife: slower than the straight throw, under full gravity, along a
curve one aims as a grenade's. Rising, it barely hurts (40 to 50); coming
down it kills (100 in the head and the body, 90 in the arms and legs).
It then lies on the ground and is picked up as in 062.

## Scope
In: ReGameDLL_CS (the lob as a second way to throw 062's `CThrownKnife`,
its damage by phase), `cs16-client` (the prediction with E and the
secondary attack, the lob's animation).

Out: a trajectory preview (CS2 has none for grenades either), bots
lobbing.

## Status (2026-10-16)
Done with 062: E + secondary attack lobs the knife on an HE grenade's
very flight (the same raised aim, `( 90 - pitch ) * 6` units/s up to 750,
the same start, gravity 0.55); 1219 units from de_dust2's terrorist spawn
aimed up. Rising 45, falling 100 / 100 / 90. Left: hits rising and falling
measured on a player, a lob over a cover in play.

## Design
- Input: `CKnife::SecondaryAttack` with `IN_USE` held lobs; without E it
  stabs as in 1.6. With a shield, E + secondary attack keeps raising the
  shield for a start (no lob); to be decided in play.
- The lob: the same `CThrownKnife` on the physics of an HE grenade
  (`CHEGrenade::WeaponIdle`, `CGrenade::ShootTimed2`): the aim raised by
  10°, ( 90 - pitch ) * 6 units/s up to 750, plus the thrower's velocity,
  a gravity of 0.55; it tumbles end over end.
- Phase: rising while its vertical speed is above zero, falling after.
  Damage by hit group and phase, set as 062's:
  rising 45 anywhere (`mp_knife_lob_dmg_rise`, the 40 to 50 asked);
  falling: head 100, chest and stomach 100, arms and legs 90
  (`mp_knife_lob_dmg_head`, `_body`, `_limb`). Armour takes nothing off,
  as 062's.
- Everything else as 062: one hit, the bounce, the knife on the ground,
  its pickup, one knife per player, the kill icon.

## Tasks
- [ ] `CKnife`: E + secondary attack lobs; without E, the stab.
- [ ] The lob's speed and gravity; damage by phase and hit group.
- [ ] Client prediction, the lob's animation (062's throw, played higher).
- [ ] Test: a lob over the double doors of de_dust2's long A, rising hits
      at close range, falling hits from a height.

## Acceptance criteria
- E + right click lobs the knife on a curve that one aims upwards; it can
  land behind a crate.
- Hit on its way up: 45 damage; on its way down: a player at 100 health
  dies from a head or body hit, keeps 10 from a limb.
- Without E, the right click stabs as before.

## Dependencies
062.

## Risks / notes
- The lob coming down kills from the body (100) while the straight throw
  leaves 10 (90): the lob is the stronger throw, paid for by its slow
  flight and its aim. To be checked in play that it does not become the
  only throw used.
