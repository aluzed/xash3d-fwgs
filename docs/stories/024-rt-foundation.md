# 024 - Ray tracing foundation: acceleration structures

## Goal
Build and maintain acceleration structures for the whole scene each frame,
on GPUs supporting `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`.

## Tasks
- [x] Static world BLAS per map (opaque and alpha-tested geometry split;
      alpha test in any-hit / ray query candidate handling).
- [x] Brush entity BLAS instanced with transforms.
- [x] Studio models: BLAS refit / rebuild per frame with pooling.
      Done with the triangles captured as they're drawn (CPU skinning),
      not a compute pass: GPU skinning stays with 017.
- [x] Sprites / particles / beams: excluded or as emissive proxies (decision
      recorded here). Excluded, with decals: flat and often additive, they
      stay drawn over the RT result.
- [x] TLAS per frame with instance masks (world, models, view model,
      translucent).
- [x] Geometry metadata buffer (material index, texcoords) for hit shading.
- [x] Runtime toggle `r_vk_rt 0|1` without restart; feature hidden on
      unsupported GPUs.
- [x] Memory: scratch buffers budgeted; validate in the i386 build.

## Acceptance criteria
- Debug view showing ray-traced primary visibility matches the raster view.
- AS build time under 2 ms on RTX 2080 Ti for `de_dust2` with 32 players.

## Results
- `r_vk_rt_debug 4` (traced depth against raster): no difference but the
  view model, which mode 4 leaves out on purpose.
- `de_dust2`, 31 bots: build 0.89 ms average, 1.67 ms max.
- No validation messages; feature hidden on the i386 build (no RT in
  NVIDIA's 32-bit driver).

## Dependencies
021.
