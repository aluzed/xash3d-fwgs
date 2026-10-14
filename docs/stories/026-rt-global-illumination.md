# 026 - RT global illumination and light extraction

## Context
GoldSrc maps carry baked lightmaps but also the data used to bake them:
light entities in the entity lump and texture lights (`lights.rad`,
`info_texlights`).

## Tasks
- [x] Parse light entities (`light`, `light_spot`, `light_environment`)
      from the entity lump client-side (no server change), with lightstyle
      links. `vk_rtlights.c`; the "style" the compiler writes for named
      lights, else styles from 32 by targetname; the sun from movevars.
- [x] Texture lights: built-in table for HL/CS `lights.rad` values, plus
      material emissive (022). No `lights.rad` ships with the games: every
      `~` texture (not the `+a`... frames of switchable ones) gives 300
      times its average colour, `texlight` in the overrides sets one.
- [x] Light sampling (ReSTIR DI or simpler importance sampling) and one-bounce
      diffuse GI; lightmaps as fallback / blend factor. A 256 unit grid
      lists the lights of each cell; one static light per pixel by
      resampled importance sampling and one bounce (lit by the hit's
      lightmap), accumulated and filtered; sun and styled lights every
      frame. `r_vk_rt_gi` blends with the lightmaps.
- [x] Dynamic lights from `dlight_t` included as real lights (shadowed,
      `R_AddDynamicLights`' falloff).
- [x] Per-map overrides file for tuning (`maps/<map>.rtlights`).

## Acceptance criteria
- `c1a0` and `de_dust2` lit by RT match overall brightness of lightmaps
  within a tunable factor; lightstyles still animate.

## Results
- Mean brightness of the lighting alone (`r_lightmap 1`), lightmaps / rays:
  c1a0 lab 62 / 68, de_dust2 T spawn 72 / 75 (`r_vk_rt_gi_scale 1`,
  `r_vk_rt_gi_sky 0.25`).
- Switching the lights of an office off and on (`ent_fire office1 use`):
  the rays follow at once (history reset where the lightmap changed).
- Cost 1-2.5 ms; `long_c2a5` 379 fps, `long_de_dust2` 366 (606 / 609
  without). Off by default, until 027's presets.
- Light falloff is the inverse square of a 48 unit fixture, not of a
  point: the compilers' lightmaps never show point hot spots.

## Dependencies
024, 027.
