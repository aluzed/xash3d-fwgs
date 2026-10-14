# 013 - World rendering: visibility, lightstyles, dynamic lights

## Goal
Correct and fast world rendering in motion.

## Tasks
- [x] PVS + frustum culling (reuse engine `Mod_LeafPVS` helpers, frustum code
      from `ref_gl` adapted); generate draw lists per frame.
- [ ] Indirect draws (`vkCmdDrawIndexedIndirect`) for visible surfaces.
- [ ] Lightstyle animation evaluated in the shader from a per-frame style
      table (no lightmap re-upload).
- [ ] Dynamic lights (`dlight_t`, muzzle flashes, flashlight, CS bomb light):
      evaluated per pixel in the shader with a GoldSrc-like falloff, limited
      to lit surfaces as GoldSrc does. Option `r_vk_dlight_classic 1` to
      reproduce the lightmap-blocky look.
- [x] `R_LightPoint` stays CPU-side (gameplay/visual compat, used by models).
- [ ] `r_drawworld`, `r_novis`, `r_lockpvs`, `r_speeds` debug cvars.

## Acceptance criteria
- Walking through HL chapter 1 and `de_dust2`: no missing or flickering
  surfaces; lightstyles (flickering lights, switchable lights) correct.

## Progress (2026-10-14)
- Visibility is ref_gl's code (R_MarkLeaves, R_RecursiveWorldNode with
  efrags, backface and frustum culling); r_novis, r_nocull, r_lockpvs,
  r_lockfrustum exist.
- Light styles and dynamic lights currently rebuild the surface lightmap
  on the CPU, as ref_gl does; the GPU versions are not done.
- Indirect draws not done (one vkCmdDrawIndexed per surface).

## Dependencies
012.
