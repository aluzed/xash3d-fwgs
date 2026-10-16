# 065 - Dust2: small crates to climb alone (Counter-Strike)

## Context
In de_dust2's mid, the big crate against the catwalk's wall (the "xbox",
`SandCrtLrg`, 96 x 96 x 96: x -352..-256, y 1376..1472, its top at z -32
over the mid's floor at -128) is too high to jump on: a crouched jump
lifts about 63 units. One climbs it with a boost only; from its top the
catwalk (z 0..32, towards A short) is one jump away. Counter-Strike 2's
Dust2 has a smaller crate beside it: anyone climbs alone, mid to catwalk
flows.

This set is its own game (story 064): its de_dust2 is de_dust2, changed in
place; the installer's wizard takes the map from the player's Steam
Counter-Strike.

## Goal
A small crate, as the map's own (`SandCrtSm`, 48 x 48 x 48), behind the
big one on the terrorists' side, against the catwalk's wall, hidden from
the mid doors by the big crate: floor (-128)
→ small crate (-80) → big crate (-32) → catwalk, each step a crouched
jump.

## Scope
In: the crate in de_dust2's BSP (geometry, collision, textures, light),
the bots' graphs (YaPB, ReGameDLL_CS's navigation), the installer making
the map.

Out: other changes to Dust2 (each its own story), a remake of the map.

## Status (2026-10-16)
Done in `lot64`, played: `cstrike-17/scripts/maps/patch_dust2.py`
(with `bsp30.py`) writes de_dust2 from the Steam map with five small
crates (48), brush models built as the map's own crates (a node per side,
empty leaves, the shared solid leaf, clip nodes for the three hulls), the
map's crate textures, their light sampled from the faces beside them; the
rest of the map byte for byte:
- mid, at the big crate (the "xbox"): one on the floor on the
  terrorists' side against the catwalk's wall, one on the big crate's
  front (the mid doors' side): floor -128, -80, -32, 16, the parapet 32,
  the short;
- CT spawn's "elevator" (the crate stack): one on the small crate at the
  top, against the north wall: 40, 88, the short's parapet 128, the short
  96;
- the terrorists' big crates under the upper passage (x -1536, y -128):
  one below against the wall (16..64), one on them against the parapet
  (96..144): the parapet 160, the passage 128.
The user placed them from `viewpos` (printed in `engine.log`). Left: the
bots' graphs, the installer's step, YaPB's warning (the map's size
changed).

## Design
- Where: x -304..-256, y 1328..1376, z -128..-80: south of the big
  crate (the terrorists' side), against the catwalk's wall (x -256),
  hidden by the big crate from the mid doors (the user's choice). Two of
  its faces touch the wall and the big crate and are not drawn. The
  mid's lane stays as wide.
- How: `scripts/maps/patch_dust2.py` reads the player's de_dust2.bsp
  (Steam) and writes cstrike-17's de_dust2.bsp with one more brush model,
  a `func_wall` box: its six faces textured as the map's small crates
  (`SandCrtSmSd`, `SandCrtSmTp`, already in the BSP), its nodes and
  clip nodes for the four hulls (player standing, crouched, point,
  large), its lightmaps taken from the floor's light at that spot,
  shaded per face as the big crate's are. The world itself is not
  recompiled: no decompilation of Dust2, the rest of the map identical
  byte for byte.
  Considered: decompiling to a .map and recompiling with VHLT (free
  geometry and a real light bake, but a decompiled Dust2 is never clean:
  for later, bigger changes); editing by hand with bspguy (the same
  result, not reproducible from the Steam map).
- The installer (story 052) runs the patch on the Steam map; the dev set
  (`lot64`) gets it the same way.
- Bots: YaPB's graph for de_dust2 gets the climb (nodes on both crates,
  jump links); ReGameDLL_CS's `.nav` regenerated (or the old one used
  where it still fits).

## Tasks
- [ ] The patch: the brush model, its faces, lightmaps, clip nodes.
- [ ] In game: climbing alone, the crate lit as its neighbours, ref_gl and
      ref_vk (rt), no hole in the collision.
- [ ] YaPB graph, ReGameDLL_CS nav.
- [ ] The installer's step, `lot64`.

## Acceptance criteria
- From the mid's floor a player alone reaches the catwalk over the two
  crates with crouched jumps.
- The crate looks like Dust2's small crates, lit like its neighbours.
- Nothing else in de_dust2 changes.

## Dependencies
064 (the game is its own: its maps are its own).

## Risks / notes
- A brush model's clip nodes must be right for every hull, or players
  stick or fall through: tested standing, crouched, with bots.
- The light is copied, not baked: a shadow the crate should cast on the
  floor is missing (small at this size).
