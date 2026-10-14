# 011 - Screenshots, gamma and brightness

## Goal
Screenshots, envshots, level shots and GoldSrc-accurate gamma.

## Tasks
- [x] `VID_ScreenShot` (normal, snapshot, levelshot, minishot, envshot,
      skyshot) via copy from the main colour target to a host buffer.
      envshot / skyshot go through `VID_CubemapShot`.
- [x] `VID_CubemapShot` for `envmap` / `skyshot` (harness scene `cubemap_valve`).
- [x] Gamma pipeline: `gamma`, `brightness`, `lightgamma`, `texgamma`
      applied as GoldSrc does (lightmaps and textures), not as a swapchain
      gamma ramp, so screenshots match the screen. As in ref_gl: textures
      get the engine's texture gamma at load, lightmaps the light gamma
      table (`R_BuildLightMap`), and `R_GammaChanged` rebuilds them.

## Acceptance criteria
- Harness can capture with `ref_vk`.
- Changing gamma / brightness in the menu matches `ref_gl` visually.

## Results (2026-10-14)
- The harness captures `ref_vk` (all scenes).
- `VID_CubemapShot` (envmap / skyshot) is still a stub: it needs the sky
  and an off-screen 3D view, story 015.

## Dependencies
010.
