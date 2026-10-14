# 014 - Brush entities, render modes, water and animated textures

## Tasks
- [x] Brush entities with transforms (doors, trains, `func_rotating`).
- [x] Render modes on brush entities: solid, transColor, transTexture,
      transAlpha (`{` textures, CS fences), transAdd, glow; FX amount and
      colour; correct sorting of translucent entities.
- [x] Animated textures (`+0`..`+9`, `+a`..`+j`, toggled by entity frame),
      random tiling (`-0`..), conveyor (`scroll`) textures.
- [x] Water / slime / lava (`!` textures): turbulent warp in shader, water
      surface visibility from inside and outside, underwater view warp
      (`r_waterwarp` style) and contents tint (the tint is the underwater
      fog of story 015; the warp is done on the CPU, see below).
- [ ] Mirrors only if supported by `ref_gl` today; otherwise out of scope.

## Acceptance criteria
- `cs_office` glass, `de_inferno` fences, HL water areas, trains and doors
  match `ref_gl`.

## Progress (2026-10-14)
- Brush entities, render modes, sorting, animated, random tiling and
  conveyor textures follow ref_gl (R_DrawBrushModel, R_TextureAnimation,
  DrawGLPoly's scroll as a texture coordinate offset).
- Water is warped on the CPU every frame like ref_gl's EmitWaterPolys, not
  in a shader: same vertices, same `r_turbsin` lookups, so the images
  match. World water (opaque inline, translucent after the entities when
  the map allows `sv_wateralpha`), water brush entities, wave height and
  lightmapped water are drawn. `r_ripple` is not supported (off in ref_gl
  by default).
- Not verified by a capture yet: translucent world water (no map at hand
  supports it) and `func_water` entities.
- Underwater view: the fov warp is the engine's (V_ApplyRefUnderwater,
  Quake-compatible games only); the contents tint is ref_gl's underwater
  fog (R_CheckFog), done with the fog in story 015.
- Harness: static scene `water_valve` (c2a2a pool), 85.7 dB against
  ref_gl; demo scenes 37/37 above 40 dB.

## Dependencies
013.
