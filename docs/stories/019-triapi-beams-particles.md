# 019 - TriAPI, beams, particles and alias models

## Tasks
- [x] Full TriAPI (`TriBegin`/`TriEnd`, vertices, render modes, cull face,
      `SpriteTexture`, `Fog`, `WorldToScreen`, `ScreenToWorld`,
      `GetMatrix`, `BoxInPVS`, `LightAtPoint`, `Color4fRendermode`) feeding
      the dynamic ring buffer.
- [x] Beams (all types: points, ents, rings, cylinders, torus, disk, follow)
      with the same noise generation as `ref_gl`.
- [x] Particles (`R_DrawParticles`, tracers, `R_DrawTracer`,
      `R_DrawParticleSystem` if used).
- [ ] Alias (Quake MDL) models for Quake-based mods (low priority).
- [x] Sort all translucent geometry (sprites, beams, triapi, translucent
      entities) consistently with `ref_gl` ordering.

## Acceptance criteria
- HL Gauss/Egon beams, tripmines, CS tracers and HE explosions match.

## Progress (2026-10-14)
- `vk_beams.c` and `vk_rpart.c` are ref_gl's beams and particles on the
  GL-state emulation of `vk_imm.c`, same noise and same order. The client
  3D TriAPI is open, with `TriRenderMode` setting the depth mask like
  ref_gl, so the translucent geometry keeps ref_gl's draw order: solid
  entities, sprites, EFX, client triangles, translucent entities.
- `TriFog`, `FogParams`: story 015. Alias models (Quake) stay out of
  scope.
- Harness: `beams_valve` static scene (egon 74 dB, gauss 62 dB against
  ref_gl), `fire_cstrike` 53 dB (bullet holes are decals, story 016).

## Dependencies
018.
