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

## Dependencies
013.
