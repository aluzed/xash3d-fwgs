# 018 - Sprites

## Tasks
- [x] Sprite loading via `Mod_ProcessRenderData` (SPR v1/v2, Quake sprites).
- [x] Orientations: parallel, parallel upright, facing upright, oriented,
      parallel oriented.
- [x] Render modes, frame interpolation (`r_sprite_lerping` style), sprite
      groups.
- [x] Glow sprites with occlusion test (`kRenderGlow`): GPU occlusion query
      or depth-buffer sampling in shader, faded like `ref_gl`.
- [x] HUD sprites via `SPR_*` client API paths (2D, shared with 010).

## Acceptance criteria
- Muzzle flashes, explosions, CS smoke grenade puffs, glow lights match
  `ref_gl`.

## Progress (2026-10-14)
- `vk_sprite.c` is ref_gl's `gl_sprite.c` on the GL-state emulation of
  `vk_imm.c`, like the studio models: orientations, render modes, frame
  lerping, sprite groups and angled frames, sprite lighting. Sprite frames
  are loaded by the engine through the texture API.
- Glow sprites keep ref_gl's test, a trace line from the eye plus the
  distance fade, not a GPU occlusion query: same result as ref_gl.
- `WorldToScreen`, `ScreenToWorld` and `TriGetMatrix` are no longer stubs.
- Harness: `fire_cstrike` static scene (AK-47 burst on de_dust2) above
  49 dB; the missing bullet sparks are particles (story 019).

## Dependencies
017.
