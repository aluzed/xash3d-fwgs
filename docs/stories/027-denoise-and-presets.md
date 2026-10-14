# 027 - Denoising, temporal upscaling and quality presets

## Tasks
- [ ] Motion vectors for world, entities (previous transforms) and skinned
      models (previous bone matrices).
- [ ] Temporal accumulation + spatial denoiser (SVGF / A-SVGF class) per
      signal (shadows, AO, reflections, GI).
- [ ] Temporal upscaling: built-in TAA-U; optional FSR / DLSS integration if
      licensing and SDKs allow (decision recorded here).
- [ ] Presets `low / medium / high / ultra` and per-feature cvars exposed in
      the video options menu.

## Acceptance criteria
- 60+ FPS at 1440p `high` RT preset on RTX 2080 Ti on `de_dust2`.

## Dependencies
024.
