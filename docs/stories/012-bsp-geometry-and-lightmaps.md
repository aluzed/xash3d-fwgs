# 012 - BSP world geometry and lightmaps

## Goal
Convert loaded BSP models (v30 GoldSrc, plus BSP2 / Quake formats Xash3D
supports) into GPU-friendly static buffers with lightmap atlases.

## Tasks
- [x] Implement `Mod_ProcessRenderData` for brush models: build per-surface
      vertex/index ranges once per map in a single static buffer. Built in
      `R_NewMap` from ref_gl's own polygons (`GL_BuildPolygonFromSurface`,
      `GL_SubdivideSurface` for warped faces), fanned into triangles.
- [x] Lightmap atlas packing (multiple pages as a texture array); 4 lightstyle
      slots per surface uploaded separately so styles can animate on GPU.
      **Changed for parity**: ref_gl's 128x128 pages built on the CPU with
      `R_BuildLightMap` (styles, light gamma, overbright scale), same names
      and texture numbers (`*lightmapN`); changed styles and dynamic lights
      re-upload the surface's texels. GPU styles stay an optimisation for
      later, since GL filters the final 8-bit texels.
- [~] Texture chains grouped by texture for batching; keep per-surface
      metadata (flags, texinfo) in a storage buffer for later RT use (024).
- [x] Respect `gl_overbright` / lightmap scale behaviour of GoldSrc.
      Two passes like ref_gl (texture, then lightmap blended with
      `DST_COLOR, SRC_COLOR` and depth `EQUAL`): coplanar overlapping faces
      are lit twice exactly as in GoldSrc, and the 8-bit intermediate value
      is the same.

## Acceptance criteria
- A static view of `c1a0` and `de_dust2` with world only matches `ref_gl`.
- Map load time no worse than `ref_gl` + 10 %.

## Results (2026-10-14)
World only (`r_drawentities 0`, `r_drawviewmodel 0`), ref_vk vs ref_gl:
c1a0 47-68 dB, c3a2 44-84 dB, cs_office 44-51 dB, c2a5 indoor 58 dB.
What is left is the sky (015), water (014) and static decals (016,
probably the bright spots in cs_office). The harness now forces the shared
renderer cvars to their defaults: `opengl.cfg` had `gl_overbright 0`.
Timedemo with the world: 730-1120 fps (ref_gl 600-1030). Per-surface
storage buffer for RT (024) not done.

## Dependencies
009.
