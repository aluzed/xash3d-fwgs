# 016 - Decals

## Tasks
- [x] `R_DecalShoot`, `R_DecalRemoveAll`, `R_CreateDecalList`,
      `R_ClearAllDecals` with the same clipping as `ref_gl` (decal polys
      clipped against surfaces), so save/restore and changelevel decal
      lists stay compatible.
- [x] Decals on brush entities and studio models (`R_StudioDecalShoot`).
- [ ] Player sprays (custom logos, `tempdecal.wad`) via texture updates.
- [x] Depth bias instead of polygon offset; ring-limited count like GoldSrc
      (`r_decals`).

## Acceptance criteria
- Bullet holes, blood, explosions scorch, sprays render and persist across
  save/load in HL.

## Progress (2026-10-14)
- `vk_decals.c` is ref_gl's `gl_decals.c`: same clipping, pool, decal
  lists and save/restore entries. Decals draw through the immediate mode
  between the texture and the lightmap passes of their surfaces, batched
  for normal entities and in order for the others, like ref_gl.
- Polygon offset is the immediate mode's push/pop stack, set as Vulkan
  depth bias; brush models push theirs like ref_gl.
- Decals on `{` surfaces use ref_gl's stencil clip (the depth format has
  stencil): Vulkan 1.3 dynamic stencil state and a no-colour pipeline.
- Studio decals exist only through the client's `R_CreateStudioDecalList`
  and `R_ClearStudioDecals` callbacks, as in ref_gl.
- Not verified by a capture: sprays (the engine uploads them as textures),
  decals on transparent surfaces.
- Harness: gauss scorch 70 dB, `cs_office` 66 dB and up.

## Dependencies
014.
