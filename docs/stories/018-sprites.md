# 018 - Sprites

## Tasks
- [ ] Sprite loading via `Mod_ProcessRenderData` (SPR v1/v2, Quake sprites).
- [ ] Orientations: parallel, parallel upright, facing upright, oriented,
      parallel oriented.
- [ ] Render modes, frame interpolation (`r_sprite_lerping` style), sprite
      groups.
- [ ] Glow sprites with occlusion test (`kRenderGlow`): GPU occlusion query
      or depth-buffer sampling in shader, faded like `ref_gl`.
- [ ] HUD sprites via `SPR_*` client API paths (2D, shared with 010).

## Acceptance criteria
- Muzzle flashes, explosions, CS smoke grenade puffs, glow lights match
  `ref_gl`.

## Dependencies
017.
