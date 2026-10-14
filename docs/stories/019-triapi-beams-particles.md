# 019 - TriAPI, beams, particles and alias models

## Tasks
- [ ] Full TriAPI (`TriBegin`/`TriEnd`, vertices, render modes, cull face,
      `SpriteTexture`, `Fog`, `WorldToScreen`, `ScreenToWorld`,
      `GetMatrix`, `BoxInPVS`, `LightAtPoint`, `Color4fRendermode`) feeding
      the dynamic ring buffer.
- [ ] Beams (all types: points, ents, rings, cylinders, torus, disk, follow)
      with the same noise generation as `ref_gl`.
- [ ] Particles (`R_DrawParticles`, tracers, `R_DrawTracer`,
      `R_DrawParticleSystem` if used).
- [ ] Alias (Quake MDL) models for Quake-based mods (low priority).
- [ ] Sort all translucent geometry (sprites, beams, triapi, translucent
      entities) consistently with `ref_gl` ordering.

## Acceptance criteria
- HL Gauss/Egon beams, tripmines, CS tracers and HE explosions match.

## Dependencies
018.
