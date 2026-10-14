# 021 - HDR, tonemapping and post-process framework

## Goal
A render graph with an HDR pipeline, keeping a "classic" mode that is
pixel-identical to parity output.

## Tasks
- [ ] Minimal render graph (passes, transient attachments, automatic
      barriers) replacing ad-hoc pass code from phases 1-5.
- [ ] HDR main target (`R16G16B16A16_SFLOAT`), lightmaps decoded to linear.
- [ ] Tonemappers (classic passthrough, ACES, AgX) and auto exposure.
- [ ] Bloom, SSAO (fallback for non-RT GPUs), FXAA/SMAA, sharpen.
- [ ] `r_vk_quality classic|enhanced|rt` master switch plus per-feature cvars.

## Acceptance criteria
- `classic` mode passes the parity harness unchanged.
- Each effect toggles at runtime without a restart.

## Dependencies
020.
