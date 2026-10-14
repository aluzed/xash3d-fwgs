# 026 - RT global illumination and light extraction

## Context
GoldSrc maps carry baked lightmaps but also the data used to bake them:
light entities in the entity lump and texture lights (`lights.rad`,
`info_texlights`).

## Tasks
- [ ] Parse light entities (`light`, `light_spot`, `light_environment`)
      from the entity lump client-side (no server change), with lightstyle
      links.
- [ ] Texture lights: built-in table for HL/CS `lights.rad` values, plus
      material emissive (022).
- [ ] Light sampling (ReSTIR DI or simpler importance sampling) and one-bounce
      diffuse GI; lightmaps as fallback / blend factor.
- [ ] Dynamic lights from `dlight_t` included as real lights.
- [ ] Per-map overrides file for tuning (`maps/<map>.rtlights`).

## Acceptance criteria
- `c1a0` and `de_dust2` lit by RT match overall brightness of lightmaps
  within a tunable factor; lightstyles still animate.

## Dependencies
024, 027.
