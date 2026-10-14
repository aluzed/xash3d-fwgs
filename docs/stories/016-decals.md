# 016 - Decals

## Tasks
- [ ] `R_DecalShoot`, `R_DecalRemoveAll`, `R_CreateDecalList`,
      `R_ClearAllDecals` with the same clipping as `ref_gl` (decal polys
      clipped against surfaces), so save/restore and changelevel decal
      lists stay compatible.
- [ ] Decals on brush entities and studio models (`R_StudioDecalShoot`).
- [ ] Player sprays (custom logos, `tempdecal.wad`) via texture updates.
- [ ] Depth bias instead of polygon offset; ring-limited count like GoldSrc
      (`r_decals`).

## Acceptance criteria
- Bullet holes, blood, explosions scorch, sprays render and persist across
  save/load in HL.

## Dependencies
014.
