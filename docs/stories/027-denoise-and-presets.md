# 027 - Denoising, temporal upscaling and quality presets

## Tasks
- [x] Motion vectors for world, entities (previous transforms) and skinned
      models (previous bone matrices). From the ray of each pixel: brush
      entities' previous transforms, studio models' previous frame
      triangles (copied by the GPU: the CPU refills the dynamic ring's slot
      while the next frame runs), in place of bone matrices.
- [x] Temporal accumulation + spatial denoiser (SVGF / A-SVGF class) per
      signal (shadows, AO, reflections, GI). Accumulation with motion and
      a depth (and normal) aware filter per signal, not SVGF's variance
      guided a-trous: soft sun shadows (new, 4 rays, 4 frames, 3x3), AO,
      GI (reset where a lightmap changed); reflections are mirrors.
- [x] Temporal upscaling: built-in TAA-U; optional FSR / DLSS integration if
      licensing and SDKs allow (decision recorded here). Decision: none.
      1440p high runs at 315 fps; TAA blur and lag hurt GoldSrc's look and
      online play. FSR 2/3 (MIT) possible later for weak GPUs; DLSS's SDK
      licence doesn't go with the GPL.
- [ ] A light `enhanced` preset for integrated GPUs (half size bloom
      start, SSAO off or quarter size, FXAA), measured there. The `low`
      preset exists (quarter size first bloom level, no SSAO, FXAA); not
      measured: no integrated GPU here (with story 020's checks).
- [x] Presets `low / medium / high / ultra` and per-feature cvars exposed in
      the video options menu. `r_vk_preset` (and `classic`), "Rendering
      quality" in the menu (both mainui copies), check boxes for the four
      ray traced effects.

## Acceptance criteria
- 60+ FPS at 1440p `high` RT preset on RTX 2080 Ti on `de_dust2`.

## Results
- 2560x1440, `long_de_dust2`: `high` 315 fps (1 % low 196), `ultra` 180
  (99); enhanced 795. `long_c2a5` `ultra` 206.
- Classic and enhanced captures identical to before; no validation
  message; i386 build fine.

## Dependencies
024.
