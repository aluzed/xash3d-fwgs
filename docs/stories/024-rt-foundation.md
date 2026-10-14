# 024 - Ray tracing foundation: acceleration structures

## Goal
Build and maintain acceleration structures for the whole scene each frame,
on GPUs supporting `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`.

## Tasks
- [ ] Static world BLAS per map (opaque and alpha-tested geometry split;
      alpha test in any-hit / ray query candidate handling).
- [ ] Brush entity BLAS instanced with transforms.
- [ ] Studio models: skinned vertices written by a compute pass, BLAS
      refit / rebuild per frame with pooling.
- [ ] Sprites / particles / beams: excluded or as emissive proxies (decision
      recorded here).
- [ ] TLAS per frame with instance masks (world, models, view model,
      translucent).
- [ ] Geometry metadata buffer (material index, texcoords) for hit shading.
- [ ] Runtime toggle `r_vk_rt 0|1` without restart; feature hidden on
      unsupported GPUs.
- [ ] Memory: scratch buffers budgeted; validate in the i386 build.

## Acceptance criteria
- Debug view showing ray-traced primary visibility matches the raster view.
- AS build time under 2 ms on RTX 2080 Ti for `de_dust2` with 32 players.

## Dependencies
021.
