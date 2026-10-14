# 014 - Brush entities, render modes, water and animated textures

## Tasks
- [ ] Brush entities with transforms (doors, trains, `func_rotating`).
- [ ] Render modes on brush entities: solid, transColor, transTexture,
      transAlpha (`{` textures, CS fences), transAdd, glow; FX amount and
      colour; correct sorting of translucent entities.
- [ ] Animated textures (`+0`..`+9`, `+a`..`+j`, toggled by entity frame),
      random tiling (`-0`..), conveyor (`scroll`) textures.
- [ ] Water / slime / lava (`!` textures): turbulent warp in shader, water
      surface visibility from inside and outside, underwater view warp
      (`r_waterwarp` style) and contents tint.
- [ ] Mirrors only if supported by `ref_gl` today; otherwise out of scope.

## Acceptance criteria
- `cs_office` glass, `de_inferno` fences, HL water areas, trains and doors
  match `ref_gl`.

## Dependencies
013.
