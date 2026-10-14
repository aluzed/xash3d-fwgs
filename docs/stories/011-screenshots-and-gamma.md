# 011 - Screenshots, gamma and brightness

## Goal
Screenshots, envshots, level shots and GoldSrc-accurate gamma.

## Tasks
- [ ] `VID_ScreenShot` (normal, snapshot, levelshot, minishot, envshot,
      skyshot) via copy from the main colour target to a host buffer.
- [ ] `VID_CubemapShot` for `envmap` / `skyshot`.
- [ ] Gamma pipeline: `gamma`, `brightness`, `lightgamma`, `texgamma`
      applied as GoldSrc does (lightmaps and textures), not as a swapchain
      gamma ramp, so screenshots match the screen.

## Acceptance criteria
- Harness can capture with `ref_vk`.
- Changing gamma / brightness in the menu matches `ref_gl` visually.

## Dependencies
010.
