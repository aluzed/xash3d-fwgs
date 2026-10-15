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
(item 6, T $400, CT $500), thrown, bursts on the ground into fire spreading in a
ring, burns (100, 80, 60 HP, then the kill feed "killed self with
molotov"), the incendiary's fire at a distance. Left: the smoke putting it
out and the air burst seen in play, the test server (its Linux ReGameDLL
needs the change too), the bottle model, a kill icon.

Looked at from above on de_dust2's bomb site A (`molotov_at 1152 2464 200`,
`r_vk_debug_view "1152 2464 520 89 90"`, `sv_novis 1`): the first pool,
150 units on a sparse ring, poured off the site's ledge and looked faint
from above. Now a hexagonal grid of flames out to 100 units, stopped by
walls and drops, each hurting within 28 units so the red circles of
`mp_molotov_debug 1` cover exactly the burning ground, and an orange light
on the ground as far as it burns. Validated by the player.

Debug (sv_cheats): `molotov_here` (at the feet), `molotov_at x y z`,
`mp_molotov_debug 1`. Seen from noclip above the map, `sv_novis 1` is
needed: the flames are entities, sent only inside the player's view.

Icons and menus: the weapon list shows Valve's own MOLOTOV icon, left over
from Condition Zero in Steam's `640hud17`/`640hud18` (nothing to ship), and
for the counter-terrorists an INCENDIARY icon drawn by
`cstrike-17/scripts/make_hud_incendiary.py` (`640hud_cs17.spr`,
`weapon_incgrenade.txt`); the client reloads the icons when its player
changes team. The buy menu pictures `gfx/vgui/molotov.tga` and
`incgrenade.tga` are rendered by `make_buy_images.py`. The text buy menus
(`_vgui_menus 0`) are written by the server as the VGUI ones: no ammo,
equipment on 6, molotov / incendiary on 6 (Valve's `titles.txt` still
sold ammo on 6 and 7). Tested on a local server for both teams.

Models: a bottle with its rag for the terrorists, a canister with its
lever and pin for the counter-terrorists, made with their textures by
`cstrike-17/scripts/make_molotov_models.py` (`w_`, `p_` and the view's
part, in the extras). The view keeps the arms of Valve's
`v_smokegrenade.mdl`: the client leaves out its bodypart "weapon" and draws
`v_molotov_part.mdl` / `v_incgrenade_part.mdl` on the grenade's bone, as a
player's weapon model. The server picks `p_` and `w_` by team, and falls
back to the smoke grenade's on a server without the extras. Seen in play:
the view and the hand of both teams.

## Design
- Weapon id 2 (`WEAPON_GLOCK`, never used by CS 1.6) for both: the team
  picks the model, the fire's length and the price, like the CS2 pair.
- Projectile: thrown and falling as a flashbang (gravity 0.55, full throw
  speed: CS2's bottle is no heavier than its grenades; at first heavier,
  it fell too short in play); bounces off walls; bursts on touching a floor (surface normal
  pointing up enough); bursts in the air without fire when its fuse runs
  out (`mp_molotov_detonate_time`, CS:GO's 2 seconds by default).
- Fire: points of flame laid on the ground around the burst (traces down
  from a ring), spreading over a fraction of a second, burning
  `mp_molotov_duration` (7 s, incendiary 5.5 s), hurting about 40 HP a
  second whoever stands in a flame (burn damage, armour counts), put out
  by a smoke grenade's cloud. Fire sprites and sounds of Half-Life and CS.
- Purchase: equipment menu (6 since the ammo isn't bought any more), item 6
  for both teams, after the smoke grenade as in CS2; night vision follows:
  7 for the terrorists, 9 for the counter-terrorists after the defusal kit
  (7) and the shield (8); console `molotov` / `incgrenade`; $400 / $500 as
  in CS2; one at a time.
- Models: made here (no licence question), placed where Valve's smoke
  grenade is in its models; the view's arms stay Valve's, from the
  player's own files.

## Tasks
- [x] Server: `weapon_molotov`, its throw and the projectile with its physics
      and fuse (cvars for gravity, speed, fuse).
- [x] Server: the fire entity: points on the ground, spread, damage,
      lifetime, smoke puts it out; the killer and weapon in death notices.
- [x] Server: purchase (menu entries, `molotov` command, team prices),
      round reset, drop on death, `mp_` cvars.
- [x] Client: weapon prediction, HUD (weapon list, selection, the icon of
      each team), buy menu entry and picture, text menus.
- [ ] Client: kill icon, burst and fire effects.
- [ ] Test on a local server, then on the test server with players.
- [x] Models: a bottle for the terrorists, an incendiary canister for the
      counter-terrorists (v_, p_, w_).
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
