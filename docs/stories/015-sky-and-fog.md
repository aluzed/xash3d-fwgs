# 015 - Sky and fog

## Tasks
- [ ] Skybox from `skyname` (6 faces, `ref/common/ref_skybox.c` naming
      rules), rendered at infinite depth.
- [ ] Quake-style scrolling sky (`R_SetSkyCloudsTextures`) for Quake maps.
- [ ] `sv_skycolor` / `sv_skyvec` exposed for later use (024-026).
- [ ] Fog: GoldSrc `TriFog`/`FogParams` client fog (used by some mods),
      water fog; exact linear / exp formula as `ref_gl`.

## Acceptance criteria
- Outdoor HL and CS maps sky match `ref_gl`; fog in HL water matches.

## Progress (2026-10-14)
- Skybox ported from ref_gl (sky surfaces clipped into per-face bounds, box
  drawn after the world at half the far clip distance, gray stub when a
  face is missing); `R_SetupSky`, `PARM_TEX_SKYBOX`, `PARM_TEX_SKYTEXNUM`.
- World + sky, ref_vk vs ref_gl (entities off): 37/37 captures above 40 dB,
  most of them 70-88 dB.
- Fog and the Quake sky sphere / clouds are not done.

## Dependencies
013.
